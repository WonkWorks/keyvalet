/* Real network worker and credential adapter; simulated IDF events/NVS only.
 * No radio, serial port, GPIO, filesystem store, or physical device is used. */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../main/network_manager.c"
#include "improv_serial.h"
#include "nvs.h"

const char WIFI_EVENT[] = "wifi", IP_EVENT[] = "ip";
static message_t queued[16];
static unsigned queue_head, queue_count, starts, stops, connects, scans, writes, commits;
static bool radio_on, ap_connected, gate, saved_present, fail_before, fail_after;
static uint8_t durable[112], pending[112];
static wifi_config_t radio_config;
static wifi_ap_record_t access_points[32];
static uint16_t ap_count;
static int64_t simulated_ms = 1000;
static esp_netif_ip_info_t current_ip;
static keyvalet_network_error_t last_result;
static unsigned successes, failures, scan_entries, scan_done, notifications;
static keyvalet_network_scan_result_t entries[32];

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{ SemaphoreHandle_t mutex = malloc(sizeof(*mutex)); assert(mutex); assert(!pthread_mutex_init(mutex,NULL)); return mutex; }
int xSemaphoreTake(SemaphoreHandle_t mutex, unsigned timeout)
{ (void)timeout; assert(!pthread_mutex_lock(mutex)); return 1; }
int xSemaphoreGive(SemaphoreHandle_t mutex)
{ assert(!pthread_mutex_unlock(mutex)); return 1; }
QueueHandle_t xQueueCreate(unsigned count, size_t size)
{ assert(count==16 && size==sizeof(message_t)); return queued; }
int xQueueSend(QueueHandle_t queue, const void *item, unsigned timeout)
{ (void)queue;(void)timeout; if(queue_count==16)return 0; queued[(queue_head+queue_count++)%16]=*(const message_t *)item; return pdTRUE; }
int xQueueReceive(QueueHandle_t queue, void *item, unsigned timeout)
{ (void)queue;(void)timeout; if(!queue_count)return 0; *(message_t *)item=queued[queue_head++%16];--queue_count; return pdTRUE; }
int xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle)
{ (void)task;(void)name;(void)stack;(void)arg;(void)priority;(void)handle;return pdPASS; }
int64_t esp_timer_get_time(void) {return simulated_ms*1000;}
esp_err_t esp_netif_init(void) {return ESP_OK;}
void *esp_netif_create_default_wifi_sta(void) {return &radio_on;}
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif,esp_netif_ip_info_t *info)
{(void)netif;*info=current_ip;return ESP_OK;}
esp_err_t esp_event_loop_create_default(void) {return ESP_OK;}
esp_err_t esp_event_handler_register(esp_event_base_t base,int32_t id,
    void (*handler)(void *,esp_event_base_t,int32_t,void *),void *arg)
{(void)base;(void)id;(void)handler;(void)arg;return ESP_OK;}
esp_err_t esp_read_mac(uint8_t *mac,int type)
{(void)type;const uint8_t fixed[]={0,1,2,3,4,5};memcpy(mac,fixed,6);return ESP_OK;}
esp_err_t keyvalet_time_sync_init(void) {return ESP_OK;}
esp_err_t esp_wifi_init(const wifi_init_config_t *config) {(void)config;return ESP_OK;}
esp_err_t esp_wifi_set_storage(int storage) {assert(storage==WIFI_STORAGE_RAM);return ESP_OK;}
esp_err_t esp_wifi_set_mode(int mode) {assert(mode==WIFI_MODE_STA);return ESP_OK;}
esp_err_t esp_wifi_set_config(int interface,const wifi_config_t *config)
{assert(interface==WIFI_IF_STA && !radio_on);radio_config=*config;return ESP_OK;}
esp_err_t esp_wifi_start(void)
{radio_on=true;++starts;network_event_handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_START,NULL);return ESP_OK;}
esp_err_t esp_wifi_stop(void)
{
    if(!radio_on)return ESP_ERR_WIFI_NOT_STARTED;
    radio_on=false;ap_connected=false;current_ip.ip.addr=0;++stops;
    network_event_handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
    network_event_handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_STOP,NULL);return ESP_OK;
}
esp_err_t esp_wifi_connect(void) {assert(radio_on);++connects;return ESP_OK;}
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config,bool block)
{assert(radio_on && !block && !config->show_hidden);++scans;return ESP_OK;}
esp_err_t esp_wifi_scan_stop(void) {return ESP_OK;}
esp_err_t esp_wifi_clear_ap_list(void) {return ESP_OK;}
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *count,wifi_ap_record_t *records)
{if(*count>ap_count)*count=ap_count;memcpy(records,access_points,*count*sizeof(*records));return ESP_OK;}
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *record)
{if(!ap_connected)return ESP_FAIL;memset(record,0,sizeof(*record));memcpy(record->ssid,radio_config.sta.ssid,32);record->rssi=-42;return ESP_OK;}
esp_err_t keyvalet_lock_flash_begin(void) {assert(!gate);gate=true;return ESP_OK;}
void keyvalet_lock_flash_end(void) {assert(gate);gate=false;}
uint32_t esp_crc32_le(uint32_t initial,const uint8_t *data,uint32_t length)
{
    uint32_t crc=~initial;
    while(length--){crc^=*data++;for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320U&(0U-(crc&1U)));}
    return ~crc;
}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *handle)
{assert(!strcmp(name,"keyvalet_wifi") && mode==NVS_READWRITE);*handle=1;return ESP_OK;}
esp_err_t nvs_get_blob(nvs_handle_t handle,const char *key,void *value,size_t *length)
{assert(handle==1 && !strcmp(key,"station"));if(!saved_present)return ESP_ERR_NVS_NOT_FOUND;assert(*length>=112);memcpy(value,durable,112);*length=112;return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t handle,const char *key,const void *value,size_t length)
{assert(handle==1 && !strcmp(key,"station") && length==112 && gate);++writes;if(fail_before)return ESP_FAIL;memcpy(pending,value,112);return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t handle)
{assert(handle==1 && gate);++commits;memcpy(durable,pending,112);saved_present=true;return fail_after?ESP_FAIL:ESP_OK;}

