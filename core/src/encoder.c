#include "sk.h"
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
bool sk_mt6701_decode(const uint8_t bytes[3], float *angle) {
    uint32_t word = ((uint32_t)bytes[0] << 16) | ((uint32_t)bytes[1] << 8) | bytes[2];
    /* Field 1/2 warns about magnet strength; CRC-valid angle data remains usable.
     * Keep loss-of-tracking and reserved field status 3 as hard failures. */
    if (sk_mt6701_crc6(bytes) != (word & 63) || (word & (1u << 9)) || ((word >> 6) & 3) == 3)
        return false;
    *angle = (float)(word >> 10) * SK_TAU / 16384;
    return true;
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
