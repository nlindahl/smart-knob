#include "sk.h"
#include "sk_render.h"
#include <math.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif
static sk_project project, staging;
static sk_renderer renderer;
static sk_haptic_state simulation;
static uint8_t pixels[240 * 240 * 4], draw[240 * 32 * 4], bundle[SK_MAX_BUNDLE];
static sk_rgb leds[SK_LED_COUNT];
static size_t bundle_len;
static uint16_t view_id;
static float input_angle;
static float sim_result[3];
static bool initialized;
static uint32_t now;
static uint32_t ticks(void) {
    return now;
}
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *map) {
    for (int y = area->y1; y <= area->y2; y++)
        for (int x = area->x1; x <= area->x2; x++) {
            if (x >= 0 && x < 240 && y >= 0 && y < 240) {
                size_t offset = (y * 240 + x) * 4;
                pixels[offset] = map[2];
                pixels[offset + 1] = map[1];
                pixels[offset + 2] = map[0];
                int dx = 2 * x - 239, dy = 2 * y - 239;
                pixels[offset + 3] = dx * dx + dy * dy <= 240 * 240 ? 255 : 0;
            }
            map += 4;
        }
    lv_display_flush_ready(display);
}
EXPORT void sk_preview_init(void) {
    if (initialized)
        return;
    lv_init();
    lv_tick_set_cb(ticks);
    lv_display_t *display = lv_display_create(240, 240);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display, draw, NULL, sizeof(draw), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    sk_project_default(&project);
    initialized = true;
}
EXPORT int sk_preview_load(const uint8_t *data, size_t len) {
    sk_preview_init();
    if (len == bundle_len && !memcmp(data, bundle, len))
        return 1;
    if (!sk_project_decode(data, len, &staging))
        return 0;
    project = staging;
    memcpy(bundle, data, len);
    bundle_len = len;
    view_id = 0;
    return 1;
}
EXPORT int sk_preview_select(uint16_t id) {
    const sk_view *v = sk_find_view(&project, id);
    if (!v)
        return 0;
    if (view_id != id) {
        view_id = id;
        sk_render_view(&renderer, v);
        sk_haptic_init(&simulation, &v->haptic, input_angle);
    }
    return 1;
}
EXPORT void sk_preview_reset(void) {
    input_angle = 0;
    view_id = 0;
}
EXPORT const float *sk_preview_step(float angle, int pressed, int long_press) {
    if (!isfinite(angle) || !simulation.initialized)
        return sim_result;
    if (pressed)
        sk_haptic_pulse(&simulation, true, long_press != 0);
    float delta = angle - input_angle;
    int steps = (int)ceilf(fabsf(delta) / .02f);
    if (steps < 1)
        steps = 1;
    if (steps > 4096)
        steps = 4096;
    for (int i = 1; i <= steps; i++)
        sk_haptic_step(&simulation, input_angle + delta * (float)i / steps, .001f);
    input_angle = angle;
    sim_result[0] = simulation.position;
    sim_result[1] = simulation.sub_position;
    sim_result[2] = simulation.voltage;
    return sim_result;
}
EXPORT const uint8_t *sk_preview_render(int32_t position, float sub, uint32_t ms, uint32_t flash) {
    now = ms;
    sk_render_value(&renderer, position, sub);
    lv_refr_now(NULL);
    if (renderer.view)
        sk_led_render(&renderer.view->leds, &renderer.view->haptic, position, sub, ms, flash, leds);
    return pixels;
}
EXPORT const uint8_t *sk_preview_leds(void) {
    return (const uint8_t *)leds;
}
