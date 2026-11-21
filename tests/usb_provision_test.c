/* Execute the actual USB RX dispatcher and handlers in both build variants. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../main/usb_provision.c"

static uint8_t input[2048];
static size_t input_length, input_offset;
static jmp_buf completed;
static int64_t clock_ms;
static unsigned diagnostic_commands, credential_commands, scan_commands;
static keyvalet_network_status_t status={.available=true};
static keyvalet_network_provision_callback_t listener;
static keyvalet_wifi_credentials_t received;
static bool host_connected=true;
static unsigned staging_requests;
static const keyvalet_hardware_profile_t profile={.name="keyvalet-v0.4.2"};

const esp_app_desc_t *esp_app_get_description(void)
{static const esp_app_desc_t app={.version="test"};return &app;}
esp_err_t usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t *config)
{(void)config;return ESP_OK;}
void usb_serial_jtag_vfs_use_driver(void) {}
bool usb_serial_jtag_is_connected(void) {return host_connected;}
int usb_serial_jtag_write_bytes(const void *data,size_t length,unsigned timeout)
{assert(timeout==pdMS_TO_TICKS(50));return (int)fwrite(data,1,length,stdout);}
int usb_serial_jtag_read_bytes(void *data,size_t length,unsigned timeout)
{
    (void)timeout;assert(length==1);
    if(input_offset==input_length)longjmp(completed,1);
    *(uint8_t *)data=input[input_offset++];++clock_ms;return 1;
}
int64_t esp_timer_get_time(void) {return clock_ms*1000;}
int xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle)
{(void)task;(void)name;(void)stack;(void)arg;(void)priority;(void)handle;return pdPASS;}
esp_err_t keyvalet_network_status_get(keyvalet_network_status_t *output)
{*output=status;return ESP_OK;}
const char *keyvalet_network_device_id(void) {return "keyvalet-030405";}
void keyvalet_tls_status_get(keyvalet_tls_status_t *tls)
{memset(tls,0,sizeof(*tls));tls->ready=true;strcpy(tls->hostname,"kv-test.devices.example.com");}
bool keyvalet_tls_usb_handle(const uint8_t *body,size_t length,keyvalet_tls_usb_reply_t reply)
{(void)body;(void)length;(void)reply;return false;}
void keyvalet_tls_usb_expire(void) {}
void keyvalet_network_set_provision_callback(keyvalet_network_provision_callback_t callback)
{listener=callback;}
esp_err_t keyvalet_network_scan(void)
{
    ++scan_commands;
    keyvalet_network_scan_result_t entry={.ssid="example",.rssi=-44,.secured=true,.authentication="WPA2-Personal"};
    listener(KEYVALET_NETWORK_SCAN_RESULT,KEYVALET_NETWORK_OK,&entry);
    listener(KEYVALET_NETWORK_SCAN_DONE,KEYVALET_NETWORK_OK,NULL);
    return ESP_OK;
}
esp_err_t keyvalet_network_configure(const keyvalet_wifi_credentials_t *credentials)
{received=*credentials;++credential_commands;return ESP_OK;}
esp_err_t keyvalet_security_stage_enrollment_key(const char *key,char fingerprint[65],uint32_t *remaining)
{
    assert(strlen(key)==130 && key[0]=='0' && key[1]=='4');++staging_requests;
    memset(fingerprint,'a',64);fingerprint[64]=0;*remaining=300;return ESP_OK;
}
#ifdef CONFIG_KEYVALET_DIAGNOSTICS
esp_err_t keyvalet_diagnostic_console_init(const keyvalet_hardware_profile_t *hardware)
{assert(hardware==&profile);return ESP_OK;}
void keyvalet_diagnostic_console_handle(const char *command)
{assert(!strcmp(command,"KVTEST CHIRP"));++diagnostic_commands;}
#endif

static void append(const void *data,size_t length)
{assert(input_length+length<=sizeof(input));memcpy(input+input_length,data,length);input_length+=length;}
static void rpc(uint8_t command,const uint8_t *body,size_t length)
{
    uint8_t payload[255]={command,(uint8_t)length},packet[265];
    if(length)memcpy(payload+2,body,length);
    const size_t size=keyvalet_improv_frame(packet,3,payload,length+2);append(packet,size);append("\n",1);
}

static void current_network_checks(FILE *capture)
{
    const keyvalet_network_status_t cases[]={
        {.available=true,.configured=true,.connected=true,.ssid="example-network",.ip_address="192.168.1.27"},
        /* Stale SSID data must never become a connected-network claim. */
        {.available=true,.configured=true,.ssid="previous"},
        {.available=true},
        {.available=true,.provisioning=true,.ssid="candidate"},
        {0},
        /* Reinitialized snapshot: no prior USB request or selection is needed. */
        {.available=true,.configured=true,.connected=true,.ssid="after-reboot",.ip_address="192.168.1.28"},
        {.available=true,.configured=true,.connected=true,.ssid="12345678901234567890123456789012",.ip_address="192.168.1.29"},
        {.available=true,.configured=true,.connected=true,.ssid="R\xc3\xa9seau",.ip_address="192.168.1.30"},
    };
    const char *states[]={"connected","disconnected","unconfigured","provisioning","unavailable","connected","connected","connected"};
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i){
        status=cases[i];
        const char *strings[]={states[i],status.connected?status.ssid:""};
        uint8_t frame[265];const size_t size=keyvalet_improv_result(frame,KEYVALET_CURRENT_NETWORK_RPC,strings,2);
        const off_t start=lseek(fileno(capture),0,SEEK_CUR);
        uint8_t query[]={KEYVALET_CURRENT_NETWORK_RPC,0},request[265];
        keyvalet_improv_frame(request,3,query,sizeof(query));handle_improv(request);
        fflush(stdout);const off_t end=lseek(fileno(capture),0,SEEK_CUR);
        assert(end-start==(long)(12+size+1)); /* no-error frame + LF, result + LF */
        uint8_t actual[265];
        assert(pread(fileno(capture),actual,size,start+12)==(ssize_t)size && !memcmp(actual,frame,size));
    }
    /* Standard RPC 7 remains flags plus URL, never SSID. */
    const char *values[]={"3","https://kv-test.devices.example.com/api/v1/status"};
    uint8_t expected[265],actual[265],query[]={7,0},request[265];
    const size_t size=keyvalet_improv_result(expected,7,values,2);
    const off_t start=lseek(fileno(capture),0,SEEK_CUR);
    keyvalet_improv_frame(request,3,query,sizeof(query));handle_improv(request);fflush(stdout);
    const off_t end=lseek(fileno(capture),0,SEEK_CUR);assert(end-start==(long)(12+size+1));
    assert(pread(fileno(capture),actual,size,start+12)==(ssize_t)size && !memcmp(actual,expected,size));
}
int main(void)
{
    /* Protocol boundaries and malformed nested lengths. */
    uint8_t payload[110]={1,0,32};memset(payload+3,'s',32);payload[35]=63;memset(payload+36,'p',63);payload[1]=97;
    keyvalet_wifi_credentials_t credentials;
    assert(keyvalet_improv_credentials(payload,99,&credentials));
    payload[35]=65;assert(!keyvalet_improv_credentials(payload,99,&credentials));payload[35]=63;
    payload[3]=0;assert(!keyvalet_improv_credentials(payload,99,&credentials));payload[3]='s';
    /* Arbitrary noise/length/checksum input cannot overrun the fixed parser. */
    keyvalet_improv_parser_t parser={0};uint32_t random=42;
    for(unsigned i=0;i<100000;++i){random=random*1664525U+1013904223U;(void)keyvalet_improv_feed(&parser,(uint8_t)(random>>24),i);assert(parser.used<265);}
    memset(&parser,0,sizeof(parser));
    uint8_t binary_payload[38]={0x82,36,1,2,32,0};
    memcpy(binary_payload+12,"IMPROV\x01",7);
    uint8_t binary_packet[265];
    size_t binary_size=keyvalet_improv_frame(binary_packet,3,binary_payload,sizeof(binary_payload));
    int binary_result=0;
    for(size_t i=0;i<binary_size;++i)binary_result=keyvalet_improv_feed(&parser,binary_packet[i],i);
    assert(binary_result==1 && !memcmp(parser.bytes+9,binary_payload,sizeof(binary_payload)));
    memset(&parser,0,sizeof(parser));
    const char partial[]="IMPROV\x01\x03\xff";
    for(size_t i=0;i<sizeof(partial)-1;++i)(void)keyvalet_improv_feed(&parser,partial[i],i);
    uint8_t request[265],query[]={2,0};size_t size=keyvalet_improv_frame(request,3,query,2);
    int parsed=0;for(size_t i=0;i<size;++i)parsed=keyvalet_improv_feed(&parser,request[i],20+i);
    assert(parsed==1);
    /* Capture only the simulated stdout transport. No physical serial is opened. */
    FILE *capture=tmpfile();assert(capture);fflush(stdout);int original=dup(STDOUT_FILENO);assert(original>=0);
    assert(dup2(fileno(capture),STDOUT_FILENO)>=0);
    assert(keyvalet_usb_provision_start(&profile)==ESP_OK);
    current_network_checks(capture);status=(keyvalet_network_status_t){.available=true};
    rpc(3,NULL,0);rpc(2,NULL,0);rpc(7,NULL,0);rpc(KEYVALET_CURRENT_NETWORK_RPC,NULL,0);rpc(4,NULL,0);
    const uint8_t unexpected=1;rpc(KEYVALET_CURRENT_NETWORK_RPC,&unexpected,1);
    uint8_t public_key[131]={130,'0','4'};memset(public_key+3,'a',128);
    rpc(KEYVALET_STAGE_ENROLLMENT_KEY_RPC,public_key,sizeof(public_key));
    rpc(KEYVALET_STAGE_ENROLLMENT_KEY_RPC,public_key,sizeof(public_key)-1);
    public_key[20]=0;rpc(KEYVALET_STAGE_ENROLLMENT_KEY_RPC,public_key,sizeof(public_key));
    append("KVTEST CHIRP\n",13);
    const uint8_t secret[]={4,'t','e','s','t',12,'K','V','T','E','S','T',' ','s','e','c','r','t'};
    rpc(1,secret,sizeof(secret));
    request[size-1]^=1;append(request,size);append("\n",1);
    rpc(99,NULL,0);rpc(2,NULL,0);
    if(!setjmp(completed))usb_task(NULL);
    assert(credential_commands==1 && !strcmp(received.ssid,"test") && !strcmp(received.password,"KVTEST secrt"));
    assert(scan_commands==1);
    assert(staging_requests==1);
#ifdef CONFIG_KEYVALET_DIAGNOSTICS
    assert(diagnostic_commands==1);
#else
    assert(diagnostic_commands==0);
#endif
    status.connected=true;snprintf(status.ip_address,16,"192.168.1.42");
    listener(KEYVALET_NETWORK_PROVISION_RESULT,KEYVALET_NETWORK_OK,NULL);
    listener(KEYVALET_NETWORK_PROVISION_RESULT,KEYVALET_NETWORK_STORAGE_FAILED,NULL);
    host_connected=false;listener(KEYVALET_NETWORK_PROVISION_RESULT,KEYVALET_NETWORK_OK,NULL);
    fflush(stdout);long length=ftell(capture);rewind(capture);uint8_t output[4096]={0};assert(length>0 && length<(long)sizeof(output));
    assert(fread(output,1,(size_t)length,capture)==(size_t)length);
    assert(dup2(original,STDOUT_FILENO)>=0);close(original);fclose(capture);
    assert(!memmem(output,(size_t)length,"KVTEST secrt",12));
    unsigned frames=0, errors=0, provisioning_results=0;
    memset(&parser,0,sizeof(parser));
    for(long i=0;i<length;++i)if(keyvalet_improv_feed(&parser,output[i],i)==1){
        ++frames;assert(parser.bytes[7]!=3);
        if(parser.bytes[7]==2 && parser.bytes[9])++errors;
        if(parser.bytes[7]==4 && parser.bytes[9]==1)++provisioning_results;
    }
    assert(frames>=14 && errors>=3 && provisioning_results==1);
    puts("USB provisioning tests passed: real dispatcher/handlers, bounds, malformed recovery, no credentials echoed, variant separation");
}
