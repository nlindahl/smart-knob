#include "hardware.h"
#include "sk_protocol.h"
#include "sk_render.h"
#include <string.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/ring_buffer.h>

static sk_project projects[2];
static unsigned active;
static const sk_view *view;
static sk_renderer renderer;
static sk_protocol protocol;
static uint8_t system_faults;
static bool motor_ready, strain_ready;
static uint16_t presses;
static uint32_t flash_until;
static const struct device *serial = DEVICE_DT_GET(DT_NODELABEL(usb_serial));
static const struct device *strip = DEVICE_DT_GET(DT_ALIAS(led_strip));
static const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static uint8_t draw_buffer[240 * 24 * 2] __aligned(4);
static uint32_t lv_ticks(void) {
    return k_uptime_get_32();
}
static void display_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *map) {
    struct display_buffer_descriptor desc = {.width = area->x2 - area->x1 + 1,
                                             .height = area->y2 - area->y1 + 1};
    desc.pitch = desc.width;
    desc.buf_size = desc.width * desc.height * 2;
    lv_draw_sw_rgb565_swap(map, desc.width * desc.height);
    if (display_write(display, area->x1, area->y1, &desc, map))
        system_faults |= FAULT_DISPLAY;
    lv_display_flush_ready(disp);
}
static const struct pwm_dt_spec backlight = PWM_DT_SPEC_GET(DT_NODELABEL(backlight_pwm));
RING_BUF_DECLARE(serial_rx, 8192);
static atomic_t rx_overflow;
static void serial_isr(const struct device *dev, void *arg) {
    (void)arg;
    while (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
        uint8_t bytes[64];
        int n = uart_fifo_read(dev, bytes, sizeof(bytes));
        if (n <= 0)
            break;
        if (ring_buf_put(&serial_rx, bytes, n) != (uint32_t)n)
            atomic_set(&rx_overflow, 1);
    }
}
static void send(void *ctx, const uint8_t *bytes, size_t len) {
    (void)ctx;
    for (size_t i = 0; i < len; i++)
        uart_poll_out(serial, bytes[i]);
}
typedef struct {
    sk_led_config leds;
    sk_haptic_config haptic;
    int32_t position;
    float sub;
    uint32_t flash;
} led_state;
static led_state ring_state;
static struct k_spinlock led_lock;
static atomic_t led_fault;
static struct k_thread led_thread;
K_THREAD_STACK_DEFINE(led_stack, 2048);
static void led_run(void *a, void *b, void *c) {
    (void)a;
    (void)b;
    (void)c;
    while (true) {
        led_state s;
        k_spinlock_key_t key = k_spin_lock(&led_lock);
        s = ring_state;
        k_spin_unlock(&led_lock, key);
        sk_rgb colors[72];
        struct led_rgb pixels[72];
        sk_led_render(&s.leds, &s.haptic, s.position, s.sub, k_uptime_get_32(), s.flash, colors);
        for (int i = 0; i < 72; i++)
            pixels[i] = (struct led_rgb){.r = colors[i].r, .g = colors[i].g, .b = colors[i].b};
        atomic_set(&led_fault, led_strip_update_rgb(strip, pixels, 72) != 0);
        k_sleep(K_MSEC(33));
    }
}
static bool select_view(uint16_t id) {
    const sk_view *next = sk_find_view(&projects[active], id);
    if (!next)
        return false;
    motor_status s;
    motor_snapshot(&s);
    if (s.calibration == 1)
        return false;
    if (motor_ready && !motor_configure(&next->haptic))
        return false;
    view = next;
    sk_render_view(&renderer, view);
    if (view->leds.mode_flash)
        flash_until = k_uptime_get_32() + 180;
    return true;
}
static bool apply(void *ctx, const uint8_t *bytes, size_t len) {
    (void)ctx;
    motor_status s;
    motor_snapshot(&s);
    if (s.calibration == 1)
        return false;
    unsigned next = 1 - active;
    if (!sk_project_decode(bytes, len, &projects[next]))
        return false;
    const sk_view *new_view = sk_find_view(&projects[next], projects[next].start_view);
    if (motor_ready && !motor_configure(&new_view->haptic))
        return false;
    active = next;
    view = new_view;
    sk_render_view(&renderer, view);
    if (view->leds.mode_flash)
        flash_until = k_uptime_get_32() + 180;
    return true;
}
static bool save(void *ctx, const uint8_t *bytes, size_t len) {
    (void)ctx;
    motor_status s;
    motor_snapshot(&s);
    if (s.calibration == 1)
        return false;
    if (motor_ready && !motor_pause(true))
        return false;
    bool ok = storage_save(bytes, len);
    if (motor_ready)
        motor_pause(false);
    if (!ok)
        system_faults |= FAULT_STORAGE;
    return ok;
}
static void put32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++)
        p[i] = v >> (8 * i);
}
static void putfloat(uint8_t *p, float v) {
    uint32_t bits;
    memcpy(&bits, &v, 4);
    put32(p, bits);
}
static uint8_t command(void *ctx, uint8_t op, const uint8_t *bytes, size_t len, uint8_t *out,
                       size_t *out_len) {
    (void)ctx;
    if (op == SK_SELECT) {
        if (len != 2)
            return SK_INVALID;
        return select_view(bytes[0] | ((uint16_t)bytes[1] << 8)) ? SK_OK : SK_BUSY;
    }
    if (len)
        return SK_INVALID;
    if (op == SK_STATE) {
        motor_status s;
        motor_snapshot(&s);
        memset(out, 0, 28);
        out[0] = view->id;
        out[1] = view->id >> 8;
        put32(out + 2, (uint32_t)s.position);
        putfloat(out + 6, s.sub);
        out[10] = s.faults | system_faults | (strain_failed() ? FAULT_STRAIN : 0) |
                  (atomic_get(&led_fault) ? FAULT_LED : 0);
        out[11] = s.calibration;
        put32(out + 12, s.loops);
        putfloat(out + 16, s.voltage);
        putfloat(out + 20, s.max_loop_us);
        out[24] = presses;
        out[25] = presses >> 8;
        *out_len = 28;
        return SK_OK;
    }
    if (op == SK_TARE)
        return strain_ready && strain_tare() ? SK_OK : SK_HARDWARE;
    if (!motor_ready)
        return SK_HARDWARE;
    if (op == SK_CALIBRATE)
        return motor_calibrate() ? SK_OK : SK_BUSY;
    if (op == SK_STOP)
        return motor_enable(false) ? SK_OK : SK_BUSY;
    if (op == SK_ENABLE) {
        motor_status s;
        motor_snapshot(&s);
        if (s.calibration != 2)
            return SK_HARDWARE;
        return motor_enable(true) ? SK_OK : SK_BUSY;
    }
    return SK_INVALID;
}
int main(void) {
    if (!device_is_ready(display))
        return -ENODEV;
    lv_init();
    lv_tick_set_cb(lv_ticks);
    lv_display_t *disp = lv_display_create(240, 240);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, draw_buffer, NULL, sizeof(draw_buffer),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, display_flush);
    if (storage_init())
        system_faults |= FAULT_STORAGE;
    motor_ready = motor_start() == 0;
    if (!motor_ready)
        system_faults |= FAULT_MOTOR;
    strain_ready = strain_start() == 0;
    if (!strain_ready)
        system_faults |= FAULT_STRAIN;
    sk_project_default(&projects[0]);
    sk_protocol_init(
        &protocol,
        (sk_protocol_ops){.send = send, .apply = apply, .save = save, .command = command});
    size_t len = storage_load(protocol.active, sizeof(protocol.active));
    if (len && sk_project_decode(protocol.active, len, &projects[1])) {
        active = 1;
        protocol.active_len = len;
    }
    select_view(projects[active].start_view);
    display_blanking_off(display);
    if (pwm_is_ready_dt(&backlight))
        pwm_set_pulse_dt(&backlight, backlight.period / 2);
    if (device_is_ready(serial)) {
        uart_irq_callback_user_data_set(serial, serial_isr, NULL);
        uart_irq_rx_enable(serial);
    }
    if (device_is_ready(strip))
        k_thread_create(&led_thread, led_stack, K_THREAD_STACK_SIZEOF(led_stack), led_run, NULL,
                        NULL, NULL, 8, K_FP_REGS, K_NO_WAIT);
    else
        system_faults |= FAULT_LED;
    const struct device *watchdog = DEVICE_DT_GET(DT_ALIAS(watchdog0));
    struct wdt_timeout_cfg timeout = {.window = {.min = 0, .max = 2000},
                                      .flags = WDT_FLAG_RESET_SOC};
    int watchdog_channel = wdt_install_timeout(watchdog, &timeout);
    if (watchdog_channel >= 0)
        wdt_setup(watchdog, WDT_OPT_PAUSE_HALTED_BY_DBG);
    uint32_t last_frame = 0;
    uint32_t last_calibration_save = 0, motor_seen_at = k_uptime_get_32(), motor_seen_loops = 0;
    while (true) {
        if (atomic_cas(&rx_overflow, 1, 0)) {
            unsigned key = irq_lock();
            ring_buf_reset(&serial_rx);
            irq_unlock(key);
            protocol.rx_len = 0;
            protocol.uploading = false;
        }
        uint8_t bytes[512];
        uint32_t n = ring_buf_get(&serial_rx, bytes, sizeof(bytes));
        if (n)
            sk_protocol_feed(&protocol, bytes, n);
        uint8_t event;
        while ((event = strain_event())) {
            if (event == 1) {
                presses++;
                motor_pulse(true, false);
                if (view->leds.press_flash)
                    flash_until = k_uptime_get_32() + 180;
            } else if (event == 3) {
                motor_pulse(true, true);
                if (view->on_long_press)
                    select_view(view->on_long_press);
            } else if (event == 2) {
                motor_pulse(false, false);
                if (view->on_press)
                    select_view(view->on_press);
            } else if (event == 4)
                motor_pulse(false, false);
        }
        if (motor_ready && motor_calibration_dirty() &&
            (last_calibration_save == 0 || k_uptime_get_32() - last_calibration_save >= 5000)) {
            last_calibration_save = k_uptime_get_32();
            sk_calibration cal;
            if (motor_get_calibration(&cal) && motor_pause(true)) {
                if (storage_save_calibration(&cal))
                    motor_calibration_saved();
                else
                    system_faults |= FAULT_STORAGE;
                motor_pause(false);
            }
        }
        uint32_t now = k_uptime_get_32();
        if (now - last_frame >= 33) {
            last_frame = now;
            motor_status s;
            motor_snapshot(&s);
            sk_render_value(&renderer, s.position, s.sub);
            lv_timer_handler();
            k_spinlock_key_t key = k_spin_lock(&led_lock);
            ring_state = (led_state){.leds = view->leds,
                                     .haptic = view->haptic,
                                     .position = s.position,
                                     .sub = s.sub,
                                     .flash = flash_until};
            k_spin_unlock(&led_lock, key);
        }
        motor_status alive;
        motor_snapshot(&alive);
        if (alive.loops != motor_seen_loops) {
            motor_seen_loops = alive.loops;
            motor_seen_at = now;
        }
        if (watchdog_channel >= 0 && (!motor_ready || now - motor_seen_at < 200))
            wdt_feed(watchdog, watchdog_channel);
        k_sleep(K_MSEC(1));
    }
}
