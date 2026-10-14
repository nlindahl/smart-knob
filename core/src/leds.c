#include "sk.h"
#include <math.h>
static sk_rgb rgb(uint32_t c, unsigned b) {
    return (sk_rgb){((c >> 16) & 255) * b / 255, ((c >> 8) & 255) * b / 255, (c & 255) * b / 255};
}
static uint32_t wheel(unsigned hue) {
    hue %= 768;
    if (hue < 256)
        return ((255 - hue) << 16) | (hue << 8);
    if (hue < 512)
        return ((511 - hue) << 8) | (hue - 256);
    return ((hue - 512) << 16) | (767 - hue);
}
void sk_led_render(const sk_led_config *c, const sk_haptic_config *h, int32_t pos, float sub,
                   uint32_t ms, uint32_t flash_until, sk_rgb out[SK_LED_COUNT]) {
    if (!isfinite(sub))
        sub = 0;
    float fraction;
    if (h->max > h->min && (h->mode == SK_BOUNDED || h->mode == SK_SWITCH)) {
        fraction = ((float)pos + sub - h->min) / (h->max - h->min);
        fraction = fminf(fmaxf(fraction, 0), 1);
    } else {
        fraction = fmodf(((float)pos + sub) * h->width_deg / 360, 1);
        if (fraction < 0)
            fraction += 1;
    }
    int marker = (int)lroundf(fraction * (SK_LED_COUNT - 1));
    bool flash = (int32_t)(flash_until - ms) > 0;
    uint32_t total = 0;
    for (int i = 0; i < SK_LED_COUNT; i++) {
        uint32_t color = 0;
        switch (c->mode) {
        case SK_LED_SOLID:
            color = c->color;
            break;
        case SK_LED_POSITION:
            color = i == marker ? c->color : c->accent;
            break;
        case SK_LED_PROGRESS:
            color = (float)i / SK_LED_COUNT < fraction ? c->color : c->accent;
            break;
        case SK_LED_RAINBOW:
            color = wheel((unsigned)i * 768 / SK_LED_COUNT + ms / 10);
            break;
        default:
            break;
        }
        if (flash)
            color = c->accent;
        int physical = (c->offset + (c->reverse ? SK_LED_COUNT - i : i)) % SK_LED_COUNT;
        out[physical] = rgb(color, c->brightness);
        total += out[physical].r + out[physical].g + out[physical].b;
    }
    /* Budget roughly 180 mA at 20 mA/channel; full-white rings are scaled. */
    const uint32_t budget = 2295;
    if (total > budget)
        for (int i = 0; i < SK_LED_COUNT; i++) {
            out[i].r = (uint32_t)out[i].r * budget / total;
            out[i].g = (uint32_t)out[i].g * budget / total;
            out[i].b = (uint32_t)out[i].b * budget / total;
        }
}
