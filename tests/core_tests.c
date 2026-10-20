#include "sk.h"
#include "sk_protocol.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static sk_project project;
static sk_protocol protocol;
static uint8_t response[524];
static size_t response_len;
static unsigned apply_count, save_count;
static void send(void *ctx, const uint8_t *data, size_t len) {
    (void)ctx;
    memcpy(response, data, len);
    response_len = len;
}
static bool apply(void *ctx, const uint8_t *data, size_t len) {
    (void)ctx;
    apply_count++;
    return sk_project_decode(data, len, &project);
}
static bool save(void *ctx, const uint8_t *data, size_t len) {
    (void)ctx;
    (void)data;
    (void)len;
    save_count++;
    return true;
}
static void command(uint8_t op, const uint8_t *data, size_t len) {
    uint8_t frame[524] = {'S', 'K', 1, op, 42, 0, (uint8_t)len, (uint8_t)(len >> 8)};
    if (len)
        memcpy(frame + 8, data, len);
    uint32_t crc = sk_crc32(frame + 2, len + 6);
    for (int i = 0; i < 4; i++)
        frame[len + 8 + i] = crc >> (i * 8);
    response_len = 0;
    for (size_t i = 0; i < len + 12; i++)
        sk_protocol_feed(&protocol, frame + i, 1);
    assert(response_len >= 13);
    assert(response[3] == (op | 0x80));
}
static void haptics(void) {
    sk_haptic_config c = {
        .mode = SK_DETENT, .width_deg = 10, .strength = 1, .endstop = 1, .snap = .6f, .damping = 0};
    sk_haptic_state s;
    sk_haptic_init(&s, &c, 0);
    sk_haptic_init(&s, &s.config, 0);
    assert(s.initialized && s.config.width_deg == 10);
    assert(sk_haptic_step(&s, .04f, .001f) < 0);
    sk_haptic_step(&s, .4f, .001f);
    assert(s.position == 2);
    sk_haptic_init(&s, &c, 6.27f);
    sk_haptic_step(&s, .01f, .001f);
    assert(fabsf(s.sub_position) < .2f);
    c.mode = SK_BOUNDED;
    c.min = 0;
    c.max = 2;
    sk_haptic_init(&s, &c, 0);
    for (int i = 1; i <= 100; i++)
        sk_haptic_step(&s, .01f * i, .001f);
    assert(s.position == 2);
    assert(s.sub_position > 0);
    assert(s.voltage < 0 && s.voltage >= -2);
    sk_haptic_init(&s, &c, 0);
    assert(sk_haptic_step(&s, -.05f, .001f) > 0);
    c.mode = SK_FREE;
    sk_haptic_init(&s, &c, 0);
    assert(sk_haptic_step(&s, .05f, .001f) == 0);
    sk_haptic_pulse(&s, true, false);
    assert(sk_haptic_step(&s, .05f, .001f) > 0);
    assert(sk_haptic_step(&s, NAN, .001f) == 0);
    c.mode = SK_MAGNETIC;
    c.detent_count = 1;
    c.detents[0] = 0;
    sk_haptic_init(&s, &c, 0);
    assert(sk_haptic_step(&s, .04f, .001f) < 0);
    for (int i = 1; i <= 30; i++)
        sk_haptic_step(&s, .01f * i, .001f);
    assert(s.position == 2);
    assert(fabsf(s.voltage) < .001f);
    c.snap = .49f;
    assert(!sk_haptic_valid(&c));
    c.snap = 1;
    c.width_deg = INFINITY;
    assert(!sk_haptic_valid(&c));
    float duty[3];
    sk_foc(0, 0, 0, 5, duty);
    for (int i = 0; i < 3; i++)
        assert(duty[i] == .5f);
    sk_foc(100, 0, 1, 5, duty);
    for (int i = 0; i < 3; i++)
        assert(duty[i] >= .02f && duty[i] <= .98f);
}
static void led_tests(void) {
    sk_led_config c = {.mode = SK_LED_PROGRESS, .brightness = 255, .color = 0xffffff, .accent = 0};
    sk_haptic_config h = {.mode = SK_BOUNDED, .min = 0, .max = 100, .width_deg = 6};
    sk_rgb leds[72];
    sk_led_render(&c, &h, 0, 0, 10, 0, leds);
    for (int i = 0; i < 72; i++)
        assert(leds[i].r == 0);
    sk_led_render(&c, &h, 100, 0, 10, 0, leds);
    unsigned sum = 0;
    for (int i = 0; i < 72; i++) {
        assert(leds[i].r > 0);
        sum += leds[i].r + leds[i].g + leds[i].b;
    }
    assert(sum <= 2295);
    c.mode = SK_LED_POSITION;
    c.color = 0xff0000;
    c.offset = 3;
    c.reverse = true;
    sk_led_render(&c, &h, 0, 0, 10, 0, leds);
    assert(leds[3].r > 0);
    c.accent = 0x00ff00;
    sk_led_render(&c, &h, 0, 0, 10, 100, leds);
    assert(leds[3].g > 0 && leds[3].r == 0);
}
int main(void) {
    for (unsigned dead = 40; dead <= 240; dead += 10) {
        for (unsigned i = 0; i <= 1000; i++) {
            unsigned a, b;
            sk_pwm_compare((float)i / 1000, 4000, dead, &a, &b);
            assert(a > 0 && a < b && b < 4000 && b - a == dead);
        }
    }
    const uint8_t zero_frame[3] = {0, 0, 0};
    float angle = 100;
    assert(sk_mt6701_decode(zero_frame, &angle) && angle == 0);
    const uint8_t quarter_frame[3] = {0x40, 0, 0x35};
    assert(sk_mt6701_decode(quarter_frame, &angle) && fabsf(angle - SK_PI / 2) < .0001f);
    assert(sk_mt6701_crc6(quarter_frame) == 0x35);
    /* Regression: actual Dev Kit frame with valid CRC and weak magnetic field. */
    const uint8_t weak_frame[3] = {0x48, 0xd4, 0xa4};
    assert(sk_mt6701_crc6(weak_frame) == 0x24);
    assert(sk_mt6701_decode(weak_frame, &angle));
    assert(fabsf(angle - (float)0x1235 * SK_TAU / 16384) < .0001f);
    /* Test every status combination, including push, strength warnings, and loss. */
    for (unsigned flags = 0; flags < 16; flags++) {
        uint32_t raw = flags << 6;
        uint8_t flagged[3] = {raw >> 16, raw >> 8, raw};
        flagged[2] |= sk_mt6701_crc6(flagged);
        bool usable = !(flags & 8) && (flags & 3) != 3;
        assert(sk_mt6701_decode(flagged, &angle) == usable);
    }
    for (unsigned bit = 0; bit < 24; bit++) {
        uint8_t corrupt[3];
        memcpy(corrupt, quarter_frame, 3);
        corrupt[bit / 8] ^= 1u << (bit % 8);
        assert(!sk_mt6701_decode(corrupt, &angle));
    }
    uint8_t bad_frame[3] = {0, 0, 1};
    assert(!sk_mt6701_decode(bad_frame, &angle));
    assert(sk_crc32((const uint8_t *)"123456789", 9) == 0xcbf43926);
    sk_project_default(&project);
    assert(sk_project_validate(&project));
    project.views[0].on_press = 999;
    assert(!sk_project_validate(&project));
    sk_project_default(&project);
    project.views[0].widgets[0].x = 240;
    assert(!sk_project_validate(&project));
    haptics();
    led_tests();
    sk_protocol_init(&protocol, (sk_protocol_ops){.send = send, .apply = apply, .save = save});
    command(SK_HELLO, NULL, 0);
    assert(response[8] == SK_OK);
    command(SK_SAVE, NULL, 0);
    assert(response[8] == SK_INVALID && save_count == 0);
    uint8_t begin[8] = {12};
    command(SK_BEGIN, begin, 8);
    assert(response[8] == SK_OK);
    uint8_t chunk[16] = {0};
    command(SK_CHUNK, chunk, 16);
    assert(response[8] == SK_OK);
    command(SK_COMMIT, NULL, 0);
    assert(response[8] == SK_BAD_BUNDLE && apply_count == 0);
    command(SK_SAVE, NULL, 0);
    assert(response[8] == SK_INVALID && save_count == 0);
    puts("Core: control, bounds, wraparound, LEDs, validation, upload rejection passed");
    return 0;
}
