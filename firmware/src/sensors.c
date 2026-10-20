#include "hardware.h"
#include <math.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
static const struct spi_dt_spec encoder =
    SPI_DT_SPEC_GET(DT_NODELABEL(encoder), SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_MODE_CPHA);
static const struct gpio_dt_spec strain_data = GPIO_DT_SPEC_GET(DT_NODELABEL(strain_data), gpios);
static const struct gpio_dt_spec strain_clock = GPIO_DT_SPEC_GET(DT_NODELABEL(strain_clock), gpios);
static struct k_spinlock encoder_lock;
static struct {
    uint32_t reads, valid, spi_errors, crc_errors, status_errors;
    int spi_rc;
    uint8_t raw[3], expected_crc;
    uint8_t last_bad[3], last_reason;
    uint32_t last_bad_ms;
    uint32_t bad_run, max_bad_run;
} encoder_diag;
int encoder_init(void) {
    return spi_is_ready_dt(&encoder) ? 0 : -ENODEV;
}
int encoder_read(float *angle) {
    uint8_t bytes[3] = {0};
    struct spi_buf buffer = {.buf = bytes, .len = 3};
    struct spi_buf_set rx = {.buffers = &buffer, .count = 1};
    int rc = spi_read_dt(&encoder, &rx);
    uint8_t expected_crc = sk_mt6701_crc6(bytes);
    bool valid = !rc && sk_mt6701_decode(bytes, angle);
    k_spinlock_key_t key = k_spin_lock(&encoder_lock);
    encoder_diag.reads++;
    encoder_diag.spi_rc = rc;
    memcpy(encoder_diag.raw, bytes, sizeof(bytes));
    encoder_diag.expected_crc = expected_crc;
    if (rc)
        encoder_diag.spi_errors++;
    else if (expected_crc != (bytes[2] & 63))
        encoder_diag.crc_errors++;
    else if (!valid)
        encoder_diag.status_errors++;
    else {
        encoder_diag.valid++;
        encoder_diag.bad_run = 0;
    }
    if (!valid) {
        encoder_diag.bad_run++;
        encoder_diag.max_bad_run = MAX(encoder_diag.max_bad_run, encoder_diag.bad_run);
        memcpy(encoder_diag.last_bad, bytes, sizeof(bytes));
        encoder_diag.last_reason = rc                                ? 1
                                   : expected_crc != (bytes[2] & 63) ? 2
                                   : (bytes[1] & 2)                  ? 3
                                                                     : 4;
        encoder_diag.last_bad_ms = k_uptime_get_32();
    }
    k_spin_unlock(&encoder_lock, key);
    if (rc)
        return rc;
    if (!valid)
        return -EIO;
    return 0;
}
size_t encoder_diagnostic_text(char *out, size_t size) {
    if (!size)
        return 0;
    k_spinlock_key_t key = k_spin_lock(&encoder_lock);
    __typeof__(encoder_diag) d = encoder_diag;
    k_spin_unlock(&encoder_lock, key);
    int written =
        snprintk(out, size,
                 "Encoder: spi_rc=%d raw=%02X%02X%02X crc=%02X/%02X "
                 "loss=%u field=%u reads=%u valid=%u spi_err=%u crc_err=%u status_err=%u "
                 "last_bad=%02X%02X%02X reason=%s age_ms=%u bad_run=%u max_bad_run=%u",
                 d.spi_rc, d.raw[0], d.raw[1], d.raw[2], d.raw[2] & 63, d.expected_crc,
                 (d.raw[1] >> 1) & 1, (d.raw[2] >> 6) & 3, d.reads, d.valid, d.spi_errors,
                 d.crc_errors, d.status_errors, d.last_bad[0], d.last_bad[1], d.last_bad[2],
                 (const char *[]){"none", "spi", "crc", "loss", "reserved"}[d.last_reason],
                 d.last_reason ? k_uptime_get_32() - d.last_bad_ms : 0, d.bad_run, d.max_bad_run);
    return written > 0 ? MIN((size_t)written, size - 1) : 0;
}
K_MSGQ_DEFINE(press_events, sizeof(uint8_t), 16, 1);
static atomic_t tare_requested, strain_fault;
static struct k_thread strain_thread;
K_THREAD_STACK_DEFINE(strain_stack, 2048);
static int read_strain(int32_t *value) {
    int ready = gpio_pin_get_dt(&strain_data);
    if (ready < 0)
        return ready;
    if (ready)
        return -EAGAIN;
    uint32_t raw = 0;
    /* HX711 powers down if SCK stays high >60 us. Mask IRQs per pulse only. */
    for (int i = 0; i < 24; i++) {
        unsigned key = irq_lock();
        gpio_pin_set_dt(&strain_clock, 1);
        k_busy_wait(1);
        int bit = gpio_pin_get_dt(&strain_data);
        gpio_pin_set_dt(&strain_clock, 0);
        irq_unlock(key);
        k_busy_wait(1);
        if (bit < 0)
            return bit;
        raw = (raw << 1) | (bit != 0);
    }
    unsigned key = irq_lock();
    gpio_pin_set_dt(&strain_clock, 1);
    k_busy_wait(1);
    gpio_pin_set_dt(&strain_clock, 0);
    irq_unlock(key);
    *value = (int32_t)(raw & 0x800000 ? raw | 0xff000000 : raw);
    return 0;
}
static void emit(uint8_t event) {
    if (k_msgq_put(&press_events, &event, K_NO_WAIT))
        atomic_set(&strain_fault, 1);
}
static void strain_run(void *a, void *b, void *c) {
    (void)a;
    (void)b;
    (void)c;
    int64_t tare_sum = 0;
    unsigned samples = 0;
    float zero = 0, filtered = 0;
    bool pressed = false, long_sent = false;
    uint32_t pressed_at = 0, last_sample = k_uptime_get_32();
    while (true) {
        if (atomic_cas(&tare_requested, 1, 0)) {
            samples = 0;
            tare_sum = 0;
            if (pressed)
                emit(long_sent ? 4 : 2);
            pressed = false;
            long_sent = false;
        }
        int32_t raw = 0;
        int rc = read_strain(&raw);
        uint32_t now = k_uptime_get_32();
        if (!rc) {
            last_sample = now;
            atomic_clear(&strain_fault);
            if (samples < 16) {
                tare_sum += raw;
                samples++;
                if (samples == 16) {
                    zero = (float)tare_sum / 16;
                    filtered = zero;
                }
            } else {
                filtered += .35f * ((float)raw - filtered);
                /* Raw threshold is configurable; factory load-cell gains vary. */
                float force = fabsf(filtered - zero);
                if (!pressed && force > CONFIG_SK_PRESS_THRESHOLD) {
                    pressed = true;
                    long_sent = false;
                    pressed_at = now;
                    emit(1);
                }
                if (pressed && !long_sent && now - pressed_at >= 800) {
                    long_sent = true;
                    emit(3);
                }
                if (pressed && force < CONFIG_SK_PRESS_THRESHOLD * .55f) {
                    emit(long_sent ? 4 : 2);
                    pressed = false;
                }
                if (!pressed && force < CONFIG_SK_PRESS_THRESHOLD * .2f)
                    zero += .001f * (filtered - zero);
            }
        } else if (rc != -EAGAIN || now - last_sample > 1500)
            atomic_set(&strain_fault, 1);
        k_sleep(K_MSEC(2));
    }
}
int strain_start(void) {
    if (!gpio_is_ready_dt(&strain_data) || !gpio_is_ready_dt(&strain_clock))
        return -ENODEV;
    int rc = gpio_pin_configure_dt(&strain_data, GPIO_INPUT);
    if (rc)
        return rc;
    rc = gpio_pin_configure_dt(&strain_clock, GPIO_OUTPUT_INACTIVE);
    if (rc)
        return rc;
    k_thread_create(&strain_thread, strain_stack, K_THREAD_STACK_SIZEOF(strain_stack), strain_run,
                    NULL, NULL, NULL, 6, K_FP_REGS, K_NO_WAIT);
    return 0;
}
bool strain_tare(void) {
    atomic_set(&tare_requested, 1);
    return true;
}
uint8_t strain_event(void) {
    uint8_t e = 0;
    k_msgq_get(&press_events, &e, K_NO_WAIT);
    return e;
}
bool strain_failed(void) {
    return atomic_get(&strain_fault) != 0;
}
