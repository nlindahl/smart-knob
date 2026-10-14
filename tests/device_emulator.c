#include "sk.h"
#include "sk_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static sk_protocol protocol;
static sk_project projects[2];
static unsigned active;
static uint16_t view_id = 1;
static int fd;
static void send(void *ctx, const uint8_t *bytes, size_t len) {
    (void)ctx;
    while (len) {
        ssize_t n = write(fd, bytes, len);
        if (n <= 0)
            exit(2);
        bytes += n;
        len -= n;
    }
}
static bool apply(void *ctx, const uint8_t *bytes, size_t len) {
    (void)ctx;
    unsigned next = 1 - active;
    if (!sk_project_decode(bytes, len, &projects[next]))
        return false;
    active = next;
    view_id = projects[active].start_view;
    return true;
}
static bool save(void *ctx, const uint8_t *bytes, size_t len) {
    (void)ctx;
    (void)bytes;
    (void)len;
    return true;
}
static uint8_t command(void *ctx, uint8_t op, const uint8_t *data, size_t len, uint8_t *out,
                       size_t *out_len) {
    (void)ctx;
    if (op == SK_SELECT && len == 2) {
        uint16_t id = data[0] | ((uint16_t)data[1] << 8);
        if (!sk_find_view(&projects[active], id))
            return SK_INVALID;
        view_id = id;
        return SK_OK;
    }
    if (op == SK_STATE && len == 0) {
        memset(out, 0, 28);
        out[0] = view_id;
        out[1] = view_id >> 8;
        *out_len = 28;
        return SK_OK;
    }
    return SK_INVALID;
}
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    fd = atoi(argv[1]);
    sk_project_default(&projects[0]);
    sk_protocol_init(
        &protocol,
        (sk_protocol_ops){.send = send, .apply = apply, .save = save, .command = command});
    uint8_t bytes[512];
    ssize_t len;
    while ((len = read(fd, bytes, sizeof(bytes))) > 0)
        sk_protocol_feed(&protocol, bytes, len);
    return 0;
}
