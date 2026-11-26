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
    float click; /* detent tick, 0..1 of SK_VOLTAGE_LIMIT */
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
    uint16_t pulse_ticks, pulse_half;
    uint8_t tick_ticks;
    int8_t tick_dir;
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
/* The view to show after a new project is applied: the current view if it
 * still exists (live editing keeps the user where they are), otherwise the
 * project's start view. */
const sk_view *sk_view_after_upload(const sk_project *p, uint16_t current);
/* Whether two haptic configurations behave identically. */
bool sk_haptic_equal(const sk_haptic_config *a, const sk_haptic_config *b);
void sk_project_default(sk_project *p);
uint32_t sk_crc32(const uint8_t *data, size_t len);
bool sk_haptic_valid(const sk_haptic_config *c);
void sk_haptic_init(sk_haptic_state *s, const sk_haptic_config *c, float angle);
/* c with its initial position replaced by position, clamped to its bounds,
 * so reconfiguring a live view keeps the knob where it is. */
sk_haptic_config sk_haptic_with_position(const sk_haptic_config *c, int32_t position);
float sk_haptic_step(sk_haptic_state *s, float raw_angle, float dt);
void sk_haptic_pulse(sk_haptic_state *s, bool pressed, bool long_press);
void sk_led_render(const sk_led_config *c, const sk_haptic_config *h, int32_t position,
                   float sub_position, uint32_t ms, uint32_t flash_until, sk_rgb out[SK_LED_COUNT]);
/* Hard limit on requested haptic voltage. Voltage mode, no current sensing:
 * stall current is roughly this over the phase resistance. Linear up to
 * 0.96 * supply / sqrt(3) (2.77 V at 5 V). */
#define SK_VOLTAGE_LIMIT 2.5f
/* Click for bundles that predate the field: 1.0 V, as tuned on a Dev Kit. */
#define SK_DEFAULT_CLICK .4f
void sk_foc(float q, float d, float electrical_angle, float supply, float duty[3]);
enum sk_mt6701_status { SK_MT6701_OK, SK_MT6701_CRC, SK_MT6701_LOSS, SK_MT6701_RESERVED };
/* Decodes the angle for OK and LOSS frames; CRC/RESERVED leave it unchanged. */
enum sk_mt6701_status sk_mt6701_parse(const uint8_t bytes[3], float *angle);
/* Strict decode: only frames without tracking loss or reserved status. */
bool sk_mt6701_decode(const uint8_t bytes[3], float *angle);
uint8_t sk_mt6701_crc6(const uint8_t bytes[3]);
typedef struct {
    uint32_t bad_since_ms;
    bool rejecting;
} sk_encoder_health;
/* Every invalid sample must gate the motor off; only sustained rejection latches. */
#define SK_ENCODER_LATCH_MS 20u
bool sk_encoder_should_latch(sk_encoder_health *health, bool valid, uint32_t now_ms);
typedef struct {
    float last;
    uint32_t clean_ms;
    bool have;
} sk_encoder_track;
/* Largest angle change per read accepted from a tracking-loss frame (rad). */
#define SK_ENCODER_MAX_STEP .1f
/* SK_LOSS_CONTINUOUS also requires a clean frame within this window. */
#define SK_ENCODER_LOSS_WINDOW_MS 200u
enum sk_loss_policy {
    SK_LOSS_REJECT,     /* every tracking-loss frame is rejected */
    SK_LOSS_CONTINUOUS, /* accepted if continuous and a clean frame was recent */
    SK_LOSS_ACCEPT      /* accepted if continuous (factory firmware ignores the flag) */
};
bool sk_encoder_accept(sk_encoder_track *t, enum sk_mt6701_status status, float angle,
                       uint32_t now_ms, enum sk_loss_policy policy);
void sk_pwm_compare(float duty, unsigned peak, unsigned dead, unsigned *a, unsigned *b);
#ifdef __cplusplus
}
#endif
#endif
