#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"
#include "replay_record.h"
#if defined(CONFIG_IDF_TARGET_LINUX) && defined(CONFIG_ESP_PARTITION_ENABLE_STATS)
#include "esp_private/partition_linux.h"
#endif

#define ITERATIONS 10000U

static unsigned s_reinitializations;
static unsigned s_rollovers;
static size_t s_min_used = SIZE_MAX;
static size_t s_max_used;
static size_t s_min_available = SIZE_MAX;
static size_t s_max_available;

static void key_for(size_t slot, char key[8])
{
    snprintf(key, 8, "r%03u", (unsigned)slot);
}

static void make_digests(uint64_t sequence, uint8_t operation[16], uint8_t nonce[16])
{
    memset(operation, 0, 16);
    memset(nonce, 0, 16);
    memcpy(operation, &sequence, sizeof(sequence));
    sequence ^= UINT64_C(0xd00df00da5a55a5a);
    memcpy(nonce, &sequence, sizeof(sequence));
}

static void verify_ring(nvs_handle_t handle, uint64_t highest)
{
    keyvalet_replay_record_t records[KEYVALET_REPLAY_CAPACITY] = {0};
    bool present[KEYVALET_REPLAY_CAPACITY] = {0};
    for (size_t slot = 0; slot < KEYVALET_REPLAY_CAPACITY; ++slot) {
        char key[8];
        key_for(slot, key);
        size_t size = sizeof(records[slot]);
        const esp_err_t result = nvs_get_blob(handle, key, &records[slot], &size);
        if (result == ESP_ERR_NVS_NOT_FOUND) continue;
        assert(result == ESP_OK && size == sizeof(records[slot]));
        present[slot] = true;
    }
    keyvalet_replay_window_t window;
    uint64_t recovered = 0;
    assert(keyvalet_replay_records_recover(records, present, &window, &recovered));
    assert(recovered == highest);
    assert(window.count == (highest < KEYVALET_REPLAY_CAPACITY ?
                            highest : KEYVALET_REPLAY_CAPACITY));

    const uint64_t first = highest - window.count + 1U;
    for (uint64_t offset = 0; offset < window.count; ++offset) {
        uint8_t operation[16], nonce[16];
        make_digests(first + offset, operation, nonce);
        assert(memcmp(window.operation_digests[offset], operation,
                      sizeof(operation)) == 0);
        assert(memcmp(window.nonce_digests[offset], nonce, sizeof(nonce)) == 0);
    }

    uint8_t latest_operation[16], latest_nonce[16];
    uint8_t fresh_operation[16], fresh_nonce[16];
    make_digests(highest, latest_operation, latest_nonce);
    make_digests(highest + ITERATIONS + 1U, fresh_operation, fresh_nonce);
    assert(keyvalet_replay_window_contains(&window, latest_operation, fresh_nonce));
    assert(keyvalet_replay_window_contains(&window, fresh_operation, latest_nonce));
    assert(!keyvalet_replay_window_contains(&window, fresh_operation, fresh_nonce));

    if (highest > KEYVALET_REPLAY_CAPACITY) {
        uint8_t evicted_operation[16], evicted_nonce[16];
        uint8_t oldest_operation[16], oldest_nonce[16];
        make_digests(first - 1U, evicted_operation, evicted_nonce);
        make_digests(first, oldest_operation, oldest_nonce);
        assert(!keyvalet_replay_window_contains(&window, evicted_operation,
                                                evicted_nonce));
        assert(keyvalet_replay_window_contains(&window, oldest_operation,
                                               oldest_nonce));
    }
}

static nvs_stats_t print_stats(const char *checkpoint, uint64_t sequence)
{
    nvs_stats_t stats;
    assert(nvs_get_stats("replay_nvs", &stats) == ESP_OK);
    printf("STATS %s sequence=%" PRIu64 " used=%u free=%u available=%u total=%u\n",
           checkpoint, sequence, (unsigned)stats.used_entries,
           (unsigned)stats.free_entries, (unsigned)stats.available_entries,
           (unsigned)stats.total_entries);
    return stats;
}

