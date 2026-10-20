#ifndef SK_H
#define SK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SK_VERSION 1
#define SK_MAX_VIEWS 8
#define SK_MAX_WIDGETS 16
#define SK_MAX_DETENTS 16
#define SK_MAX_BUNDLE 24576
#define SK_LED_COUNT 72
#define SK_PI 3.14159265358979323846f
#define SK_TAU (2.0f * SK_PI)
enum sk_mode { SK_FREE, SK_DETENT, SK_BOUNDED, SK_MAGNETIC, SK_SWITCH };
enum sk_widget_kind { SK_LABEL, SK_VALUE, SK_ARC, SK_BAR, SK_RECT, SK_CIRCLE };
enum sk_led_mode { SK_LED_OFF, SK_LED_SOLID, SK_LED_POSITION, SK_LED_PROGRESS, SK_LED_RAINBOW };
typedef struct {
    uint8_t mode;
    int32_t min, max, initial;
    float width_deg, strength, endstop, snap, damping;
    uint8_t detent_count;
    int32_t detents[SK_MAX_DETENTS];
} sk_haptic_config;
typedef struct {
    uint8_t mode, brightness;
    uint32_t color, accent;
    uint8_t offset;
    bool reverse, press_flash, mode_flash;
} sk_led_config;
typedef struct {
    uint16_t id;
    uint8_t kind;
    int16_t x, y;
    uint16_t w, h;
    uint32_t color;
    uint8_t font_size;
    char text[64];
} sk_widget;
typedef struct {
    uint16_t id;
    char name[32];
    uint32_t background;
    sk_haptic_config haptic;
    sk_led_config leds;
    uint16_t on_press, on_long_press; /* 0 = stay */
    uint8_t widget_count;
    sk_widget widgets[SK_MAX_WIDGETS];
} sk_view;
typedef struct {
    uint16_t start_view;
    uint8_t view_count;
    sk_view views[SK_MAX_VIEWS];
} sk_project;
typedef struct {
    sk_haptic_config config;
    float center, previous_angle, unwrapped, velocity, sub_position, voltage;
    int32_t position;
    uint16_t pulse_ticks;
    float pulse_strength;
    bool initialized;
} sk_haptic_state;
typedef struct {
    uint8_t r, g, b;
} sk_rgb;
typedef struct {
    float zero_electrical;
    uint8_t pole_pairs;
    int8_t direction;
    bool valid;
} sk_calibration;
bool sk_project_decode(const uint8_t *data, size_t len, sk_project *out);
bool sk_project_validate(const sk_project *p);
const sk_view *sk_find_view(const sk_project *p, uint16_t id);
void sk_project_default(sk_project *p);
uint32_t sk_crc32(const uint8_t *data, size_t len);
bool sk_haptic_valid(const sk_haptic_config *c);
void sk_haptic_init(sk_haptic_state *s, const sk_haptic_config *c, float angle);
float sk_haptic_step(sk_haptic_state *s, float raw_angle, float dt);
void sk_haptic_pulse(sk_haptic_state *s, bool pressed, bool long_press);
void sk_led_render(const sk_led_config *c, const sk_haptic_config *h, int32_t position,
                   float sub_position, uint32_t ms, uint32_t flash_until, sk_rgb out[SK_LED_COUNT]);
void sk_foc(float q, float d, float electrical_angle, float supply, float duty[3]);
bool sk_mt6701_decode(const uint8_t bytes[3], float *angle);
uint8_t sk_mt6701_crc6(const uint8_t bytes[3]);
typedef struct {
    uint32_t bad_since_ms;
    bool rejecting;
} sk_encoder_health;
/* Every invalid sample must gate the motor off; only sustained rejection latches. */
#define SK_ENCODER_LATCH_MS 20u
bool sk_encoder_should_latch(sk_encoder_health *health, bool valid, uint32_t now_ms);
void sk_pwm_compare(float duty, unsigned peak, unsigned dead, unsigned *a, unsigned *b);
#ifdef __cplusplus
}
#endif
#endif
