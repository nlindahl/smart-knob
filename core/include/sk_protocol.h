#ifndef SK_PROTOCOL_H
#define SK_PROTOCOL_H
#include "sk.h"
#define SK_MAX_PAYLOAD 512
enum sk_op {
    SK_HELLO = 1,
    SK_BEGIN,
    SK_CHUNK,
    SK_COMMIT,
    SK_SELECT,
    SK_STATE,
    SK_CALIBRATE,
    SK_TARE,
    SK_SAVE,
    SK_STOP,
    SK_ENABLE,
    SK_ENCODER_DIAGNOSTICS
};
enum sk_status { SK_OK, SK_INVALID, SK_BUSY, SK_BAD_BUNDLE, SK_STORAGE, SK_HARDWARE };
typedef struct {
    void *context;
    void (*send)(void *, const uint8_t *, size_t);
    bool (*apply)(void *, const uint8_t *, size_t);
    bool (*save)(void *, const uint8_t *, size_t);
    uint8_t (*command)(void *, uint8_t, const uint8_t *, size_t, uint8_t *, size_t *);
} sk_protocol_ops;
typedef struct {
    sk_protocol_ops ops;
    uint8_t rx[SK_MAX_PAYLOAD + 12];
    size_t rx_len;
    uint8_t upload[SK_MAX_BUNDLE], active[SK_MAX_BUNDLE];
    uint32_t expected_len, expected_crc, received;
    size_t active_len;
    bool uploading;
} sk_protocol;
void sk_protocol_init(sk_protocol *p, sk_protocol_ops ops);
void sk_protocol_feed(sk_protocol *p, const uint8_t *data, size_t len);
#endif
