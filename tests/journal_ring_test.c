#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "journal_ring.h"
#include "journal_sync_contract.h"

typedef struct {
    uint8_t *flash;
    size_t sectors;
    uint8_t meta[KEYVALET_JOURNAL_RING_META_SIZE];
    bool present;
    unsigned mutations, reads, erases, checkpoints;
    unsigned cut_at;
    size_t partial;
    jmp_buf power_loss;
} flash_t;

static flash_t device;
static keyvalet_journal_ring_t ring;
static uint8_t scratch[4096];
static unsigned fault_cases;

static bool read_flash(void *context, size_t offset, void *data, size_t length)
{
    flash_t *flash = context;
    assert(offset + length <= flash->sectors * 4096U);
    memcpy(data, flash->flash + offset, length);
    ++flash->reads;
    return true;
}

static bool write_flash(void *context, size_t offset, const void *data, size_t length)
{
    flash_t *flash = context;
    const bool cut = ++flash->mutations == flash->cut_at;
    const size_t actual = cut && flash->partial < length ? flash->partial : length;
    assert(offset + length <= flash->sectors * 4096U);
    for (size_t i = 0; i < actual; ++i) {
        const uint8_t byte = ((const uint8_t *)data)[i];
        assert((flash->flash[offset + i] & byte) == byte);
        flash->flash[offset + i] &= byte;
    }
    if (cut) longjmp(flash->power_loss, 1);
    return true;
}

static bool erase_flash(void *context, size_t sector)
{
    flash_t *flash = context;
    assert(sector < flash->sectors);
    const bool cut = ++flash->mutations == flash->cut_at;
    size_t length = 4096U;
    if (cut && flash->partial < length) length = flash->partial;
    memset(flash->flash + sector * 4096U, 0xff, length);
    ++flash->erases;
    if (cut) longjmp(flash->power_loss, 1);
    return true;
}

static bool load_meta(void *context, uint8_t *data, size_t length, bool *present)
{
    flash_t *flash = context;
    assert(length == sizeof(flash->meta));
    *present = flash->present;
    if (*present) memcpy(data, flash->meta, length);
    return true;
}

static bool save_meta(void *context, const uint8_t *data, size_t length)
{
    flash_t *flash = context;
    const bool cut = ++flash->mutations == flash->cut_at;
    assert(length == sizeof(flash->meta));
    /* NVS single-blob commit is atomic: reboot observes either old or new. */
    if (!cut || flash->partial >= length) {
        memcpy(flash->meta, data, length);
        flash->present = true;
    }
    ++flash->checkpoints;
    if (cut) longjmp(flash->power_loss, 1);
    return true;
}

static void recover(void)
{
    const keyvalet_journal_storage_t storage = {
        .context = &device, .read = read_flash, .write = write_flash,
        .erase_sector = erase_flash, .load_meta = load_meta, .save_meta = save_meta,
    };
    assert(keyvalet_journal_ring_recover(&ring, &storage, device.sectors, scratch));
}

static void fresh(size_t sectors)
{
    free(device.flash);
    memset(&device, 0, sizeof(device));
    device.sectors = sectors;
    device.flash = malloc(sectors * 4096U);
    assert(device.flash);
    memset(device.flash, 0xff, sectors * 4096U);
    recover();
}

static uint64_t append(unsigned detail)
{
    keyvalet_journal_record_t record = {
        .type = KEYVALET_EVENT_SENSOR_OPEN, .detail = detail,
        .metadata = {0x11, 0x22, 0x33},
    };
    assert(keyvalet_journal_ring_append(&ring, &record));
    return record.sequence;
}

static void check_read(uint64_t sequence, unsigned detail)
{
    keyvalet_journal_record_t record;
    assert(keyvalet_journal_ring_read(&ring, sequence, &record));
    assert(record.sequence == sequence && record.detail == detail);
    assert(record.metadata[0] == 0x11 && record.metadata[2] == 0x33);
}

static void check_batches(uint64_t start)
{
    uint64_t cursor = start;
    bool more;
    do {
        const unsigned before = device.reads;
        keyvalet_journal_record_t records[8];
        size_t count;
        uint64_t next, gap_start, gap_end;
        assert(keyvalet_journal_ring_batch(&ring, cursor, records, 8, &count,
            &next, &more, &gap_start, &gap_end));
        assert(device.reads - before <= 16U);
        assert(count <= 8U);
        for (size_t i = 0; i < count; ++i) {
            assert(records[i].sequence == cursor + i);
            check_read(records[i].sequence, (unsigned)records[i].sequence);
        }
        if (gap_start) {
            assert(gap_start == cursor + count);
            for (uint64_t seq = gap_start; seq <= gap_end; ++seq)
                assert(!keyvalet_journal_ring_contains(&ring, seq));
        }
        if (more) assert(next > cursor);
        cursor = next;
    } while (more);
    assert(cursor == ring.next_sequence || start >= ring.next_sequence);
}

