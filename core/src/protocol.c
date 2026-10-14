#include "sk_protocol.h"
#include <string.h>
static uint16_t u16(const uint8_t *p) {
    return p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t u32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++)
        p[i] = v >> (8 * i);
}
void sk_protocol_init(sk_protocol *p, sk_protocol_ops ops) {
    memset(p, 0, sizeof(*p));
    p->ops = ops;
}
static void dispatch(sk_protocol *p, const uint8_t *f, size_t len) {
    uint8_t reply[SK_MAX_PAYLOAD + 12] = {'S', 'K', 1, (uint8_t)(f[3] | 0x80), f[4], f[5]};
    uint8_t *data = reply + 9;
    size_t result_len = 0;
    uint8_t status = SK_INVALID;
    const uint8_t *payload = f + 8;
    switch (f[3]) {
    case SK_HELLO:
        if (!len) {
            const char *name = "SmartKnob DevKit 0.1 / Zephyr / protocol 1";
            result_len = strlen(name);
            memcpy(data, name, result_len);
            status = SK_OK;
        }
        break;
    case SK_BEGIN:
        if (len == 8 && u32(payload) >= 12 && u32(payload) <= SK_MAX_BUNDLE) {
            p->expected_len = u32(payload);
            p->expected_crc = u32(payload + 4);
            p->received = 0;
            p->uploading = true;
            status = SK_OK;
        }
        break;
    case SK_CHUNK:
        if (p->uploading && len > 4 && u32(payload) == p->received &&
            len - 4 <= p->expected_len - p->received) {
            memcpy(p->upload + p->received, payload + 4, len - 4);
            p->received += len - 4;
            status = SK_OK;
        }
        break;
    case SK_COMMIT:
        if (!len && p->uploading) {
            if (p->received == p->expected_len &&
                sk_crc32(p->upload, p->received) == p->expected_crc &&
                p->ops.apply(p->ops.context, p->upload, p->received)) {
                memcpy(p->active, p->upload, p->received);
                p->active_len = p->received;
                status = SK_OK;
            } else
                status = SK_BAD_BUNDLE;
            p->uploading = false;
        }
        break;
    case SK_SAVE:
        if (!len && p->active_len)
            status = p->ops.save(p->ops.context, p->active, p->active_len) ? SK_OK : SK_STORAGE;
        break;
    default:
        if (p->ops.command)
            status = p->ops.command(p->ops.context, f[3], payload, len, data, &result_len);
        break;
    }
    if (result_len > SK_MAX_PAYLOAD - 1) {
        status = SK_INVALID;
        result_len = 0;
    }
    reply[8] = status;
    size_t body = result_len + 1;
    reply[6] = body;
    reply[7] = body >> 8;
    put32(reply + 8 + body, sk_crc32(reply + 2, body + 6));
    p->ops.send(p->ops.context, reply, body + 12);
}
void sk_protocol_feed(sk_protocol *p, const uint8_t *bytes, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (p->rx_len == sizeof(p->rx))
            p->rx_len = 0;
        p->rx[p->rx_len++] = bytes[i];
        while (p->rx_len >= 2) {
            if (p->rx[0] != 'S' || p->rx[1] != 'K')
                goto shift;
            if (p->rx_len < 8)
                break;
            size_t n = u16(p->rx + 6);
            if (p->rx[2] != 1 || n > SK_MAX_PAYLOAD)
                goto shift;
            if (p->rx_len < n + 12)
                break;
            if (u32(p->rx + n + 8) != sk_crc32(p->rx + 2, n + 6))
                goto shift;
            dispatch(p, p->rx, n);
            memmove(p->rx, p->rx + n + 12, p->rx_len - n - 12);
            p->rx_len -= n + 12;
            continue;
        shift:
            memmove(p->rx, p->rx + 1, --p->rx_len);
        }
    }
}
