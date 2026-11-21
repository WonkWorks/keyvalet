#!/usr/bin/env python3
"""Compile the actual record serializer with IDF cJSON and fail every allocation."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
if not os.environ.get("IDF_PATH"):
    raise SystemExit("Set IDF_PATH to the ESP-IDF checkout (source its export.sh).")
idf = Path(os.environ["IDF_PATH"])
cjson = idf / "components/json/cJSON"
for required in (cjson / "cJSON.c", cjson / "cJSON.h"):
    if not required.is_file():
        raise SystemExit(f"Missing SDK dependency: {required}; check IDF_PATH and SDK submodules.")
if not shutil.which("cc"):
    raise SystemExit("Missing host C compiler: cc must be on PATH.")
source = (ROOT / "main/web_server.c").read_text()
function = source[source.index("static bool add_journal_record_json"):
                  source.index("static esp_err_t handle_journal_get")]
program = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include "cJSON.h"
#include "journal_format.h"
static const char *keyvalet_network_device_id(void) {return "keyvalet-test";}
static unsigned fail_at, allocations, live;
static void *test_malloc(size_t size) {
    if (++allocations == fail_at) return NULL;
    void *result = malloc(size);
    if (result) ++live;
    return result;
}
static void test_free(void *pointer) {
    if (pointer) {assert(live > 0); --live; free(pointer);}
}
''' + function + r'''
int main(void) {
    const cJSON_Hooks hooks = {.malloc_fn = test_malloc, .free_fn = test_free};
    cJSON_InitHooks((cJSON_Hooks *)&hooks);
    for (unsigned utc = 0; utc < 2; ++utc) {
        for (unsigned failure = 1; failure < 64; ++failure) {
            fail_at = 0; allocations = 0;
            cJSON *records = cJSON_CreateArray();
            assert(records);
            fail_at = failure; allocations = 0;
            const keyvalet_journal_record_t record = {
                .sequence = 42, .type = KEYVALET_EVENT_SENSOR_OPEN, .detail = 2,
                .flags = utc ? KEYVALET_EVENT_FLAG_WALL_TIME_VALID : 0,
            };
            const bool complete = add_journal_record_json(records, &record);
            if (complete) {
                assert(cJSON_GetArraySize(records) == 1);
                cJSON *item = cJSON_GetArrayItem(records, 0);
                const char *fields[] = {"device_id", "sequence", "event_id", "event",
                    "uptime_ms", "utc_s", "flags", "detail", "metadata_hex"};
                for (unsigned i = 0; i < sizeof(fields)/sizeof(fields[0]); ++i)
                    assert(cJSON_GetObjectItemCaseSensitive(item, fields[i]));
            } else assert(cJSON_GetArraySize(records) == 0);
            cJSON_Delete(records);
            assert(live == 0);
        }
    }
    puts("journal JSON allocation tests passed: no incomplete event or leaked object");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="keyvalet-json-test-") as directory:
    binary = str(Path(directory) / "test")
    subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(ROOT / "main"), "-I" + str(cjson),
                    "-x", "c", "-", str(cjson / "cJSON.c"),
                    str(ROOT / "main/journal_format.c"), "-o", binary],
                   input=program, text=True, check=True)
    subprocess.run([binary], check=True)
