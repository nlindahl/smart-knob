#ifndef SK_RENDER_H
#define SK_RENDER_H
#include "sk.h"
#include <lvgl.h>
typedef struct {
    const sk_view *view;
    lv_obj_t *screen;
    lv_obj_t *widgets[SK_MAX_WIDGETS];
} sk_renderer;
void sk_render_view(sk_renderer *r, const sk_view *v);
void sk_render_value(sk_renderer *r, int32_t position, float sub);
#endif