static void normal_and_wrap(void)
{
    fresh(3);
    check_batches(1); /* empty */
    for (unsigned i = 1; i <= 192; ++i) assert(append(i) == i);
    assert(ring.valid_count == 192 && ring.oldest_sequence == 1);
    check_batches(1);
    const unsigned erased = device.erases;
    assert(append(193) == 193);
    assert(device.erases == erased + 1U);
    assert(ring.oldest_sequence == 65 && ring.retired_through == 64);
    assert(ring.valid_count == 129);
    uint64_t start, end;
    assert(keyvalet_journal_ring_first_gap(&ring, 1, &start, &end));
    assert(start == 1 && end == 64);
    /* Exactly the same reclamation whether authority acked old records or not. */
    assert(!keyvalet_journal_ring_first_gap(&ring, 65, &start, &end));
    for (unsigned i = 194; i <= 1600; ++i) {
        assert(append(i) == i);
        if (i % 79U == 0) recover();
    }
    recover();
    check_batches(1);
    check_batches(ring.oldest_sequence);
    assert(ring.next_sequence == 1601 && ring.latest_sequence == 1600);
    /* Signed authority can cover a durably recorded loss with unchanged V1. */
    assert(keyvalet_journal_ack_decide(100, ring.next_sequence - 1U, 1500) ==
           KEYVALET_JOURNAL_ACK_ADVANCE);
    assert(keyvalet_journal_ack_decide(1500, 1600, 1500) ==
           KEYVALET_JOURNAL_ACK_ALREADY_ACKNOWLEDGED);
    assert(keyvalet_journal_ack_decide(1500, 1600, 1601) ==
           KEYVALET_JOURNAL_ACK_BEYOND_LATEST);
}

static void initialization(void)
{
    fresh(3);
    assert(device.present && device.checkpoints == 2 && device.erases == 1);
    assert(ring.next_sequence == 1 && ring.valid_count == 0 && ring.ready);
    assert(append(1) == 1);
    recover();
    assert(ring.next_sequence == 2 && ring.valid_count == 1);
    check_read(1, 1);
    const keyvalet_journal_storage_t storage = ring.storage;
    device.present = false;
    const unsigned mutations = device.mutations;
    assert(!keyvalet_journal_ring_recover(&ring, &storage, 3, scratch));
    assert(device.mutations == mutations); /* no adoption or destructive repair */

    /* Interrupt each mutation of virgin initialization, both before and after
     * its durable effect. Reboot must establish a canonical empty generation. */
    static const size_t partials[] = {0, 1, 40, 2048, 4096};
    for (unsigned mutation = 1; mutation <= 3; ++mutation) {
        for (size_t p = 0; p < sizeof(partials) / sizeof(partials[0]); ++p) {
            memset(device.flash, 0xff, 3U * 4096U);
            device.present = false;
            device.mutations = 0;
            device.cut_at = mutation;
            device.partial = partials[p];
            if (setjmp(device.power_loss) == 0) recover();
            device.cut_at = 0;
            recover();
            assert(ring.next_sequence == 1 && ring.valid_count == 0);
            assert(append(1) == 1);
            recover();
            assert(ring.next_sequence == 2);
            check_read(1, 1);
            ++fault_cases;
        }
    }
}

static void corrupt_records(void)
{
    fresh(2);
    for (unsigned i = 1; i <= 20; ++i) append(i);
    device.flash[3U * 64U + 40U] ^= 1U;
    memset(device.flash + 8U * 64U, 0xff, 64U);
    /* An interrupted last record still consumes its sequence. */
    device.flash[19U * 64U + 60U] = 0xff;
    recover();
    assert(ring.next_sequence == 21 && ring.latest_sequence == 19);
    uint64_t start, end;
    assert(keyvalet_journal_ring_first_gap(&ring, 1, &start, &end));
    assert(start == 4 && end == 4);
    assert(keyvalet_journal_ring_first_gap(&ring, 5, &start, &end));
    assert(start == 9 && end == 9);
    assert(keyvalet_journal_ring_first_gap(&ring, 10, &start, &end));
    assert(start == 20 && end == 20);
    check_batches(1);
    assert(append(21) == 21);
}

