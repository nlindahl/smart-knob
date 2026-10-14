#include "sk.h"
#include <limits.h>
#include <math.h>
#include <string.h>
static float clamp(float v, float lo, float hi) {
    return fminf(fmaxf(v, lo), hi);
}
void sk_pwm_compare(float duty, unsigned peak, unsigned dead, unsigned *a, unsigned *b) {
    unsigned center = (unsigned)(clamp(duty, .02f, .98f) * peak);
    if (center <= dead / 2)
        center = dead / 2 + 1;
    if (center + (dead + 1) / 2 >= peak)
        center = peak - (dead + 1) / 2 - 1;
    *a = center - dead / 2;
    *b = center + (dead + 1) / 2;
}
bool sk_haptic_valid(const sk_haptic_config *c) {
    if (c->mode > SK_SWITCH || !isfinite(c->width_deg) || c->width_deg < 1 || c->width_deg > 180 ||
        !isfinite(c->strength) || c->strength < 0 || c->strength > 1 || !isfinite(c->endstop) ||
        c->endstop < 0 || c->endstop > 1 || !isfinite(c->snap) || c->snap < .5f || c->snap > 1.5f ||
        !isfinite(c->damping) || c->damping < 0 || c->damping > 1 ||
        c->detent_count > SK_MAX_DETENTS || c->min < -1000000 || c->max > 1000000 ||
        c->initial < -1000000 || c->initial > 1000000)
        return false;
    if ((c->mode == SK_BOUNDED || c->mode == SK_SWITCH) &&
        (c->min > c->max || c->initial < c->min || c->initial > c->max))
        return false;
    if (c->mode == SK_SWITCH && c->max - c->min != 1)
        return false;
    if (c->mode == SK_MAGNETIC && !c->detent_count)
        return false;
    for (int i = 0; i < c->detent_count; i++) {
        if (c->detents[i] < -1000000 || c->detents[i] > 1000000)
            return false;
        for (int j = 0; j < i; j++)
            if (c->detents[i] == c->detents[j])
                return false;
    }
    return true;
}
void sk_haptic_init(sk_haptic_state *s, const sk_haptic_config *c, float angle) {
    sk_haptic_config copy = *c;
    memset(s, 0, sizeof(*s));
    if (!sk_haptic_valid(&copy) || !isfinite(angle))
        return;
    s->config = copy;
    s->position = copy.initial;
    s->center = s->unwrapped = s->previous_angle = angle;
    s->initialized = true;
}
void sk_haptic_pulse(sk_haptic_state *s, bool pressed, bool long_press) {
    s->pulse_ticks = long_press ? 12 : 6;
    s->pulse_strength = long_press ? 1.2f : (pressed ? .7f : .35f);
}
float sk_haptic_step(sk_haptic_state *s, float angle, float dt) {
    if (!s->initialized || !isfinite(angle) || !isfinite(dt) || dt <= 0 || dt > .02f) {
        s->voltage = 0;
        s->pulse_ticks = 0;
        return 0;
    }
    float delta = remainderf(angle - s->previous_angle, SK_TAU);
    s->previous_angle = angle;
    s->unwrapped += delta;
    s->velocity += .15f * (delta / dt - s->velocity);
    /* Rebase absolute angles to avoid float precision loss after many turns. */
    if (fabsf(s->unwrapped) > 1000) {
        float offset = truncf(s->unwrapped / SK_TAU) * SK_TAU;
        s->unwrapped -= offset;
        s->center -= offset;
    }
    const sk_haptic_config *c = &s->config;
    float width = c->width_deg * SK_PI / 180;
    float error = s->unwrapped - s->center;
    bool bounded = c->mode == SK_BOUNDED || c->mode == SK_SWITCH;
    /* Handle skipped detents in a bounded amount of work. */
    float steps_f = error > width * c->snap    ? ceilf(error / width - c->snap)
                    : error < -width * c->snap ? floorf(error / width + c->snap)
                                               : 0;
    int32_t steps = (int32_t)clamp(steps_f, -1000, 1000);
    int64_t next = (int64_t)s->position + steps;
    int32_t lo = bounded ? c->min : -1000000;
    int32_t hi = bounded ? c->max : 1000000;
    if (next < lo)
        next = lo;
    if (next > hi)
        next = hi;
    s->center += (float)(next - s->position) * width;
    s->position = (int32_t)next;
    error = s->unwrapped - s->center;
    s->sub_position = error / width;
    bool at_stop =
        bounded && ((s->position == c->min && error < 0) || (s->position == c->max && error > 0));
    bool magnetic = c->mode != SK_MAGNETIC;
    if (c->mode == SK_MAGNETIC) {
        for (int i = 0; i < c->detent_count; i++)
            if (c->detents[i] == s->position)
                magnetic = true;
    }
    float strength = at_stop ? c->endstop : c->strength;
    float dead = at_stop ? 0 : fminf(width * .2f, SK_PI / 180);
    float active_error = error - clamp(error, -dead, dead);
    float q = -4 * strength * active_error;
    if (!at_stop && (c->mode == SK_FREE || !magnetic))
        q = 0;
    else
        q -= c->damping * .03f * s->velocity;
    if (s->pulse_ticks) {
        uint16_t midpoint = s->pulse_strength > 1 ? 6 : 3;
        q += s->pulse_ticks > midpoint ? s->pulse_strength : -s->pulse_strength;
        s->pulse_ticks--;
    }
    if (fabsf(s->velocity) > 60)
        q = 0;
    /* Voltage-mode FOC, no current sensing on the kit. Keep a hard limit. */
    float target = clamp(q, -2, 2);
    s->voltage += clamp(target - s->voltage, -1000 * dt, 1000 * dt);
    return s->voltage;
}
void sk_foc(float q, float d, float a, float supply, float duty[3]) {
    if (!isfinite(q) || !isfinite(d) || !isfinite(a) || !isfinite(supply) || supply <= 0) {
        duty[0] = duty[1] = duty[2] = .5f;
        return;
    }
    float alpha = d * cosf(a) - q * sinf(a);
    float beta = d * sinf(a) + q * cosf(a);
    float phases[3] = {alpha, -.5f * alpha + .8660254038f * beta,
                       -.5f * alpha - .8660254038f * beta};
    for (int i = 0; i < 3; i++)
        duty[i] = clamp(.5f + phases[i] / supply, .02f, .98f);
}
