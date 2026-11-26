/* Closed-loop simulation of the SmartKnob motor controller.
 *
 * Runs the same sk_motor controller the firmware uses against a model of a
 * surface-mount PMSM gimbal motor on a voltage-mode inverter with a 14-bit
 * absolute encoder. Pole pairs, winding direction, electrical offset, encoder
 * direction and mounting offset are unknown to the controller, so these tests
 * check calibration and every sign in the torque path, which cannot be
 * established by unit tests of the individual functions. */
#include "sk_motor.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    /* Hidden plant parameters. */
    int pole_pairs, winding, encoder_dir;
    float electrical_offset, encoder_offset;
    float resistance, flux, inertia, viscous, coulomb, cogging;
    int cogging_periods;
    /* State. */
    float theta, omega;
    float external; /* torque applied by the user's hand, N·m */
    uint32_t seed;
} plant;

static void plant_default(plant *p, int pairs, int winding, int encoder_dir, float e_off,
                          float enc_off) {
    *p = (plant){.pole_pairs = pairs,
                 .winding = winding,
                 .encoder_dir = encoder_dir,
                 .electrical_offset = e_off,
                 .encoder_offset = enc_off,
                 .resistance = 5,
                 .flux = .004f, /* Kt = 1.5 * p * flux ≈ 0.04 N·m/A for 7 pole pairs */
                 .inertia = 1e-5f,
                 .viscous = 2e-5f,
                 .coulomb = 4e-4f,
                 .cogging = 1e-3f,
                 .cogging_periods = 84,
                 .seed = 12345};
}
static float noise(plant *p) {
    p->seed = p->seed * 1103515245u + 12345u;
    return ((float)((p->seed >> 16) & 0x7fff) / 32767.0f - .5f);
}
static float encoder(plant *p) {
    float a = p->encoder_dir * p->theta + p->encoder_offset;
    a = fmodf(a, SK_TAU);
    if (a < 0)
        a += SK_TAU;
    /* 14-bit quantization with half-LSB noise. */
    float lsb = SK_TAU / 16384;
    int code = (int)floorf(a / lsb + noise(p)) & 16383;
    return code * lsb;
}
static float electrical(const plant *p) {
    return p->winding * p->pole_pairs * p->theta + p->electrical_offset;
}
/* Advance one 1 ms control period with zero-order-hold duties. */
static void plant_step(plant *p, const sk_motor_output *out, float supply) {
    const int sub = 50;
    const float h = .001f / sub;
    for (int i = 0; i < sub; i++) {
        float torque = 0;
        if (out->drive) {
            float mean = (out->duty[0] + out->duty[1] + out->duty[2]) / 3;
            float va = (out->duty[0] - mean) * supply;
            float vb = (out->duty[1] - mean) * supply;
            float vc = (out->duty[2] - mean) * supply;
            float alpha = (2.0f / 3) * (va - .5f * vb - .5f * vc);
            float beta = (vb - vc) / sqrtf(3);
            float e = electrical(p);
            float vq = -alpha * sinf(e) + beta * cosf(e);
            float we = p->winding * p->pole_pairs * p->omega;
            float iq = (vq - p->flux * we) / p->resistance;
            torque = p->winding * 1.5f * p->pole_pairs * p->flux * iq;
        }
        torque -= p->cogging * sinf(p->cogging_periods * p->theta);
        torque -= p->viscous * p->omega;
        torque += p->external;
        /* Smooth Coulomb friction model. */
        torque -= p->coulomb * tanhf(p->omega / .02f);
        p->omega += torque / p->inertia * h;
        p->theta += p->omega * h;
    }
}
/* Matches the firmware defaults (CONFIG_SK_MOTOR_SUPPLY_MV / _CALIBRATION_MV). */
static const sk_motor_limits limits = {.supply = 5, .calibration_voltage = 2};
static const sk_haptic_config free_config = {
    .mode = SK_FREE, .width_deg = 6, .snap = 1.1f, .endstop = .8f};

