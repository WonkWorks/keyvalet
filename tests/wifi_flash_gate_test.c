/* Actual credential adapter + GPIO/timer/arbiter, simulated clock/NVS. */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hardware_io.h"
#include "wifi_store.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "nvs.h"

static unsigned gpio[32], waited;
static int64_t now, due;
static esp_timer_create_args_t timer;
static pthread_t competitor;
static atomic_bool competitor_started, competitor_finished;
static keyvalet_lock_id_t competing_lock;
static const keyvalet_hardware_profile_t profile={.lock_gpio={10,3},.status_led_gpio=4,
    .solenoid_active_high=true,.solenoid_pulse_ms=350,.lock_recovery_ms=2000};

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{SemaphoreHandle_t mutex=malloc(sizeof(*mutex));assert(mutex && !pthread_mutex_init(mutex,NULL));return mutex;}
int xSemaphoreTake(SemaphoreHandle_t mutex,unsigned timeout)
{(void)timeout;assert(!pthread_mutex_lock(mutex));return 1;}
int xSemaphoreGive(SemaphoreHandle_t mutex) {assert(!pthread_mutex_unlock(mutex));return 1;}
int64_t esp_timer_get_time(void) {return now;}
void vTaskDelay(unsigned ticks)
{waited+=ticks;now+=(int64_t)ticks*1000;if(due && now>=due){due=0;timer.callback(timer.arg);}}
esp_err_t esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *handle)
{timer=*args;*handle=&timer;return ESP_OK;}
esp_err_t esp_timer_start_once(esp_timer_handle_t handle,uint64_t timeout)
{(void)handle;due=now+(int64_t)timeout;return ESP_OK;}
esp_err_t gpio_set_level(gpio_num_t pin,uint32_t level) {gpio[pin]=level;return ESP_OK;}
esp_err_t gpio_config(const gpio_config_t *config)
{(void)config;assert(!gpio[10] && !gpio[3]);return ESP_OK;}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *handle)
{assert(!strcmp(name,"keyvalet_wifi") && mode==NVS_READWRITE);*handle=1;return ESP_OK;}
esp_err_t nvs_get_blob(nvs_handle_t handle,const char *key,void *data,size_t *length)
{(void)handle;(void)key;(void)data;(void)length;return ESP_ERR_NVS_NOT_FOUND;}
esp_err_t nvs_set_blob(nvs_handle_t handle,const char *key,const void *data,size_t length)
{(void)data;assert(handle==1 && !strcmp(key,"station") && length==112 && !gpio[10] && !gpio[3]);return ESP_OK;}
static void *request_pulse(void *unused)
{
    (void)unused;atomic_store(&competitor_started,true);
    assert(keyvalet_lock_pulse(&profile,competing_lock)!=ESP_OK);
    atomic_store(&competitor_finished,true);return NULL;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle==1 && !gpio[10] && !gpio[3]);
    atomic_store(&competitor_started,false);atomic_store(&competitor_finished,false);
    assert(!pthread_create(&competitor,NULL,request_pulse,NULL));
    while(!atomic_load(&competitor_started)){}
    const struct timespec delay={.tv_nsec=1000000};nanosleep(&delay,NULL);
    assert(!atomic_load(&competitor_finished));return ESP_OK;
}
uint32_t esp_crc32_le(uint32_t initial,const uint8_t *data,uint32_t length)
{uint32_t crc=~initial;while(length--){crc^=*data++;for(unsigned i=0;i<8;++i)crc=(crc>>1)^(0xedb88320U&(0U-(crc&1U)));}return ~crc;}
int main(void)
{
    gpio[3]=gpio[10]=1;
    assert(keyvalet_hardware_io_init(&profile)==ESP_OK);
    for(unsigned lock=0;lock<2;++lock){
        assert(keyvalet_lock_maintenance_begin()==ESP_OK);
        assert(keyvalet_lock_pulse(&profile,lock)!=ESP_OK);
        assert(keyvalet_lock_reserve(&profile,lock)!=ESP_OK);
        keyvalet_lock_maintenance_end();
        assert(keyvalet_lock_reserve(&profile,lock)==ESP_OK);
        assert(keyvalet_lock_maintenance_begin()!=ESP_OK);
        assert(keyvalet_lock_cancel_reservation(&profile,lock)==ESP_OK);
        assert(keyvalet_lock_pulse(&profile,lock)==ESP_OK);
        assert(keyvalet_lock_maintenance_begin()!=ESP_OK);
        vTaskDelay(350);
        assert(keyvalet_lock_maintenance_begin()==ESP_OK);
        assert(!gpio[3] && !gpio[10]);
        keyvalet_lock_maintenance_end();vTaskDelay(2000);
    }
    keyvalet_wifi_credentials_t credentials;
    assert(keyvalet_wifi_store_load(&credentials)==ESP_ERR_NVS_NOT_FOUND);
    credentials=(keyvalet_wifi_credentials_t){.ssid="test-network",.password="test-password"};
    for(unsigned lock=0;lock<2;++lock){
        vTaskDelay(2350);competing_lock=1-lock;
        assert(keyvalet_lock_pulse(&profile,lock)==ESP_OK);
        const unsigned before=waited;
        assert(keyvalet_wifi_store_save(&credentials)==ESP_OK);
        assert(waited-before==350 && !gpio[3] && !gpio[10]);
        assert(!pthread_join(competitor,NULL) && atomic_load(&competitor_finished));
    }
    puts("Flash/transport gate passed: both channels, ACTIVE/RESERVED exclusion, maintenance inhibition, 350ms pulses, competing writes/actuation");
}
