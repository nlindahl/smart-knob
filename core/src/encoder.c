#include "sk.h"
bool sk_mt6701_decode(const uint8_t bytes[3], float *angle) {
    uint32_t word = ((uint32_t)bytes[0] << 16) | ((uint32_t)bytes[1] << 8) | bytes[2];
    uint32_t bits = word >> 6;
    uint8_t crc = 0;
    for (int bit = 17; bit >= 0; bit--) {
        bool feedback = ((crc >> 5) ^ ((bits >> bit) & 1)) & 1;
        crc = (crc << 1) & 63;
        if (feedback)
            crc ^= 3;
    }
    if (crc != (word & 63) || (word & (1u << 9)) || ((word >> 6) & 3) != 0)
        return false;
    *angle = (float)(word >> 10) * SK_TAU / 16384;
    return true;
}