static float run(sk_motor *m, plant *p, int ticks) {
    float max_speed = 0;
    for (int i = 0; i < ticks; i++) {
        sk_motor_output out;
        sk_motor_step(m, encoder(p), .001f, &out);
        plant_step(p, &out, m->limits.supply);
        max_speed = fmaxf(max_speed, fabsf(p->omega));
    }
    return max_speed;
}
static bool calibrate(sk_motor *m, plant *p) {
    sk_motor_init(m, &limits, NULL, &free_config, encoder(p));
    sk_motor_calibrate(m, encoder(p));
    for (int i = 0; i < 20000 && sk_motor_calibrating(m); i++)
        run(m, p, 1);
    return m->calibration.valid;
}
/* The controller's electrical angle must match the plant's at every position. */
static float electrical_error(const sk_motor *m, plant *p) {
    float worst = 0, saved = p->theta;
    for (int i = 0; i < 64; i++) {
        p->theta = saved + i * .37f;
        float raw = encoder(p);
        float est = raw * m->calibration.pole_pairs * m->calibration.direction -
                    m->calibration.zero_electrical;
        worst = fmaxf(worst, fabsf(remainderf(est - electrical(p), SK_TAU)));
    }
    p->theta = saved;
    return worst;
}
static void calibration_matrix(void) {
    const int pairs[] = {7, 11};
    const float offsets[] = {0, 1.3f, -2.9f};
    int cases = 0;
    for (unsigned pi = 0; pi < 2; pi++)
        for (int winding = -1; winding <= 1; winding += 2)
            for (int dir = -1; dir <= 1; dir += 2)
                for (unsigned oi = 0; oi < 3; oi++) {
                    plant p;
                    plant_default(&p, pairs[pi], winding, dir, offsets[oi], 2.1f * oi + .4f);
                    p.theta = .3f * oi;
                    sk_motor m;
                    bool ok = calibrate(&m, &p);
                    float err = ok ? electrical_error(&m, &p) : NAN;
                    if (!ok || m.calibration.pole_pairs != pairs[pi] || !(err < .2f)) {
                        fprintf(stderr,
                                "calibration failed: pairs=%d winding=%d dir=%d off=%g -> "
                                "result=%s measured=%.3f pp=%d err=%.3f\n",
                                pairs[pi], winding, dir, offsets[oi],
                                sk_calibration_result_name(m.result), m.measured_pairs,
                                m.calibration.pole_pairs, err);
                        exit(1);
                    }
                    cases++;
                }
    printf("calibration: %d plant variants recovered\n", cases);
}
/* Heavy cogging and friction lag the rotor behind the vector; averaging the
 * forward and reverse sweeps must still find electrical zero precisely. */
static void calibration_heavy_load(void) {
    for (int winding = -1; winding <= 1; winding += 2) {
        plant p;
        plant_default(&p, 7, winding, -1, 2.2f, .9f);
        p.cogging = 6e-3f;
        p.coulomb = 4e-3f;
        sk_motor m;
        assert(calibrate(&m, &p));
        float err = electrical_error(&m, &p);
        printf("calibration under load: zero error %.3f rad, sweep hysteresis %.2f rad\n", err,
               m.hysteresis);
        assert(err < .05f && m.hysteresis > .2f);
    }
}
/* A rotor that cannot move must fail calibration and leave the gates off. */
static void calibration_stalled(void) {
    plant p;
    plant_default(&p, 7, 1, 1, 0, 0);
    p.inertia = 1e3f; /* jammed */
    sk_motor m;
    assert(!calibrate(&m, &p));
    assert(m.result == SK_CAL_NO_MOTION && !m.enabled);
    /* A calibration loaded at boot reports as stored, not as never run. */
    sk_calibration stored = {.pole_pairs = 7, .direction = -1, .valid = true};
    sk_motor_init(&m, &limits, &stored, &free_config, 0);
    assert(m.result == SK_CAL_STORED && m.enabled);
    assert(!strcmp(sk_calibration_result_name(m.result), "stored"));
    sk_motor_init(&m, &limits, NULL, &free_config, 0);
    assert(m.result == SK_CAL_NONE && !m.enabled);
    sk_motor_output out;
    sk_motor_step(&m, encoder(&p), .001f, &out);
    assert(!out.drive);
}
static void calibrated(sk_motor *m, plant *p) {
    plant_default(p, 7, -1, 1, .8f, 1.7f);
    assert(calibrate(m, p));
    run(m, p, 200);
}
/* A displaced knob must return to its detent and come to rest. */
static void detent_restores(void) {
    sk_motor m;
    plant p;
    calibrated(&m, &p);
    sk_haptic_config c = {.mode = SK_DETENT,
                          .width_deg = 12,
                          .strength = 1,
                          .endstop = 1,
                          .snap = 1.1f,
                          .click = SK_DEFAULT_CLICK};
    sk_motor_configure(&m, &c, encoder(&p));
    float rest = p.theta;
    for (int sign = -1; sign <= 1; sign += 2) {
        p.theta = rest + sign * .6f * 12 * SK_PI / 180;
        p.omega = 0;
        run(&m, &p, 1000);
        float tail = run(&m, &p, 200);
        float error = fabsf(p.theta - rest);
        printf("detent: displaced %+d -> error %.2f deg, tail speed %.3f rad/s\n", sign,
               error * 180 / SK_PI, tail);
        assert(m.haptic.position == 0);
        assert(error < 3 * SK_PI / 180);
        assert(tail < .5f);
    }
}
/* Pushing past an end stop must be resisted, not amplified. */
static void endstop_resists(void) {
    sk_motor m;
    plant p;
    calibrated(&m, &p);
    sk_haptic_config c = {.mode = SK_BOUNDED,
                          .min = 0,
                          .max = 0,
                          .width_deg = 10,
                          .strength = 1,
                          .endstop = 1,
                          .snap = 1.1f};
    sk_motor_configure(&m, &c, encoder(&p));
    float rest = p.theta;
    for (int sign = -1; sign <= 1; sign += 2) {
        p.theta = rest;
        p.omega = 0;
        p.external = sign * 4e-3f;
        run(&m, &p, 800);
        float held = fabsf(p.theta - rest);
        p.external = 0;
        run(&m, &p, 800);
        float back = fabsf(p.theta - rest);
        printf("endstop: push %+d held at %.1f deg, released to %.1f deg\n", sign,
               held * 180 / SK_PI, back * 180 / SK_PI);
        assert(held < 20 * SK_PI / 180);
        assert(back < 4 * SK_PI / 180);
    }
    /* Same push without feedback spins freely: the test discriminates. */
    sk_motor_stop(&m);
    p.theta = rest;
    p.omega = 0;
    p.external = 4e-3f;
    run(&m, &p, 800);
    assert(fabsf(p.theta - rest) > SK_PI);
}
/* Turning the knob through detents must count positions and stay stable. */
static void detents_count(void) {
    sk_motor m;
    plant p;
    calibrated(&m, &p);
    sk_haptic_config c = {.mode = SK_DETENT,
                          .width_deg = 10,
                          .strength = 1,
                          .endstop = 1,
                          .snap = 1.1f,
                          .click = SK_DEFAULT_CLICK};
    sk_motor_configure(&m, &c, encoder(&p));
    float start = p.theta;
    /* A finger modeled as a stiff spring dragged at 3 rad/s for 1 s. */
    float finger = start;
    int direction = m.calibration.direction; /* sign mapping is checked below */
    (void)direction;
    for (int i = 0; i < 1000; i++) {
        finger += .003f;
        p.external = .05f * (finger - p.theta) - 2e-4f * p.omega;
        run(&m, &p, 1);
    }
    p.external = 0;
    run(&m, &p, 800);
    float turned = p.theta - start;
    int expected = (int)lroundf(fabsf(turned) / (10 * SK_PI / 180));
    printf("detents: turned %.1f deg -> position %d (expected magnitude %d)\n",
           turned * 180 / SK_PI, m.haptic.position, expected);
    assert(abs(abs(m.haptic.position) - expected) <= 1);
    /* Encoder direction +1: UI position increases as the raw angle decreases. */
    assert(m.haptic.position < 0);
}
/* Fine detents use the strongest derivative "click" term; with encoder
 * quantization and one-period delay they must still settle without buzzing. */
