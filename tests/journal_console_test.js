/* Execute the reference console script against a local fake WebSocket/DOM. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const html = fs.readFileSync('tools/m5_console.html', 'utf8');
const elements = new Map();
const sent = [];
const downloads = [];
let confirmations = 0;
const context = {
    document: {
        querySelector: id => {
            if (!elements.has(id)) elements.set(id, {value: '127.0.0.1', textContent: ''});
            return elements.get(id);
        },
        createElement: () => ({click(){}}),
    },
    crypto: {randomUUID: () => 'test-request'},
    Date, Set, JSON, Error, Number, Object,
    Blob: class {constructor(parts){downloads.push(JSON.parse(parts[0]))}},
    URL: {createObjectURL: () => 'blob:test', revokeObjectURL(){}},
    confirm: () => {++confirmations; return false},
    WebSocket: class {
        static OPEN = 1;
        constructor(){this.readyState = 1}
        send(text){sent.push(JSON.parse(text))}
    },
};
vm.createContext(context);
vm.runInContext(html.split('<script>')[1].split('</script>')[0], context);
const run = source => vm.runInContext(source, context);
run("document.querySelector('#connect').onclick()");
const message = data => run(`ws.onmessage({data:${JSON.stringify(JSON.stringify(data))}})`);
message({type:'welcome',device_id:'keyvalet-test',journal_sync:{available:true,
    acknowledged_sequence:100,unsynchronized_records:true}});
assert.equal(sent[0].from_sequence, 101);
message({type:'journal_batch',device_id:'keyvalet-test',returned_count:0,records:[],
    gap:{exists:true,start_sequence:101,end_sequence:128},next_sequence:129,more:true});
assert.equal(sent.at(-1).from_sequence,129);
const batch={type:'journal_batch',device_id:'keyvalet-test',returned_count:2,
    records:[129,130].map(sequence=>({device_id:'keyvalet-test',sequence})),
    gap:{exists:false},next_sequence:131,more:false};
message(batch);
message(batch); // duplicate read/response is harmless
assert.equal(run('journalRecords.length'),2);
assert.equal(run('journalGaps.length'),1);
assert.equal(run('journalCursor'),130);
assert.equal(sent.filter(x=>x.type==='journal_ack').length,0);
assert.equal(confirmations,0);
run("document.querySelector('#exportJournal').onclick()");
assert.equal(downloads[0].records.length,2);
assert.deepEqual(downloads[0].gaps,[{device_id:'keyvalet-test',start_sequence:101,end_sequence:128}]);
run("document.querySelector('#syncJournal').onclick()");
assert.equal(sent.at(-1).from_sequence,131);
// Reconnection resumes from durable cursor, regardless of earlier memory receipt.
message({type:'welcome',device_id:'keyvalet-test',journal_sync:{available:true,
    acknowledged_sequence:100,unsynchronized_records:true}});
assert.equal(sent.at(-1).from_sequence,101);
console.log('journal console tests passed: automatic/explicit reads, retry, dedup, exported loss, no automatic ACK');

(async () => {
    const config = {protocol_version:3, schema_version:4, revision:7,
        locks:{lock_1:{sensor_required:true},lock_2:{sensor_required:false}},
        default_door_open_timeout_s:90, sensors:Object.fromEntries(
            [1,2,3,4].map(i=>[`sensor_${i}`,{monitoring_enabled:i!==2}]))};
    const requests=[];
    context.fetch = async (path,options) => {
        const body=options?.body ? JSON.parse(options.body) : null;
        if(body)requests.push({path,body});
        let response=config;
        if(path==='/api/sign/unlock')response={type:'unlock',...body};
        if(path==='/api/sign/config')response={protocol_version:3,...body.config};
        if(options?.method==='PATCH'){
            Object.assign(config,body);config.revision++;
            response={accepted:true};
        }
        return {ok:true,json:async()=>response};
    };
    await run('readConfiguration()');
    assert.equal(run('revision'),7);
    assert.equal(elements.get('#sensor_1').checked,true);
    assert.equal(elements.get('#sensor_2').checked,false);
    assert.equal(elements.get('#lock_1_required').checked,true);
    assert.equal(elements.get('#lock_2_required').checked,false);
    assert.match(elements.get('#configurationState').textContent,/Lock 2: sensor-independent/);
    elements.get('#lock_1_required').checked=false;
    elements.get('#lock_2_required').checked=true;
    elements.get('#sensor_3').checked=false;
    await run("document.querySelector('#updateConfig').onclick()");
    const admin=requests.find(request=>request.path==='/api/sign/config').body.config;
    assert.deepEqual(admin.locks,{lock_1:{sensor_required:false},lock_2:{sensor_required:true}});
    assert.equal(Object.keys(admin.sensors).length,4);
    assert.equal(admin.sensors.sensor_3.monitoring_enabled,false);
    assert.equal(run('revision'),8);
    context.document.querySelector('#lock').value='lock_2';
    context.document.querySelector('#unlockTimeout');
    for(const value of ['0','','5','3600']){
        elements.get('#unlockTimeout').value=value;
        await run("document.querySelector('#unlock').onclick()");
        const request=requests.at(-1).body;
        assert.equal(request.lock_id,'lock_2');
        if(value==='')assert.equal(Object.hasOwn(request,'door_open_timeout_s'),false);
        else assert.equal(request.door_open_timeout_s,Number(value));
    }
    assert.match(elements.get('#log').textContent,/reminder disabled/);
    const signedRequests=requests.length;
    for(const value of ['1','2','3','4','-1','3601','5.5']){
        elements.get('#unlockTimeout').value=value;
        await run("document.querySelector('#unlock').onclick()");
    }
    assert.equal(requests.length,signedRequests);
    delete config.sensors.sensor_4;
    await assert.rejects(run('readConfiguration()'), /canonical two-lock\/four-sensor/);
    console.log('console configuration tests passed: dependencies, signed admin, zero/default/positive and invalid timeouts');
})().catch(error => {console.error(error); process.exitCode = 1});
