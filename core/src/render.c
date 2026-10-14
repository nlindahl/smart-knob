#include "sk_render.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static const lv_font_t *font(uint8_t size) {
    return size == 32   ? &lv_font_montserrat_32
           : size == 24 ? &lv_font_montserrat_24
                        : &lv_font_montserrat_16;
}
void sk_render_view(sk_renderer *r, const sk_view *v) {
    lv_obj_t *previous = r->screen;
    memset(r, 0, sizeof(*r));
    r->view = v;
    r->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(r->screen, lv_color_hex(v->background), 0);
    lv_obj_set_style_bg_opa(r->screen, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(r->screen, false);
    for (int i = 0; i < v->widget_count; i++) {
        const sk_widget *w = &v->widgets[i];
        lv_obj_t *o;
        if (w->kind == SK_LABEL || w->kind == SK_VALUE) {
            o = lv_label_create(r->screen);
            lv_label_set_text(o, w->kind == SK_LABEL ? w->text : "0");
            lv_label_set_long_mode(o, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_font(o, font(w->font_size), 0);
            lv_obj_set_style_text_color(o, lv_color_hex(w->color), 0);
            lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
        } else if (w->kind == SK_ARC) {
            o = lv_arc_create(r->screen);
            lv_arc_set_rotation(o, 135);
            lv_arc_set_bg_angles(o, 0, 270);
            lv_arc_set_range(o, 0, 1000);
            lv_obj_remove_style(o, NULL, LV_PART_KNOB);
            lv_obj_set_style_arc_color(o, lv_color_hex(w->color), LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(o, lv_color_hex(0x25323e), LV_PART_MAIN);
            lv_obj_set_style_arc_width(o, 8, LV_PART_MAIN);
            lv_obj_set_style_arc_width(o, 8, LV_PART_INDICATOR);
        } else if (w->kind == SK_BAR) {
            o = lv_bar_create(r->screen);
            lv_bar_set_range(o, 0, 1000);
            lv_obj_set_style_bg_color(o, lv_color_hex(w->color), LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(o, lv_color_hex(0x25323e), LV_PART_MAIN);
        } else {
            o = lv_obj_create(r->screen);
            lv_obj_remove_style_all(o);
            lv_obj_set_style_bg_color(o, lv_color_hex(w->color), 0);
            lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(o, w->kind == SK_CIRCLE ? LV_RADIUS_CIRCLE : 4, 0);
        }
        lv_obj_set_pos(o, w->x, w->y);
        lv_obj_set_size(o, w->w, w->h);
        lv_obj_set_clickable(o, false);
        lv_obj_set_scrollable(o, false);
        r->widgets[i] = o;
    }
    lv_screen_load(r->screen);
    if (previous)
        lv_obj_delete(previous);
    sk_render_value(r, v->haptic.initial, 0);
}
void sk_render_value(sk_renderer *r, int32_t position, float sub) {
    if (!r->view)
        return;
    const sk_haptic_config *c = &r->view->haptic;
    float fraction;
    if ((c->mode == SK_BOUNDED || c->mode == SK_SWITCH) && c->max > c->min)
        fraction = ((float)position + sub - c->min) / (c->max - c->min);
    else {
        fraction = fmodf(((float)position + sub) * c->width_deg / 360, 1);
        if (fraction < 0)
            fraction += 1;
    }
    int progress = (int)(fminf(fmaxf(fraction, 0), 1) * 1000);
    for (int i = 0; i < r->view->widget_count; i++) {
        const sk_widget *w = &r->view->widgets[i];
        if (w->kind == SK_VALUE) {
            char text[96];
            snprintf(text, sizeof(text), "%ld%s", (long)position, w->text);
            lv_label_set_text(r->widgets[i], text);
        } else if (w->kind == SK_ARC)
            lv_arc_set_value(r->widgets[i], progress);
        else if (w->kind == SK_BAR)
            lv_bar_set_value(r->widgets[i], progress, LV_ANIM_OFF);
    }
}