static void fine_detents_stable(void) {
    sk_motor m;
    plant p;
    calibrated(&m, &p);
    sk_haptic_config c = {.mode = SK_DETENT,
                          .width_deg = 3,
                          .strength = 1,
                          .endstop = 1,
                          .snap = 1.1f,
                          .damping = 1,
                          .click = SK_DEFAULT_CLICK};
    sk_motor_configure(&m, &c, encoder(&p));
    float rest = p.theta;
    p.omega = 2;
    run(&m, &p, 1500);
    float tail = run(&m, &p, 300);
    float residual = remainderf(p.theta - rest, 3 * SK_PI / 180);
    printf("fine detents: kicked at 2 rad/s, tail speed %.3f rad/s, %.2f deg from a detent\n", tail,
           fabsf(residual) * 180 / SK_PI);
    assert(tail < .3f && fabsf(residual) < .5f * SK_PI / 180);
}
/* A press pulse must move the knob perceptibly and leave the position alone. */
static void press_pulse(void) {
    sk_motor m;
    plant p;
    calibrated(&m, &p);
    sk_haptic_config c = {.mode = SK_DETENT,
                          .width_deg = 6,
                          .strength = .65f,
                          .endstop = 1,
                          .snap = 1.1f,
                          .click = SK_DEFAULT_CLICK};
    sk_motor_configure(&m, &c, encoder(&p));
    run(&m, &p, 300);
    float start = p.theta, peak = 0;
    sk_haptic_pulse(&m.haptic, true, false);
    for (int i = 0; i < 40; i++) {
        run(&m, &p, 1);
        peak = fmaxf(peak, fabsf(p.theta - start));
    }
    run(&m, &p, 500);
    printf("press pulse: peak motion %.2f deg, position %d\n", peak * 180 / SK_PI,
           m.haptic.position);
    /* Previous 0.7 V pulses moved this model 0.27 deg. */
    assert(peak > .5f * SK_PI / 180 && m.haptic.position == 0);
}
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    calibration_matrix();
    calibration_heavy_load();
    calibration_stalled();
    detent_restores();
    endstop_resists();
    detents_count();
    fine_detents_stable();
    press_pulse();
    puts("motor simulation passed");
    return 0;
}
