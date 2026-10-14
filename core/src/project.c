#include "sk.h"
#include <limits.h>
#include <math.h>
#include <string.h>

uint32_t sk_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
const sk_view *sk_find_view(const sk_project *p, uint16_t id) {
    for (int i = 0; i < p->view_count; i++)
        if (p->views[i].id == id)
            return &p->views[i];
    return NULL;
}
static bool ascii(const char *s, size_t max) {
    for (size_t i = 0; i < max; i++) {
        if (!s[i])
            return true;
        if ((unsigned char)s[i] < 32 || (unsigned char)s[i] > 126)
            return false;
    }
    return false;
}
bool sk_project_validate(const sk_project *p) {
    if (!p->view_count || p->view_count > SK_MAX_VIEWS || !sk_find_view(p, p->start_view))
        return false;
    for (int i = 0; i < p->view_count; i++) {
        const sk_view *v = &p->views[i];
        if (!v->id || !v->name[0] || !ascii(v->name, sizeof(v->name)) || v->background > 0xffffff ||
            !sk_haptic_valid(&v->haptic) || v->leds.mode > SK_LED_RAINBOW ||
            v->leds.color > 0xffffff || v->leds.accent > 0xffffff ||
            v->leds.offset >= SK_LED_COUNT || v->widget_count > SK_MAX_WIDGETS ||
            (v->on_press && !sk_find_view(p, v->on_press)) ||
            (v->on_long_press && !sk_find_view(p, v->on_long_press)))
            return false;
        for (int j = 0; j < i; j++)
            if (v->id == p->views[j].id)
                return false;
        for (int j = 0; j < v->widget_count; j++) {
            const sk_widget *w = &v->widgets[j];
            if (!w->id || w->kind > SK_CIRCLE || w->x < 0 || w->y < 0 || !w->w || !w->h ||
                w->x + w->w > 240 || w->y + w->h > 240 || w->color > 0xffffff ||
                (w->font_size != 16 && w->font_size != 24 && w->font_size != 32) ||
                !ascii(w->text, sizeof(w->text)) || (w->kind == SK_CIRCLE && w->w != w->h))
                return false;
            for (int k = 0; k < j; k++)
                if (w->id == v->widgets[k].id)
                    return false;
        }
    }
    return true;
}

