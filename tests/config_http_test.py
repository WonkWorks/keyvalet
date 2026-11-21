#!/usr/bin/env python3
"""Exercise the actual HTTP config handlers with IDF cJSON and stubbed services.

Crypto and real NVS are covered separately. Here authentication denial, payload
binding, complete booleans, CAS, and no pre-auth persistence are the boundaries.
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
for required in (CJSON / "cJSON.c", CJSON / "cJSON.h"):
    if not required.is_file():
        raise SystemExit(f"Missing SDK dependency: {required}; check IDF_PATH and SDK submodules.")
if not shutil.which("cc"):
    raise SystemExit("Missing host C compiler: cc must be on PATH.")
source = (ROOT / "main/web_server.c").read_text()
parsers = source[source.index("static const char *json_string"):
                 source.index("static bool json_u64")]
authorization = source[source.index("static bool parse_authorization"):
                       source.index("static esp_err_t handle_enroll_begin")]
handlers = source[source.index("static esp_err_t config_get_handler"):
                  source.index("static esp_err_t config_options_handler")]
program = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "settings.h"
#include "security.h"
#include "journal.h"
#include "device_controller.h"
#define REQUEST_BUFFER_SIZE 2048U
#define HTTPD_403_FORBIDDEN "403"
#define HTTPD_400_BAD_REQUEST "400"
#define HTTPD_500_INTERNAL_SERVER_ERROR "500"
typedef struct {size_t content_len; const char *body; size_t offset; char response[2048];} httpd_req_t;
static const char *TAG = "test";
static bool deny;
static unsigned authenticated, persisted, applied, journaled;
static char canonical[768];
static keyvalet_settings_t stored = {.schema_version=KEYVALET_SETTINGS_SCHEMA_VERSION,
    .revision=1, .default_door_open_timeout_s=90, .lock_sensor_required={true,true}};
static esp_err_t set_console_cors(httpd_req_t *request) {(void)request;return ESP_OK;}
static esp_err_t httpd_resp_send(httpd_req_t *request,const char *body,int length) {
    assert(length>=0 && (size_t)length<sizeof(request->response));
    memcpy(request->response,body,(size_t)length);request->response[length]=0;return ESP_OK;
}
static esp_err_t httpd_resp_send_err(httpd_req_t *request,const char *code,const char *message)
{(void)code;return httpd_resp_send(request,message,(int)strlen(message));}
static void httpd_resp_set_type(httpd_req_t *request,const char *type) {(void)request;(void)type;}
static void httpd_resp_set_hdr(httpd_req_t *request,const char *key,const char *value)
{(void)request;(void)key;(void)value;}
static void httpd_resp_set_status(httpd_req_t *request,const char *status) {(void)request;(void)status;}
static int httpd_req_recv(httpd_req_t *request,char *body,size_t length) {
    assert(request->offset+length<=(size_t)request->content_len);
    memcpy(body,request->body+request->offset,length);request->offset+=length;return (int)length;
}
esp_err_t keyvalet_security_authorize_and_consume(keyvalet_auth_action_t action,
    const keyvalet_authorization_t *auth,const keyvalet_config_v3_payload_t *payload,
    const keyvalet_unlock_authorization_payload_t *unlock,const keyvalet_journal_ack_payload_t *ack)
{
    assert(action==KEYVALET_AUTH_UPDATE_CONFIG && payload && !unlock && !ack);
    assert(!strcmp(payload->operation_id,auth->operation_id));
    assert(payload->issued_at==auth->issued_at && payload->expires_at==auth->expires_at);
    assert(keyvalet_config_v3_canonical(canonical,sizeof(canonical),payload)>0);
    ++authenticated;return deny?ESP_FAIL:ESP_OK;
}
esp_err_t keyvalet_settings_get(keyvalet_settings_t *settings) {*settings=stored;return ESP_OK;}
esp_err_t keyvalet_settings_update(const keyvalet_settings_t *settings,uint32_t revision) {
    assert(authenticated>persisted);
    if(!keyvalet_settings_values_valid(settings->schema_version,settings->default_door_open_timeout_s))
        return ESP_ERR_INVALID_ARG;
    if(!keyvalet_settings_revision_matches(stored.revision,revision))return ESP_ERR_INVALID_STATE;
    stored=*settings;stored.revision=revision+1;++persisted;return ESP_OK;
}
esp_err_t keyvalet_device_controller_apply_settings(void) {assert(persisted>applied);++applied;return ESP_OK;}
esp_err_t keyvalet_journal_append(keyvalet_event_type_t type,uint16_t flags,uint32_t detail)
{assert(type==KEYVALET_EVENT_CONFIG_UPDATED && !flags && detail==stored.revision);++journaled;return ESP_OK;}
''' + parsers + authorization + handlers + r'''
static cJSON *valid(unsigned mask) {
    char body[1024];
    snprintf(body,sizeof(body),"{\"protocol_version\":3,\"device_id\":\"keyvalet-test\","
        "\"action\":\"UPDATE_CONFIG\",\"operation_id\":\"test-op\",\"nonce\":\"00\","
        "\"signature\":\"test-only\",\"issued_at\":100,\"expires_at\":160,"
        "\"expected_revision\":%u,\"default_door_open_timeout_s\":90,"
        "\"locks\":{\"lock_1\":{\"sensor_required\":%s},\"lock_2\":{\"sensor_required\":%s}},"
        "\"sensors\":{\"sensor_1\":{\"monitoring_enabled\":false},"
        "\"sensor_2\":{\"monitoring_enabled\":false},\"sensor_3\":{\"monitoring_enabled\":false},"
        "\"sensor_4\":{\"monitoring_enabled\":false}}}",stored.revision,
        (mask&1)?"true":"false",(mask&2)?"true":"false");
    cJSON *root=cJSON_Parse(body);assert(root);return root;
}
static void submit(cJSON *root,bool accepted) {
    char *body=cJSON_PrintUnformatted(root);assert(body);
    httpd_req_t request={.body=body,.content_len=(int)strlen(body)};
    assert(config_patch_handler(&request)==ESP_OK);
    cJSON *response=cJSON_Parse(request.response);assert(response);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(response,"accepted"))==accepted);
    cJSON_Delete(response);free(body);cJSON_Delete(root);
}
int main(void) {
    for(unsigned mask=0;mask<4;++mask) {
        submit(valid(mask),true);
        assert(stored.lock_sensor_required[0]==((mask&1)!=0));
        assert(stored.lock_sensor_required[1]==((mask&2)!=0));
        assert(persisted==applied && persisted==journaled);
        assert(strstr(canonical,"KEYVALET-CONFIG-V3\n"));
        httpd_req_t get={0};assert(config_get_handler(&get)==ESP_OK);
        cJSON *response=cJSON_Parse(get.response);assert(response);
        assert(cJSON_GetObjectItemCaseSensitive(response,"protocol_version")->valueint==3);
        assert(cJSON_GetObjectItemCaseSensitive(response,"schema_version")->valueint==4);
        cJSON *locks=cJSON_GetObjectItemCaseSensitive(response,"locks");
        for(unsigned lock=0;lock<2;++lock) {
            const char *id=lock?"lock_2":"lock_1";
            cJSON *entry=cJSON_GetObjectItemCaseSensitive(locks,id);
            assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(entry,"sensor_required"))==
                   stored.lock_sensor_required[lock]);
        }
        cJSON_Delete(response);
    }
    const unsigned saved_auth=authenticated,saved_persist=persisted;
    cJSON *root=valid(0);cJSON_DeleteItemFromObjectCaseSensitive(root,"locks");submit(root,false);
    root=valid(0);cJSON *locks=cJSON_GetObjectItemCaseSensitive(root,"locks");
    cJSON_DeleteItemFromObjectCaseSensitive(locks,"lock_2");submit(root,false);
    root=valid(0);locks=cJSON_GetObjectItemCaseSensitive(root,"locks");
    cJSON *entry=cJSON_GetObjectItemCaseSensitive(locks,"lock_1");
    cJSON_ReplaceItemInObjectCaseSensitive(entry,"sensor_required",cJSON_CreateNumber(0));submit(root,false);
    root=valid(0);cJSON_ReplaceItemInObjectCaseSensitive(root,"protocol_version",cJSON_CreateNumber(2));submit(root,false);
    assert(authenticated==saved_auth && persisted==saved_persist);
    deny=true;submit(valid(0),false);deny=false;
    assert(persisted==saved_persist && stored.lock_sensor_required[0]);
    root=valid(0);cJSON_ReplaceItemInObjectCaseSensitive(root,"expected_revision",cJSON_CreateNumber(1));
    submit(root,false);assert(persisted==saved_persist);
    assert(applied==saved_persist && journaled==saved_persist);
    puts("HTTP configuration tests passed: real parsing/serialization, flags, auth denial and CAS ordering");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="keyvalet-config-http-") as directory:
    binary = str(Path(directory) / "test")
    subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(ROOT / "tests/host_stubs"), "-I" + str(ROOT / "main"),
                    "-I" + str(CJSON), "-x", "c", "-", str(CJSON / "cJSON.c"),
                    str(ROOT / "main/config_contract.c"), str(ROOT / "main/settings_schema.c"),
                    str(ROOT / "main/unlock_contract.c"), "-o", binary],
                   input=program, text=True, check=True)
    subprocess.run([binary], check=True)
