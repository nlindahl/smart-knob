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
        !isfinite(c->damping) || c->damping < 0 || c->damping > 1 || !isfinite(c->click) ||
        c->click < 0 || c->click > 1 || c->detent_count > SK_MAX_DETENTS || c->min < -1000000 ||
        c->max > 1000000 || c->initial < -1000000 || c->initial > 1000000)
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
sk_haptic_config sk_haptic_with_position(const sk_haptic_config *c, int32_t position) {
    sk_haptic_config out = *c;
    int32_t lo = -1000000, hi = 1000000;
    if (c->mode == SK_BOUNDED || c->mode == SK_SWITCH) {
        lo = c->min;
        hi = c->max;
    }
    out.initial = position < lo ? lo : position > hi ? hi : position;
    return out;
}
void sk_haptic_pulse(sk_haptic_state *s, bool pressed, bool long_press) {
    /* Factory firmware plays ±5 V (press), ±1.5 V (release) and ±20 V (long),
     * all clipped by its 5 V limit; ours are clipped by SK_VOLTAGE_LIMIT. */
    s->pulse_ticks = long_press ? 12 : 6;
    s->pulse_half = s->pulse_ticks / 2;
    s->pulse_strength = long_press ? SK_VOLTAGE_LIMIT : (pressed ? SK_VOLTAGE_LIMIT : 1.5f);
}
#define TICK_HALF 2 /* control periods per tick half */
/* Detent "click" gain in V·s/rad, as in the factory firmware: fine detents
 * need more derivative action because their spring torque is small. */
static float click_gain(const sk_haptic_config *c) {
    const float lo = 3, hi = 8, at_lo = .08f, at_hi = .02f;
    float t = clamp((c->width_deg - lo) / (hi - lo), 0, 1);
    return c->strength * (at_lo + (at_hi - at_lo) * t);
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
    bool moved = next != s->position;
    int8_t moved_dir = next > s->position ? 1 : -1;
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
    else {
        /* Magnetic detents skip the click term, which would add clicks between them. */
        float d = c->mode == SK_MAGNETIC ? 0 : click_gain(c);
        q -= (d + c->damping * .03f) * s->velocity;
    }
    /* A short tick each time the knob enters a detent: a push along the
     * motion, then against it. The spring alone only fades out at the snap
     * point, which feels soft at fine spacing. Magnetic mode ticks only on
     * its detents; free mode never ticks. */
    if (moved && c->mode != SK_FREE && magnetic && c->click > 0) {
        s->tick_ticks = 2 * TICK_HALF;
        s->tick_dir = moved_dir;
    }
    if (s->tick_ticks) {
        float amplitude = c->click * SK_VOLTAGE_LIMIT * s->tick_dir;
        q += s->tick_ticks > TICK_HALF ? amplitude : -amplitude;
        s->tick_ticks--;
    }
    if (s->pulse_ticks) {
        q += s->pulse_ticks > s->pulse_half ? s->pulse_strength : -s->pulse_strength;
        s->pulse_ticks--;
    }
    if (fabsf(s->velocity) > 60)
        q = 0;
    /* Voltage-mode FOC, no current sensing on the kit. Keep a hard limit. */
    float target = clamp(q, -SK_VOLTAGE_LIMIT, SK_VOLTAGE_LIMIT);
    /* 10 V/ms slew, as the factory PID output ramp; fast enough for 3 ms pulses. */
    s->voltage += clamp(target - s->voltage, -10000 * dt, 10000 * dt);
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
    /* Center the min/max of the three phases (zero-sequence injection). The
     * star point floats, so this changes no line voltage but extends the
     * linear range from supply/2 to supply/sqrt(3), like SVPWM. */
    float hi = fmaxf(phases[0], fmaxf(phases[1], phases[2]));
    float lo = fminf(phases[0], fminf(phases[1], phases[2]));
    float shift = (hi + lo) / 2;
    for (int i = 0; i < 3; i++)
        duty[i] = clamp(.5f + (phases[i] - shift) / supply, .02f, .98f);
}