/* Strict, allocation-free decoder for our fixed CBOR array schema. */
typedef struct {
    const uint8_t *p, *end;
    bool ok;
} reader;
static uint8_t byte(reader *r) {
    if (r->p == r->end) {
        r->ok = false;
        return 0;
    }
    return *r->p++;
}
static uint64_t head(reader *r, uint8_t major) {
    uint8_t h = byte(r), ai = h & 31;
    if ((h >> 5) != major || ai >= 28) {
        r->ok = false;
        return 0;
    }
    if (ai < 24)
        return ai;
    uint64_t n = 0;
    for (unsigned i = 0; i < (1u << (ai - 24)); i++)
        n = (n << 8) | byte(r);
    return n;
}
static void array(reader *r, unsigned count) {
    if (head(r, 4) != count)
        r->ok = false;
}
static uint32_t uint(reader *r, uint32_t max) {
    uint64_t n = head(r, 0);
    if (n > max) {
        r->ok = false;
        return 0;
    }
    return (uint32_t)n;
}
static int32_t sint(reader *r) {
    if (r->p == r->end) {
        r->ok = false;
        return 0;
    }
    uint8_t major = *r->p >> 5;
    if (major > 1) {
        r->ok = false;
        return 0;
    }
    uint64_t n = head(r, major);
    if (n > INT32_MAX) {
        r->ok = false;
        return 0;
    }
    return major ? -1 - (int32_t)n : (int32_t)n;
}
static float real(reader *r) {
    if (byte(r) != 0xfa)
        r->ok = false;
    uint32_t bits = 0;
    for (int i = 0; i < 4; i++)
        bits = (bits << 8) | byte(r);
    float v;
    memcpy(&v, &bits, 4);
    if (!isfinite(v))
        r->ok = false;
    return v;
}
static bool boolean(reader *r) {
    uint8_t b = byte(r);
    if (b != 0xf4 && b != 0xf5)
        r->ok = false;
    return b == 0xf5;
}
static void string(reader *r, char *out, size_t capacity) {
    uint64_t n = head(r, 3);
    if (n >= capacity || n > (uint64_t)(r->end - r->p)) {
        r->ok = false;
        return;
    }
    memcpy(out, r->p, (size_t)n);
    out[n] = 0;
    /* Embedded NULs must not hide invalid text. */
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)out[i] < 32 || (unsigned char)out[i] > 126)
            r->ok = false;
    r->p += n;
}
static void haptic(reader *r, sk_haptic_config *c) {
    array(r, 10);
    c->mode = uint(r, SK_SWITCH);
    c->min = sint(r);
    c->max = sint(r);
    c->initial = sint(r);
    c->width_deg = real(r);
    c->strength = real(r);
    c->endstop = real(r);
    c->snap = real(r);
    c->damping = real(r);
    uint64_t n = head(r, 4);
    if (n > SK_MAX_DETENTS) {
        r->ok = false;
        return;
    }
    c->detent_count = n;
    for (int i = 0; i < c->detent_count; i++)
        c->detents[i] = sint(r);
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
bool sk_project_decode(const uint8_t *data, size_t len, sk_project *out) {
    if (len < 12 || len > SK_MAX_BUNDLE || memcmp(data, "SKV1", 4) || le32(data + 4) != len - 12 ||
        le32(data + 8) != sk_crc32(data + 12, len - 12))
        return false;
    memset(out, 0, sizeof(*out));
    reader r = {data + 12, data + len, true};
    array(&r, 3);
    if (uint(&r, 255) != SK_VERSION)
        return false;
    out->start_view = uint(&r, UINT16_MAX);
    uint64_t n = head(&r, 4);
    if (n > SK_MAX_VIEWS || n == 0)
        return false;
    out->view_count = n;
    for (int i = 0; i < out->view_count && r.ok; i++) {
        sk_view *v = &out->views[i];
        array(&r, 8);
        v->id = uint(&r, UINT16_MAX);
        string(&r, v->name, sizeof(v->name));
        v->background = uint(&r, 0xffffff);
        haptic(&r, &v->haptic);
        array(&r, 8);
        v->leds.mode = uint(&r, SK_LED_RAINBOW);
        v->leds.brightness = uint(&r, 255);
        v->leds.color = uint(&r, 0xffffff);
        v->leds.accent = uint(&r, 0xffffff);
        v->leds.offset = uint(&r, SK_LED_COUNT - 1);
        v->leds.reverse = boolean(&r);
        v->leds.press_flash = boolean(&r);
        v->leds.mode_flash = boolean(&r);
        v->on_press = uint(&r, UINT16_MAX);
        v->on_long_press = uint(&r, UINT16_MAX);
        n = head(&r, 4);
        if (n > SK_MAX_WIDGETS)
            return false;
        v->widget_count = n;
        for (int j = 0; j < v->widget_count && r.ok; j++) {
            sk_widget *w = &v->widgets[j];
            array(&r, 9);
            w->id = uint(&r, UINT16_MAX);
            w->kind = uint(&r, SK_CIRCLE);
            int32_t x = sint(&r), y = sint(&r);
            if (x < 0 || x > 239 || y < 0 || y > 239)
                r.ok = false;
            w->x = x;
            w->y = y;
            w->w = uint(&r, 240);
            w->h = uint(&r, 240);
            w->color = uint(&r, 0xffffff);
            w->font_size = uint(&r, 32);
            string(&r, w->text, sizeof(w->text));
        }
    }
    return r.ok && r.p == r.end && sk_project_validate(out);
}
void sk_project_default(sk_project *p) {
    memset(p, 0, sizeof(*p));
    p->start_view = 1;
    p->view_count = 6;
    const char *names[] = {"Free spin", "Fine detents", "Coarse detents",
                           "Bounded",   "Magnetic",     "Switch"};
    for (int i = 0; i < p->view_count; i++) {
        sk_view *v = &p->views[i];
        v->id = i + 1;
        strcpy(v->name, names[i]);
        v->background = 0x101820;
        v->haptic = (sk_haptic_config){.mode = SK_DETENT,
                                       .min = 0,
                                       .max = 100,
                                       .initial = 0,
                                       .width_deg = 6,
                                       .strength = .65f,
                                       .endstop = .8f,
                                       .snap = 1.1f,
                                       .damping = .5f};
        if (i == 0)
            v->haptic.mode = SK_FREE;
        if (i == 2)
            v->haptic.width_deg = 30;
        if (i == 3)
            v->haptic.mode = SK_BOUNDED;
        if (i == 4) {
            v->haptic.mode = SK_MAGNETIC;
            v->haptic.detent_count = 3;
            v->haptic.detents[1] = 5;
            v->haptic.detents[2] = 10;
        }
        if (i == 5) {
            v->haptic.mode = SK_SWITCH;
            v->haptic.max = 1;
            v->haptic.width_deg = 60;
            v->haptic.snap = .55f;
        }
        v->leds = (sk_led_config){.mode = SK_LED_POSITION,
                                  .brightness = 64,
                                  .color = 0x45d6b0,
                                  .accent = 0x050a0d,
                                  .press_flash = true,
                                  .mode_flash = true};
        v->on_press = i == 5 ? 1 : i + 2;
        v->on_long_press = 1;
        v->widget_count = 3;
        v->widgets[0] = (sk_widget){.id = 1,
                                    .kind = SK_ARC,
                                    .x = 20,
                                    .y = 20,
                                    .w = 200,
                                    .h = 200,
                                    .color = 0x45d6b0,
                                    .font_size = 16};
        v->widgets[1] = (sk_widget){.id = 2,
                                    .kind = SK_LABEL,
                                    .x = 20,
                                    .y = 70,
                                    .w = 200,
                                    .h = 30,
                                    .color = 0xe4edf5,
                                    .font_size = 24};
        strcpy(v->widgets[1].text, v->name);
        v->widgets[2] = (sk_widget){.id = 3,
                                    .kind = SK_VALUE,
                                    .x = 45,
                                    .y = 110,
                                    .w = 150,
                                    .h = 45,
                                    .color = 0xffffff,
                                    .font_size = 32};
    }
}
