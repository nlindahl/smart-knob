#include "hardware.h"
#include <math.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/storage/flash_map.h>
static struct nvs_fs fs;
static bool ready;
int storage_init(void) {
    fs.flash_device = PARTITION_NODE_DEVICE(DT_NODELABEL(storage_partition));
    fs.offset = PARTITION_NODE_OFFSET(DT_NODELABEL(storage_partition));
    fs.sector_size = 32768;
    fs.sector_count = PARTITION_NODE_SIZE(DT_NODELABEL(storage_partition)) / fs.sector_size;
    int rc = nvs_mount(&fs);
    ready = !rc;
    return rc;
}
size_t storage_load(uint8_t *out, size_t max) {
    if (!ready)
        return 0;
    ssize_t len = nvs_read(&fs, 1, out, max);
    return len > 0 && (size_t)len <= max ? (size_t)len : 0;
}
bool storage_save(const uint8_t *data, size_t len) {
    if (!ready || len > SK_MAX_BUNDLE)
        return false;
    ssize_t rc = nvs_write(&fs, 1, data, len);
    /* NVS returns zero for an identical value. Commit metadata and CRC are
     * written after the payload, retaining the preceding record on power loss. */
    return rc == 0 || rc == (ssize_t)len;
}
bool storage_load_calibration(sk_calibration *out) {
    if (!ready)
        return false;
    uint8_t record[16];
    if (nvs_read(&fs, 2, record, sizeof(record)) != sizeof(record) || memcmp(record, "SKC1", 4))
        return false;
    uint32_t crc;
    memcpy(&crc, record + 12, 4);
    if (crc != sk_crc32(record, 12))
        return false;
    memcpy(&out->zero_electrical, record + 4, 4);
    out->pole_pairs = record[8];
    out->direction = (int8_t)record[9];
    out->valid = isfinite(out->zero_electrical) && fabsf(out->zero_electrical) <= SK_TAU &&
                 out->pole_pairs >= 3 && out->pole_pairs <= 12 &&
                 (out->direction == 1 || out->direction == -1);
    return out->valid;
}
bool storage_save_calibration(const sk_calibration *c) {
    if (!ready || !c->valid)
        return false;
    uint8_t record[16] = {0};
    memcpy(record, "SKC1", 4);
    memcpy(record + 4, &c->zero_electrical, 4);
    record[8] = c->pole_pairs;
    record[9] = c->direction;
    uint32_t crc = sk_crc32(record, 12);
    memcpy(record + 12, &crc, 4);
    ssize_t rc = nvs_write(&fs, 2, record, sizeof(record));
    return rc == 0 || rc == sizeof(record);
}