static void listener(keyvalet_network_provision_event_t event,keyvalet_network_error_t error,
    const keyvalet_network_scan_result_t *entry)
{
    if(event==KEYVALET_NETWORK_PROVISION_RESULT){last_result=error;if(error==KEYVALET_NETWORK_OK)++successes;else ++failures;}
    if(event==KEYVALET_NETWORK_SCAN_RESULT){assert(scan_entries<32);entries[scan_entries++]=*entry;}
    if(event==KEYVALET_NETWORK_SCAN_DONE){++scan_done;last_result=error;}
}
static void notify(bool connected,void *arg) {(void)connected;(void)arg;++notifications;}
static void pump(void)
{
    message_t message;unsigned bound=0;
    while(xQueueReceive(s_queue,&message,0)==pdTRUE){assert(++bound<50);handle_message(&message);}
    network_tick();
}
static void boot(void)
{
    assert(keyvalet_network_manager_start(notify,NULL)==ESP_OK);
    keyvalet_network_set_provision_callback(listener);
    s_after_stop=USE_SAVED;apply_after_stop();pump();
}
static void configure(const char *ssid,const char *password)
{
    keyvalet_wifi_credentials_t credentials={0};
    snprintf(credentials.ssid,sizeof(credentials.ssid),"%s",ssid);
    snprintf(credentials.password,sizeof(credentials.password),"%s",password);
    assert(keyvalet_network_configure(&credentials)==ESP_OK);pump();
}
static void ip(void)
{ap_connected=true;current_ip.ip.addr=0x2a01a8c0;ip_event_got_ip_t event={.ip_info.ip.addr=0x2a01a8c0};network_event_handler(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&event);pump();}
static void establish(void) {configure("old-network","old-password");ip();assert(successes==1 && commits==1);}
static void unconfigured(void)
{
    boot();assert(starts==1 && connects==0 && s_status.available && !s_status.configured);
    assert(!writes && !commits);assert(!strcmp(keyvalet_network_device_id(),"keyvalet-030405"));
    assert(keyvalet_network_scan()==ESP_OK);pump();assert(scans==1 && s_status.scanning);
    wifi_event_sta_scan_done_t done={0};network_event_handler(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,&done);pump();
    assert(scan_done==1 && !scan_entries && !s_busy);
    configure("hidden","correct-password");assert(!writes && s_phase==CANDIDATE);
    /* Association alone is not success. No DHCP before deadline => rollback. */
    ap_connected=true;simulated_ms+=30001;pump();pump();
    assert(failures==1 && last_result==KEYVALET_NETWORK_TIMEOUT && !commits && !s_have_saved);
}
static void success_and_reconnect(void)
{
    boot();establish();assert(s_status.connected && s_status.configured && !s_busy);
    keyvalet_network_status_t snapshot;
    assert(keyvalet_network_status_get(&snapshot)==ESP_OK && !strcmp(snapshot.ssid,"old-network"));
    assert(radio_config.sta.sae_pwe_h2e==WPA3_SAE_PWE_BOTH);
    assert(radio_config.sta.threshold.authmode==WIFI_AUTH_WPA2_PSK);
    keyvalet_wifi_credentials_t loaded;
    assert(keyvalet_wifi_store_load(&loaded)==ESP_OK && !strcmp(loaded.ssid,"old-network"));
    assert(!strcmp(loaded.password,"old-password"));
    network_event_handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);pump();
    assert(!s_status.connected && s_reconnect_at==simulated_ms+1000);
    assert(keyvalet_network_status_get(&snapshot)==ESP_OK && !snapshot.ssid[0]);
    simulated_ms+=1000;pump();assert(connects==2);ip();assert(commits==1);
    configure("old-network","old-password");ip();assert(successes==2 && commits==1);
    configure("new-network","new-password");assert(commits==1);ip();
    assert(successes==3 && commits==2 && !strcmp(s_saved.ssid,"new-network"));
}
static void failures_preserve(void)
{
    boot();establish();configure("bad-network","bad-password");
    network_event_handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);pump();pump();
    assert(failures==1 && !strcmp(s_saved.ssid,"old-network") && commits==1);
    assert(!memcmp(radio_config.sta.ssid,"old-network",11));ip();
    assert(s_status.connected && successes==1);
    configure("no-dhcp","new-password");simulated_ms+=30001;pump();pump();
    assert(failures==2 && commits==1 && last_result==KEYVALET_NETWORK_TIMEOUT);
    /* A reboot during an attempt reads the last committed, not candidate. */
    configure("pending","new-password");
    keyvalet_wifi_credentials_t durable_state;
    assert(keyvalet_wifi_store_load(&durable_state)==ESP_OK);
    assert(!strcmp(durable_state.ssid,"old-network"));
}
static void persistence_errors(void)
{
    boot();establish();fail_before=true;configure("not-saved","new-password");ip();pump();
    assert(last_result==KEYVALET_NETWORK_STORAGE_FAILED && commits==1 && successes==1);
    assert(s_status.persistence_uncertain && !strcmp(s_saved.ssid,"old-network"));
    fail_before=false;configure("blocked-until-reboot","new-password");ip();pump();
    assert(last_result==KEYVALET_NETWORK_STORAGE_FAILED && commits==1);
    configure("old-network","old-password");ip();pump();
    assert(last_result==KEYVALET_NETWORK_STORAGE_FAILED && successes==1 && commits==1);
}
static void ambiguous_commit(void)
{
    boot();establish();fail_after=true;configure("maybe-durable","new-password");ip();pump();
    assert(last_result==KEYVALET_NETWORK_STORAGE_FAILED && successes==1 && commits==2);
    assert(!strcmp(s_saved.ssid,"old-network"));
    keyvalet_wifi_credentials_t reboot;
    assert(keyvalet_wifi_store_load(&reboot)==ESP_OK && !strcmp(reboot.ssid,"maybe-durable"));
}
static void scan_and_bounds(void)
{
    boot();establish();const unsigned before=stops;
    snprintf((char *)access_points[0].ssid,33,"same");access_points[0].rssi=-80;access_points[0].authmode=WIFI_AUTH_WPA2_PSK;
    access_points[1]=access_points[0];access_points[1].rssi=-20;
    snprintf((char *)access_points[2].ssid,33,"open");access_points[2].rssi=-30;
    access_points[3].ssid[0]=27;access_points[3].rssi=-10;ap_count=4;
    assert(keyvalet_network_scan()==ESP_OK);pump();assert(stops==before && s_status.connected);
    assert(keyvalet_network_scan()!=ESP_OK);
    wifi_event_sta_scan_done_t done={0};network_event_handler(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,&done);pump();
    assert(scan_entries==2 && !strcmp(entries[0].ssid,"same") && entries[0].rssi==-20);
    assert(!strcmp(entries[1].authentication,"OPEN") && !entries[1].secured);
    assert(keyvalet_network_scan()==ESP_OK);pump();simulated_ms+=10001;pump();pump();
    assert(scan_done==2 && last_result==KEYVALET_NETWORK_TIMEOUT);
    keyvalet_wifi_credentials_t credentials={.ssid="x",.password="short"};
    assert(keyvalet_network_configure(&credentials)==ESP_ERR_INVALID_ARG);
    memset(credentials.ssid,'s',32);credentials.ssid[32]=0;credentials.password[0]=0;
    assert(keyvalet_wifi_credentials_valid(&credentials));
    credentials.ssid[0]=27;assert(!keyvalet_wifi_credentials_valid(&credentials));
}
static void stale_events_and_corruption(void)
{
    boot();establish();
    keyvalet_wifi_credentials_t candidate={.ssid="new",.password="new-password"};
    assert(keyvalet_network_configure(&candidate)==ESP_OK);
    message_t message;assert(xQueueReceive(s_queue,&message,0));handle_message(&message);
    assert(s_phase==STOPPING);
    message=(message_t){.type=GOT_IP,.data.ip.addr=0x2a01a8c0};handle_message(&message);
    assert(commits==1 && !s_status.connected);pump();ip();assert(commits==2);
    durable[50]^=1;
    keyvalet_wifi_credentials_t corrupt;
    assert(keyvalet_wifi_store_load(&corrupt)==ESP_ERR_INVALID_CRC && !corrupt.ssid[0]);
}
static void saved_boot_and_usb_disconnect(void)
{
    keyvalet_wifi_credentials_t credentials={.ssid="saved-boot",.password="saved-password"},loaded;
    assert(keyvalet_wifi_store_load(&loaded)==ESP_ERR_NVS_NOT_FOUND);
    assert(keyvalet_wifi_store_save(&credentials)==ESP_OK);
    boot();assert(s_status.configured && connects==1 && commits==1);
    network_event_handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);pump();
    const unsigned intervals[]={1000,2000,4000,8000,16000,30000,30000};
    for(size_t i=0;i<sizeof(intervals)/sizeof(intervals[0]);++i){
        assert(s_reconnect_at==simulated_ms+intervals[i]);simulated_ms+=intervals[i];pump();
        network_event_handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);pump();
    }
    assert(commits==1);ip();
    keyvalet_network_status_t snapshot;
    assert(keyvalet_network_status_get(&snapshot)==ESP_OK && snapshot.connected &&
           !strcmp(snapshot.ssid,"saved-boot") && !strcmp(snapshot.ip_address,"192.168.1.42"));
    /* USB loss does not cancel the autonomous candidate transaction. */
    keyvalet_network_set_provision_callback(NULL);
    configure("headless-completion","new-password");ip();
    assert(commits==2 && s_status.connected && !s_busy);
    assert(!strcmp(s_saved.ssid,"headless-completion"));
}
static void corrupt_boot_and_event_loss(void)
{
    saved_present=true;memset(durable,0xa5,sizeof(durable));
    boot();assert(!s_status.configured && !writes && !connects);
    configure("replacement","new-password");ip();assert(successes==1 && commits==1);
    configure("event-overflow","new-password");
    atomic_store(&s_event_overflow,true);network_tick();pump();
    assert(last_result==KEYVALET_NETWORK_UNAVAILABLE && commits==1 && !s_busy);
    assert(!strcmp(s_saved.ssid,"replacement"));
}
int main(void)
{
    void (*cases[])(void)={unconfigured,success_and_reconnect,failures_preserve,persistence_errors,
        ambiguous_commit,scan_and_bounds,stale_events_and_corruption,
        saved_boot_and_usb_disconnect,corrupt_boot_and_event_loss};
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i){
        pid_t child=fork();assert(child>=0);if(!child){cases[i]();_exit(0);}
        int status;assert(waitpid(child,&status,0)==child);if(!WIFEXITED(status)||WEXITSTATUS(status))fprintf(stderr,"Wi-Fi case %zu failed\n",i);
        assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);
    }
    puts("Wi-Fi provisioning tests passed: real worker/store, blank/scan/candidate/DHCP/rollback/CRC/ambiguous persistence");
}