static void failure_matrix(unsigned baseline)
{
    fresh(2);
    for (unsigned i = 1; i <= baseline; ++i) append(i);
    uint8_t image[8192], meta[40];
    memcpy(image, device.flash, sizeof(image));
    memcpy(meta, device.meta, sizeof(meta));
    /* PREPARED save / erase / READY save / body / commit, interrupted before,
     * during and after each mutation. Reads/verification cannot mutate flash. */
    static const size_t partials[] = {0, 1, 4, 30, 40, 59, 60, 2048, 4096};
    for (unsigned mutation = 1; mutation <= 5; ++mutation) {
        for (size_t p = 0; p < sizeof(partials) / sizeof(partials[0]); ++p) {
            memcpy(device.flash, image, sizeof(image));
            memcpy(device.meta, meta, sizeof(meta));
            device.present = true;
            device.cut_at = 0;
            recover();
            device.mutations = 0;
            device.cut_at = mutation;
            device.partial = partials[p];
            if (setjmp(device.power_loss) == 0) append(baseline + 1U);
            device.cut_at = 0;
            recover();
            assert(ring.next_sequence > baseline);
            /* All of the newer sector must survive every target erase. */
            for (unsigned seq = baseline - 63U; seq <= baseline; ++seq)
                check_read(seq, seq);
            const uint64_t next = ring.next_sequence;
            assert(append((unsigned)next) == next);
            recover();
            assert(ring.next_sequence == next + 1U);
            check_read(next, (unsigned)next);
            ++fault_cases;
        }
    }
    /* Persist PREPARED then repeatedly interrupt reboot erase itself. */
    memcpy(device.flash, image, sizeof(image));
    memcpy(device.meta, meta, sizeof(meta));
    device.cut_at = 0;
    recover();
    device.mutations = 0;
    device.cut_at = 2; device.partial = 19;
    if (setjmp(device.power_loss) == 0) append(baseline + 1U);
    for (unsigned repeat = 0; repeat < 5; ++repeat) {
        device.mutations = 0;
        device.cut_at = 2; device.partial = 65U * repeat;
        if (setjmp(device.power_loss) == 0) recover();
    }
    device.cut_at = 0;
    recover();
    assert(append(baseline + 1U) == baseline + 1U);
    check_read(baseline + 1U, baseline + 1U);
}

static void actual_capacity(void)
{
    fresh(640);
    /* More than three complete v0.4.2 journals, with no synchronization. */
    const unsigned total = 40960U * 3U + 777U;
    for (unsigned seq = 1; seq <= total; ++seq) assert(append(seq) == seq);
    recover();
    assert(ring.next_sequence == total + 1U);
    assert(ring.valid_count >= 40960U - 63U && ring.valid_count <= 40960U);
    assert(device.checkpoints == 2U * device.erases);
    check_batches(1);
    printf("actual capacity: %u events, %u sector erases, %u metadata commits; RAM=%zu bytes\n",
           total, device.erases, device.checkpoints, sizeof(ring));
}

static void damaged_metadata(void)
{
    fresh(2);
    for (unsigned i = 1; i <= 140; ++i) append(i);
    const keyvalet_journal_storage_t storage = ring.storage;
    uint8_t saved_meta[40], saved_flash[8192];
    memcpy(saved_meta, device.meta, sizeof(saved_meta));
    memcpy(saved_flash, device.flash, sizeof(saved_flash));
    device.meta[32] ^= 1;
    assert(!keyvalet_journal_ring_recover(&ring, &storage, 2, scratch));
    assert(memcmp(saved_flash, device.flash, sizeof(saved_flash)) == 0);
    memcpy(device.meta, saved_meta, sizeof(saved_meta));
    device.present = false;
    /* Populated data cannot be reinterpreted without its checkpoint. */
    assert(!keyvalet_journal_ring_recover(&ring, &storage, 2, scratch));
    assert(memcmp(saved_flash, device.flash, sizeof(saved_flash)) == 0);
    device.present = true;
    recover();
    assert(ring.next_sequence == 141);
}

static void loss_bounds(void)
{
    fresh(2);
    for (unsigned seq = 1; seq <= 193; ++seq) append(seq);
    for (unsigned seq = 129; seq <= 150; ++seq)
        device.flash[((seq - 1U) % 128U) * 64U + 40U] ^= 1U;
    recover();
    assert(ring.oldest_sequence == 151);
    uint64_t start, end, next;
    assert(keyvalet_journal_ring_first_gap(&ring, 101, &start, &end));
    assert(start == 101 && end == 150);
    keyvalet_journal_record_t records[8];
    size_t count;
    bool more;
    assert(keyvalet_journal_ring_batch(&ring, 101, records, 8, &count,
        &next, &more, &start, &end));
    assert(count == 0 && more && next == 151 && start == 101 && end == 150);
    assert(!keyvalet_journal_ring_first_gap(&ring, 151, &start, &end));
    check_batches(101);
    /* No valid survivor: consumed watermark must not become an empty/new log. */
    for (unsigned seq = 151; seq <= 193; ++seq)
        device.flash[((seq - 1U) % 128U) * 64U + 40U] ^= 1U;
    recover();
    assert(ring.valid_count == 0 && ring.oldest_sequence == 0 && ring.latest_sequence == 0);
    assert(ring.next_sequence == 194);
    assert(keyvalet_journal_ring_first_gap(&ring, 101, &start, &end));
    assert(start == 101 && end == 193);
    assert(append(194) == 194);
}

int main(void)
{
    normal_and_wrap();
    initialization();
    corrupt_records();
    failure_matrix(128);
    failure_matrix(384);
    damaged_metadata();
    loss_bounds();
    actual_capacity();
    free(device.flash);
    printf("circular journal tests passed (%u append power-cut cases plus reboot cuts)\n",
           fault_cases);
    return 0;
}
