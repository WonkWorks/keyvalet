#!/usr/bin/env python3
"""Run the actual WSS handler against installed IDF frame parsing and cJSON.

The byte-stream transport models TLS reads without opening sockets. In
particular, an empty frame must not initiate a second header receive.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
if not os.environ.get("IDF_PATH"):
    raise SystemExit("Set IDF_PATH to the ESP-IDF checkout (source its export.sh).")
IDF = Path(os.environ["IDF_PATH"])
CJSON = IDF / "components/json/cJSON"
for required in (CJSON / "cJSON.c", CJSON / "cJSON.h",
                 IDF / "components/esp_http_server/src/httpd_ws.c"):
    if not required.is_file():
        raise SystemExit(f"Missing SDK dependency: {required}; check IDF_PATH and SDK submodules.")
if not shutil.which("cc"):
    raise SystemExit("Missing host C compiler: cc must be on PATH.")


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def main():
    web = (ROOT / "main/web_server.c").read_text()
    ws = (IDF / "components/esp_http_server/src/httpd_ws.c").read_text()
    receiver = section(ws, "static esp_err_t httpd_ws_check_req", "esp_err_t httpd_ws_send_frame(")
    frame_type = section(ws, "esp_err_t httpd_ws_get_frame_type(", "httpd_ws_client_info_t httpd_ws_get_fd_info")
    handler = section(web, "static esp_err_t websocket_handler", "static void session_closed")
    program = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "cJSON.h"
#define ESP_LOGD(...) ((void)0)
#define LOG_FMT(s) s
#define NEWLIB_NANO_COMPAT_FORMAT "zu"
#define NEWLIB_NANO_COMPAT_CAST(n) (n)
#define HTTPD_WS_FIN_BIT 0x80U
#define HTTPD_WS_OPCODE_BITS 0x0fU
#define HTTPD_WS_MASK_BIT 0x80U
#define HTTPD_WS_LENGTH_BITS 0x7fU
#define HTTPD_RECV_OPT_BLOCKING 1
#define HTTPD_RECV_OPT_NONE 0
#define REQUEST_BUFFER_SIZE 2048U
#define portMAX_DELAY 0
enum {HTTPD_WS_TYPE_TEXT=1, HTTPD_WS_TYPE_BINARY=2, HTTPD_WS_TYPE_CLOSE=8,
      HTTPD_WS_TYPE_PING=9, HTTPD_WS_TYPE_PONG=10};
typedef struct {bool final, fragmented; int type; uint8_t *payload; size_t len;} httpd_ws_frame_t;
struct sock_db {bool ws_handshake_done, ws_control_frames;};
struct httpd_req_aux {struct sock_db *sd; int ws_type; bool ws_final; uint8_t mask_key[4];};
typedef struct {struct httpd_req_aux *aux;} httpd_req_t;
typedef struct {bool ready;} keyvalet_tls_status_t;
static const char *TAG="test";
static uint8_t wire[8192], reply[2048];
static size_t available, consumed, reply_length, fail_at;
static unsigned overreads, sends, allocations, outstanding, dispatched, malformed, readonly;
static int reply_type, send_result, operation, request_fd=12;
static bool ready=true, fail_allocate;
static int64_t clock_us=15000000, s_last_rx[4];
static size_t s_client_count=4;
static int s_client_fds[]={10,11,12,13}, s_client_lock;
static atomic_uint s_http_stack_free;
static void keyvalet_tls_status_get(keyvalet_tls_status_t *status){status->ready=ready;}
static int xSemaphoreTake(int lock,int timeout){return 1;}
static int xSemaphoreGive(int lock){return 1;}
static int httpd_req_to_sockfd(httpd_req_t *request){return request_fd;}
static int64_t esp_timer_get_time(void){return clock_us;}
static unsigned uxTaskGetStackHighWaterMark(void *task){return 4096;}
static int httpd_recv_with_opt(httpd_req_t *request,char *out,size_t length,int flags){
    if(consumed>=available || consumed>=fail_at){++overreads;return -1;}
    size_t count=length;
    if(count>available-consumed)count=available-consumed;
    if(count>fail_at-consumed)count=fail_at-consumed;
    if(flags==HTTPD_RECV_OPT_NONE && count>3)count=3; /* partial payload reads */
    memcpy(out,wire+consumed,count);consumed+=count;return (int)count;
}
static esp_err_t httpd_ws_send_frame(httpd_req_t *request,httpd_ws_frame_t *frame){
    ++sends;reply_type=frame->type;reply_length=frame->len;
    assert(reply_length<=sizeof(reply));
    if(reply_length)memcpy(reply,frame->payload,reply_length);
    return send_result;
}
static void *request_allocate(size_t size){
    ++allocations;if(fail_allocate)return NULL;
    void *payload=malloc(size);assert(payload);++outstanding;return payload;
}
static void request_free(void *payload){assert(payload && outstanding);--outstanding;free(payload);}
static esp_err_t send_read_only_error(httpd_req_t *request){++readonly;return ESP_OK;}
static esp_err_t ws_result(httpd_req_t *request,const char *id,bool accepted,const char *reason){
    assert(!strcmp(reason,"MALFORMED_JSON"));++malformed;return ESP_OK;
}
static const char *json_string(cJSON *root,const char *name){
    cJSON *value=cJSON_GetObjectItemCaseSensitive(root,name);
    return cJSON_IsString(value)?value->valuestring:NULL;
}
static esp_err_t handle_enroll_begin(httpd_req_t *request){operation=1;++dispatched;return ESP_OK;}
static esp_err_t handle_enroll_complete(httpd_req_t *request,cJSON *root){operation=2;++dispatched;return ESP_OK;}
static esp_err_t handle_targeted_unlock(httpd_req_t *request,cJSON *root){operation=3;++dispatched;return ESP_OK;}
static esp_err_t handle_journal_get(httpd_req_t *request,cJSON *root){operation=4;++dispatched;return ESP_OK;}
static esp_err_t handle_journal_ack(httpd_req_t *request,cJSON *root){operation=5;++dispatched;return ESP_OK;}
''' + receiver + frame_type + r'''
#define malloc request_allocate
#define free request_free
''' + handler + r'''
#undef malloc
#undef free
static struct sock_db session={.ws_handshake_done=true,.ws_control_frames=true};
static struct httpd_req_aux aux={.sd=&session};
static httpd_req_t request={.aux=&aux};
static void reset(void){
    assert(!outstanding);
    available=consumed=0;fail_at=SIZE_MAX;overreads=sends=allocations=0;
    dispatched=malformed=readonly=0;send_result=ESP_OK;fail_allocate=false;ready=true;
    request_fd=12;
    memset(s_last_rx,0,sizeof(s_last_rx));
}
static void append(int type,const void *payload,size_t length){
    const uint8_t mask[]={0x12,0x34,0x56,0x78};
    assert(length<65536 && available+length+8<sizeof(wire));
    wire[available++]=0x80|type;
    if(length<126)wire[available++]=0x80|length;
    else {wire[available++]=0xfe;wire[available++]=length>>8;wire[available++]=length;}
    memcpy(wire+available,mask,4);available+=4;
    for(size_t i=0;i<length;++i)wire[available++]=((const uint8_t*)payload)[i]^mask[i%4];
}
static esp_err_t receive(void){
    /* Same first-byte dispatch as httpd_req_new with handle_ws_control_frames. */
    assert(httpd_ws_get_frame_type(&request)==ESP_OK);
    esp_err_t result=websocket_handler(&request);
    assert(!outstanding);return result;
}
int main(void){
    /* The browser's automatic response to the server's empty heartbeat PING. */
    reset();append(HTTPD_WS_TYPE_PONG,NULL,0);
    assert(receive()==ESP_OK);assert(consumed==available && !overreads && !sends);
    assert(s_last_rx[2]==clock_us && !s_last_rx[0] && !s_last_rx[1] && !s_last_rx[3]);
    /* Empty PING must respond without attempting another header read. */
    reset();append(HTTPD_WS_TYPE_PING,NULL,0);
    assert(receive()==ESP_OK && !overreads && sends==1 && reply_type==HTTPD_WS_TYPE_PONG && !reply_length);
    /* A following frame must stay aligned, including when already buffered by TLS. */
    const char text[]="{\"type\":\"unlock\"}";
    reset();append(HTTPD_WS_TYPE_PONG,NULL,0);size_t boundary=available;
    append(HTTPD_WS_TYPE_TEXT,text,strlen(text));
    assert(receive()==ESP_OK && consumed==boundary && !dispatched);
    assert(receive()==ESP_OK && consumed==available && operation==3 && dispatched==1 && !overreads);
    /* Hundreds of heartbeat cycles for each of the four client slots. */
    for(unsigned i=0;i<400;++i){
        reset();request_fd=10+i%4;
        append(HTTPD_WS_TYPE_PONG,NULL,0);assert(receive()==ESP_OK && !overreads);
        for(unsigned slot=0;slot<4;++slot)assert(s_last_rx[slot]==(slot==i%4?clock_us:0));
    }
    const uint8_t nonce[]={0,1,2,0xfe,0xff};
    reset();append(HTTPD_WS_TYPE_PING,nonce,sizeof(nonce));
    assert(receive()==ESP_OK && sends==1 && reply_type==HTTPD_WS_TYPE_PONG);
    assert(reply_length==sizeof(nonce) && !memcmp(reply,nonce,sizeof(nonce)) && !overreads);
    reset();append(HTTPD_WS_TYPE_PONG,nonce,sizeof(nonce));assert(receive()==ESP_OK && !sends && !overreads);
    const char *types[]={"enroll_begin","enroll_complete","unlock","journal_get","journal_ack"};
    for(unsigned i=0;i<5;++i){
        char json[64];snprintf(json,sizeof(json),"{\"type\":\"%s\"}",types[i]);
        reset();append(HTTPD_WS_TYPE_TEXT,json,strlen(json));
        assert(receive()==ESP_OK && dispatched==1 && operation==(int)i+1 && !overreads);
    }
    reset();append(HTTPD_WS_TYPE_TEXT,NULL,0);assert(receive()==ESP_OK && malformed==1 && !overreads);
    reset();append(HTTPD_WS_TYPE_TEXT,"{",1);assert(receive()==ESP_OK && malformed==1 && !overreads);
    reset();append(HTTPD_WS_TYPE_BINARY,nonce,sizeof(nonce));assert(receive()==ESP_OK && readonly==1 && !overreads);
    reset();append(HTTPD_WS_TYPE_CLOSE,NULL,0);assert(receive()!=ESP_OK && !overreads && consumed==available);
    const uint8_t close_code[]={3,232};
    reset();append(HTTPD_WS_TYPE_CLOSE,close_code,2);assert(receive()!=ESP_OK && !overreads && consumed==available);
    reset();append(HTTPD_WS_TYPE_PING,NULL,0);send_result=ESP_FAIL;assert(receive()==ESP_FAIL && !overreads);
    reset();append(HTTPD_WS_TYPE_TEXT,text,strlen(text));fail_allocate=true;
    assert(receive()==ESP_ERR_NO_MEM && !dispatched && !overreads);
    char large[REQUEST_BUFFER_SIZE];memset(large,'x',sizeof(large));
    reset();append(HTTPD_WS_TYPE_TEXT,large,sizeof(large));assert(receive()!=ESP_OK && !allocations && !dispatched);
    reset();append(HTTPD_WS_TYPE_TEXT,large,sizeof(large)-1);assert(receive()==ESP_OK && malformed==1 && !overreads);
    reset();append(HTTPD_WS_TYPE_TEXT,text,strlen(text));fail_at=2;
    assert(receive()!=ESP_OK && !allocations && !dispatched);
    reset();append(HTTPD_WS_TYPE_TEXT,text,strlen(text));fail_at=8;
    assert(receive()!=ESP_OK && allocations==1 && !dispatched);
    reset();append(HTTPD_WS_TYPE_TEXT,text,strlen(text));ready=false;
    assert(receive()!=ESP_OK && !allocations && !dispatched);
    puts("PASS: actual IDF receive parser + WSS handler: empty heartbeat/control frames, buffered frame alignment, repeated PONGs, payload echo/partial reads, unchanged dispatch, bounds/errors/ownership");
}
'''
    with tempfile.TemporaryDirectory(prefix="keyvalet-ws-receive-") as temporary:
        temporary = Path(temporary)
        source = temporary / "receive.c"
        source.write_text(program)
        executable = temporary / "receive"
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-parameter", "-Wno-unused-variable",
            "-I" + str(ROOT / "tests/host_stubs"), "-I" + str(CJSON),
            str(source), str(CJSON / "cJSON.c"), "-lm", "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True, cwd=temporary)


if __name__ == "__main__":
    main()