static void record_rollover_stats(uint64_t sequence)
{
    nvs_stats_t stats;
    assert(nvs_get_stats("replay_nvs", &stats) == ESP_OK);
    if (stats.used_entries < s_min_used) s_min_used = stats.used_entries;
    if (stats.used_entries > s_max_used) s_max_used = stats.used_entries;
    if (stats.available_entries < s_min_available) {
        s_min_available = stats.available_entries;
    }
    if (stats.available_entries > s_max_available) {
        s_max_available = stats.available_entries;
    }
    ++s_rollovers;
    if (sequence == KEYVALET_REPLAY_CAPACITY || sequence == 4960U ||
        sequence == 9920U) {
        print_stats("rollover", sequence);
    }
}

void app_main(void)
{
    assert(nvs_flash_init_partition("replay_nvs") == ESP_OK);
    nvs_handle_t handle;
    assert(nvs_open_from_partition("replay_nvs", "replay", NVS_READWRITE,
                                   &handle) == ESP_OK);

    /* Match the production partition's fixed 12-byte initialization marker.
     * The storage integration test checks its semantic validation. */
    uint8_t metadata[12] = {0};
    assert(nvs_set_blob(handle, "meta", metadata, sizeof(metadata)) == ESP_OK);
    assert(nvs_commit(handle) == ESP_OK);
    print_stats("before-records", 0);
#if defined(CONFIG_IDF_TARGET_LINUX) && defined(CONFIG_ESP_PARTITION_ENABLE_STATS)
    esp_partition_clear_stats();
#endif

    for (uint64_t sequence = 1; sequence <= ITERATIONS; ++sequence) {
        uint8_t operation[16], nonce[16];
        make_digests(sequence, operation, nonce);
        keyvalet_replay_record_t record;
        keyvalet_replay_record_create(&record, sequence, (int64_t)sequence + 1000,
                                      operation, nonce);
        char key[8];
        key_for(keyvalet_replay_record_slot(sequence), key);
        assert(nvs_set_blob(handle, key, &record, sizeof(record)) == ESP_OK);
        assert(nvs_commit(handle) == ESP_OK);
        if (sequence % KEYVALET_REPLAY_CAPACITY == 0) {
            record_rollover_stats(sequence);
        }
        if (sequence % 137U == 0) {
            verify_ring(handle, sequence);
            nvs_close(handle);
            assert(nvs_flash_deinit_partition("replay_nvs") == ESP_OK);
            assert(nvs_flash_init_partition("replay_nvs") == ESP_OK);
            assert(nvs_open_from_partition("replay_nvs", "replay", NVS_READWRITE,
                                           &handle) == ESP_OK);
            ++s_reinitializations;
            verify_ring(handle, sequence);
        }
    }
    verify_ring(handle, ITERATIONS);
    const nvs_stats_t stats = print_stats("final", ITERATIONS);
    printf("NVS replay stress passed: operations=%u replacements=%u reinitializations=%u "
           "slot-traversals=%u replacement-cycles=%u used=%u free=%u available=%u total=%u "
           "rollover-used-range=%u..%u rollover-available-range=%u..%u\n",
           ITERATIONS, ITERATIONS - KEYVALET_REPLAY_CAPACITY,
           s_reinitializations, s_rollovers, s_rollovers - 1U,
           (unsigned)stats.used_entries,
           (unsigned)stats.free_entries, (unsigned)stats.available_entries,
           (unsigned)stats.total_entries, (unsigned)s_min_used,
           (unsigned)s_max_used, (unsigned)s_min_available,
           (unsigned)s_max_available);
#if defined(CONFIG_IDF_TARGET_LINUX) && defined(CONFIG_ESP_PARTITION_ENABLE_STATS)
    printf("FLASH stats: reads=%u read-bytes=%u writes=%u write-bytes=%u "
           "sector-erases=%u estimated-ms=%u\n",
           (unsigned)esp_partition_get_read_ops(),
           (unsigned)esp_partition_get_read_bytes(),
           (unsigned)esp_partition_get_write_ops(),
           (unsigned)esp_partition_get_write_bytes(),
           (unsigned)esp_partition_get_erase_ops(),
           (unsigned)esp_partition_get_total_time());
#endif
    nvs_close(handle);
#ifdef CONFIG_IDF_TARGET_LINUX
    exit(EXIT_SUCCESS);
#endif
}
