#include "sk.h"
#include <math.h>
uint8_t sk_mt6701_crc6(const uint8_t bytes[3]) {
    uint32_t word = ((uint32_t)bytes[0] << 16) | ((uint32_t)bytes[1] << 8) | bytes[2];
    uint32_t bits = word >> 6;
    uint8_t crc = 0;
    for (int bit = 17; bit >= 0; bit--) {
        bool feedback = ((crc >> 5) ^ ((bits >> bit) & 1)) & 1;
        crc = (crc << 1) & 63;
        if (feedback)
            crc ^= 3;
    }
    return crc;
}
enum sk_mt6701_status sk_mt6701_parse(const uint8_t bytes[3], float *angle) {
    uint32_t word = ((uint32_t)bytes[0] << 16) | ((uint32_t)bytes[1] << 8) | bytes[2];
    if (sk_mt6701_crc6(bytes) != (word & 63))
        return SK_MT6701_CRC;
    /* Field 1/2 warns about magnet strength; the angle remains usable. */
    if (((word >> 6) & 3) == 3)
        return SK_MT6701_RESERVED;
    *angle = (float)(word >> 10) * SK_TAU / 16384;
    return word & (1u << 9) ? SK_MT6701_LOSS : SK_MT6701_OK;
}
bool sk_mt6701_decode(const uint8_t bytes[3], float *angle) {
    float decoded;
    if (sk_mt6701_parse(bytes, &decoded) != SK_MT6701_OK)
        return false;
    *angle = decoded;
    return true;
}
bool sk_encoder_accept(sk_encoder_track *t, enum sk_mt6701_status status, float angle,
                       uint32_t now_ms, enum sk_loss_policy policy) {
    bool ok = status == SK_MT6701_OK;
    /* The factory firmware ignores the tracking-loss flag. Accept such frames
     * only while they continue the last accepted angle, so a jump is still
     * never used for drive output. */
    if (status == SK_MT6701_LOSS && policy != SK_LOSS_REJECT && t->have &&
        fabsf(remainderf(angle - t->last, SK_TAU)) <= SK_ENCODER_MAX_STEP &&
        (policy == SK_LOSS_ACCEPT || (uint32_t)(now_ms - t->clean_ms) <= SK_ENCODER_LOSS_WINDOW_MS))
        ok = true;
    if (ok) {
        t->last = angle;
        t->have = true;
        if (status == SK_MT6701_OK)
            t->clean_ms = now_ms;
    }
    return ok;
}
bool sk_encoder_should_latch(sk_encoder_health *health, bool valid, uint32_t now_ms) {
    if (valid) {
        health->rejecting = false;
        return false;
    }
    if (!health->rejecting) {
        health->rejecting = true;
        health->bad_since_ms = now_ms;
    }
    return (uint32_t)(now_ms - health->bad_since_ms) >= SK_ENCODER_LATCH_MS;
}
