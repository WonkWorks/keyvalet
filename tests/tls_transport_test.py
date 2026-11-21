#!/usr/bin/env python3
"""Execute actual admission/client-cleanup and vendor allocation-error functions.

Host simulation, not ESP32 TLS heap/capacity evidence. No sockets are opened.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def section(source, begin, end):
    offset = source.index(begin)
    return source[offset:source.index(end, offset)]


def check(code, name, temporary):
    source = temporary / (name + ".c")
    source.write_text(code)
    executable = temporary / name
    subprocess.run(["cc", "-std=c11", "-D_GNU_SOURCE", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-parameter", "-Wno-unused-variable", "-I" + str(ROOT / "tests/host_stubs"),
                    str(source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)


def main():
    web = (ROOT / "main/web_server.c").read_text()
    vendor = (ROOT / "components/esp_https_server/src/https_server.c").read_text()
    assert "httpd_start(" not in web and "httpd_stop(" not in web
    assert "HTTPD_SSL_CONFIG_DEFAULT()" in web and "httpd_ssl_start(" in web
    assert "config.httpd.max_open_sockets = KEYVALET_WEBSOCKET_MAX_CLIENTS + 1" in web
    assert "config.httpd.lru_purge_enable = false" in web
    assert "config.tls_handshake_timeout_ms = 3000" in web
    assert ".handle_ws_control_frames = true" in web
    assert "httpd_sess_trigger_close(s_server, clients[i])" in web
    assert "keyvalet_lock_maintenance_begin()" in web
    declarations = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#define KEYVALET_WEBSOCKET_MAX_CLIENTS 4U
#define KEYVALET_TLS_ORIGIN_MAX 192U
#define portMAX_DELAY 0
typedef void *httpd_handle_t;
typedef struct {const char *origin;} httpd_req_t;
typedef struct {bool ready;} keyvalet_tls_status_t;
static bool ready=true;
static void keyvalet_tls_status_get(keyvalet_tls_status_t *status){status->ready=ready;}
static const char *TAG="test";
static void *s_client_lock=(void*)1;
static int s_client_fds[4];
static int64_t s_last_rx[4];
static size_t s_client_count, indicated;
static int64_t clock_us;
static unsigned closed;
static int xSemaphoreTake(void *lock,unsigned timeout){return 1;}
static int xSemaphoreGive(void *lock){return 1;}
static int64_t esp_timer_get_time(void){return clock_us;}
static void keyvalet_connectivity_indicator_set_clients(size_t count){indicated=count;}
static int close(int fd){++closed;return 0;}
static size_t httpd_req_get_hdr_value_len(httpd_req_t *r,const char *header){return r->origin?strlen(r->origin):0;}
static esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r,const char *header,char *out,size_t size){snprintf(out,size,"%s",r->origin);return ESP_OK;}
static const char stored_origin[]="https://staff.example.com";
static const char *response_origin;
static esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *name,const char *value){if(!strcmp(name,"Access-Control-Allow-Origin"))response_origin=value;return ESP_OK;}
static const char *keyvalet_tls_allowed_origin(const char *origin){return !strcmp(origin,stored_origin)?stored_origin:NULL;}
typedef struct {bool final;int type;size_t len;} httpd_ws_frame_t;
#define HTTPD_WS_TYPE_PING 9
#define HTTPD_WS_CLIENT_HTTP 1
static void *s_server=(void*)1;
static unsigned pings;
static int httpd_ws_send_frame_async(void *server,int fd,httpd_ws_frame_t *frame){assert(frame->type==9 && !frame->len);++pings;return ESP_OK;}
static int httpd_ws_get_fd_info(void *server,int fd){return fd==200?HTTPD_WS_CLIENT_HTTP:2;}
static int httpd_get_client_list(void *server,size_t *count,int *fds){assert(*count>=s_client_count+1);memcpy(fds,s_client_fds,s_client_count*sizeof(*fds));fds[s_client_count]=200;*count=s_client_count+1;return ESP_OK;}
static int httpd_sess_trigger_close(void *server,int fd);
'''
    clients = section(web, "static size_t client_count_locked", "static int build_status_json")
    cors = section(web, "static esp_err_t set_console_cors(httpd_req_t *request)\n{", "static esp_err_t config_get_handler")
    admission = section(web, "static esp_err_t websocket_pre_handshake", "static esp_err_t websocket_post_handshake")
    closed_function = section(web, "static void session_closed", "static esp_err_t set_console_cors(httpd_req_t *request)\n{")
    liveness = section(web, "static void websocket_liveness", "static void web_worker_task")
    with tempfile.TemporaryDirectory(prefix="keyvalet-transport-test-") as temporary:
        temporary = Path(temporary)
        check(declarations + clients + cors + admission + closed_function + r'''
static int httpd_sess_trigger_close(void *server,int fd){session_closed(server,fd);return ESP_OK;}
''' + liveness + r'''
int main(void){
    httpd_req_t request={.origin="https://staff.example.com"};
    for(int i=0;i<4;++i){assert(websocket_pre_handshake(&request)==ESP_OK);assert(add_client(10+i));}
    assert(indicated==4 && keyvalet_web_server_client_count()==4);
    assert(add_client(10) && !add_client(99));
    assert(websocket_pre_handshake(&request)!=ESP_OK);
    session_closed(NULL,11);assert(indicated==3 && closed==1);
    assert(websocket_pre_handshake(&request)==ESP_OK && add_client(99));
    session_closed(NULL,10);session_closed(NULL,12);session_closed(NULL,13);session_closed(NULL,99);
    assert(indicated==0 && keyvalet_web_server_client_count()==0);
    const char *bad[]={NULL,"null","https://evil.example.com","https://staff.example.com.evil","http://staff.example.com"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i){request.origin=bad[i];assert(set_console_cors(&request)!=ESP_OK);assert(websocket_pre_handshake(&request)!=ESP_OK);}
    request.origin="https://staff.example.com";assert(set_console_cors(&request)==ESP_OK);
    assert(response_origin==stored_origin); /* HTTPD retains pointers until send. */
    ready=false;assert(set_console_cors(&request)!=ESP_OK && websocket_pre_handshake(&request)!=ESP_OK);
    for(int i=0;i<4;++i)assert(add_client(10+i));
    clock_us=15000000;websocket_liveness(NULL);assert(pings==4 && s_client_count==4);
    clock_us=61000000;s_last_rx[0]=clock_us;websocket_liveness(NULL);assert(s_client_count==1 && indicated==1);
    clock_us=122000000;websocket_liveness(NULL);assert(s_client_count==0 && indicated==0);
    puts("PASS: actual HTTPS/WSS Origins/expiry, fixed four-client admission, cleanup/re-admission/LED counts, PING/idle/dead-session reclamation");
}
''', "admission", temporary)
        allocation = section(vendor, "static esp_err_t httpd_ssl_open", "/**\n * Free a global SSL context") if "/**\n * Free a global SSL context" in vendor else section(vendor, "static esp_err_t httpd_ssl_open", "static void free_secure_context")
        check(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_err.h"
#include "esp_log.h"
#define ESP_LOGD(...) ((void)0)
typedef void *httpd_handle_t;
typedef struct {int dummy;} esp_tls_t;
typedef void *esp_tls_error_handle_t;
typedef struct {int last_error,esp_tls_error_code,esp_tls_flags;} esp_https_server_last_error_t;
typedef struct {int user_cb_state;esp_tls_t *tls;} esp_https_server_user_cb_arg_t;
enum {HTTPS_SERVER_EVENT_ERROR,HTTPD_SSL_USER_CB_SESS_CREATE,HTTPS_SERVER_EVENT_ON_CONNECTED};
typedef struct {void *tls_cfg;void (*open_fn)(httpd_handle_t,int);void (*user_cb)(esp_https_server_user_cb_arg_t*);} httpd_ssl_ctx_t;
typedef struct {esp_tls_t *tls;httpd_ssl_ctx_t *global_ctx;} httpd_ssl_transport_ctx_t;
static const char *TAG="test";
static httpd_ssl_ctx_t global;
static esp_tls_t tls;
static unsigned deleted;
static int fail_allocation,fail_handshake;
static void *stored;
static void *httpd_get_global_transport_ctx(httpd_handle_t server){return &global;}
static esp_tls_t *esp_tls_init(void){return &tls;}
static void http_dispatch_event_to_event_loop(int id,void *data,size_t length){}
static int esp_tls_server_session_create(void *config,int fd,esp_tls_t *session){return fail_handshake?-1:0;}
static void esp_tls_server_session_delete(esp_tls_t *session){assert(session==&tls);++deleted;}
static void *allocate(size_t count,size_t size){return fail_allocation?NULL:calloc(count,size);}
#define calloc allocate
static void httpd_ssl_close(void *context){}
static int httpd_ssl_send,httpd_ssl_recv,httpd_ssl_pending;
static void httpd_sess_set_transport_ctx(httpd_handle_t server,int fd,void *context,void (*release)(void*)){stored=context;}
static void httpd_sess_set_send_override(httpd_handle_t server,int fd,int callback){}
static void httpd_sess_set_recv_override(httpd_handle_t server,int fd,int callback){}
static void httpd_sess_set_pending_override(httpd_handle_t server,int fd,int callback){}
static esp_err_t esp_tls_get_error_handle(esp_tls_t *session,esp_tls_error_handle_t *handle){return ESP_FAIL;}
static int esp_tls_get_and_clear_last_error(esp_tls_error_handle_t handle,int *code,int *flags){return 0;}
''' + allocation + r'''
int main(void){
    fail_allocation=1;assert(httpd_ssl_open((void*)1,10)==ESP_ERR_NO_MEM && deleted==1 && !stored);
    fail_allocation=0;fail_handshake=1;assert(httpd_ssl_open((void*)1,10)!=ESP_OK && deleted==2 && !stored);
    fail_handshake=0;assert(httpd_ssl_open((void*)1,10)==ESP_OK && deleted==2 && stored);free(stored);
    puts("PASS: actual IDF secure-open success, handshake failure and post-handshake allocation-failure cleanup");
}
''', "vendor_cleanup", temporary)
        clock_source = (ROOT / "main/time_sync.c").read_text()
        clock_set = clock_source[clock_source.index("esp_err_t keyvalet_time_set_usb_utc"):]
        check(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/time.h>
#include "esp_err.h"
#define KEYVALET_EARLIEST_VALID_UTC 1767225600LL
static atomic_bool s_time_valid;
static int failure;
static unsigned writes;
static int set_clock(const struct timeval *time,void *zone){assert(time->tv_sec>=1767225600 && time->tv_usec==0);++writes;return failure;}
#define settimeofday set_clock
''' + clock_set + r'''
int main(void){
    assert(keyvalet_time_set_usb_utc(0)!=ESP_OK && !s_time_valid && !writes);
    assert(keyvalet_time_set_usb_utc(4102444801LL)!=ESP_OK && !writes);
    failure=1;assert(keyvalet_time_set_usb_utc(1800000000)!=ESP_OK && !s_time_valid);
    failure=0;assert(keyvalet_time_set_usb_utc(1800000000)==ESP_OK && s_time_valid && writes==2);
    puts("PASS: actual USB UTC range/failure/validity behavior; no TLS expiry bypass");
}
''', "usb_clock", temporary)


if __name__ == "__main__":
    main()
