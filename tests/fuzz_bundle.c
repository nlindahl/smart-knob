#include "sk.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static sk_project project;
static uint8_t original[SK_MAX_BUNDLE], bytes[SK_MAX_BUNDLE];
static uint32_t random_state = 1234567;
static uint32_t random32(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f)
        return 2;
    size_t len = fread(original, 1, sizeof(original), f);
    fclose(f);
    assert(sk_project_decode(original, len, &project));
    for (size_t i = 0; i < len; i++)
        assert(!sk_project_decode(original, i, &project));
    for (int i = 0; i < 30000; i++) {
        memcpy(bytes, original, len);
        for (unsigned n = 0; n < 1 + random32() % 10; n++)
            bytes[12 + random32() % (len - 12)] ^= (uint8_t)random32();
        uint32_t crc = sk_crc32(bytes + 12, len - 12);
        for (int n = 0; n < 4; n++)
            bytes[8 + n] = crc >> (n * 8);
        sk_project_decode(bytes, len, &project);
    }
    puts("Bundle decoder: every truncation and 30000 CRC-valid mutations checked");
    return 0;
}
