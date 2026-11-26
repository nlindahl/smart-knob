#include "sk_motor.h"
#include <math.h>
#include <string.h>
enum {
    PHASE_IDLE,
    PHASE_ALIGN,
    PHASE_SWEEP,
    PHASE_SETTLE,
    PHASE_OFFSET_FORWARD,
    PHASE_OFFSET_REVERSE,
    PHASE_VERIFY
};
#define SWEEP_RATE .006f /* electrical radians per control tick */
#define OFFSET_SKIP 150  /* ticks after a direction change excluded from the offset */
static void coast(sk_motor_output *out) {
    out->drive = false;
    out->q = out->d = 0;
    out->duty[0] = out->duty[1] = out->duty[2] = .5f;
}
static void drive(const sk_motor *m, float q, float d, float angle, sk_motor_output *out) {
    sk_foc(q, d, angle, m->limits.supply, out->duty);
    out->drive = true;
    out->q = q;
    out->d = d;
}
void sk_motor_init(sk_motor *m, const sk_motor_limits *limits, const sk_calibration *cal,
                   const sk_haptic_config *config, float raw) {
    memset(m, 0, sizeof(*m));
    m->limits = *limits;
    if (cal)
        m->calibration = *cal;
    m->enabled = m->calibration.valid;
    sk_haptic_init(&m->haptic, config, -raw);
}
void sk_motor_configure(sk_motor *m, const sk_haptic_config *config, float raw) {
    if (!m->phase)
        sk_haptic_init(&m->haptic, config, -raw);
}
void sk_motor_calibrate(sk_motor *m, float raw) {
    m->calibration.valid = false;
    m->phase = PHASE_ALIGN;
    m->tick = 0;
    m->previous_raw = raw;
    m->travel = 0;
    m->result = SK_CAL_NONE;
    m->enabled = true;
}
bool sk_motor_enable(sk_motor *m, float raw) {
    if (!m->calibration.valid)
        return false;
    m->enabled = true;
    sk_haptic_init(&m->haptic, &m->haptic.config, -raw);
    return true;
}
void sk_motor_stop(sk_motor *m) {
    if (m->phase)
        m->result = SK_CAL_ABORTED;
    m->enabled = false;
    m->phase = PHASE_IDLE;
}
bool sk_motor_calibrating(const sk_motor *m) {
    return m->phase != PHASE_IDLE;
}
static enum sk_motor_event fail(sk_motor *m, uint8_t result, sk_motor_output *out) {
    m->result = result;
    m->enabled = false;
    m->phase = PHASE_IDLE;
    coast(out);
    return SK_MOTOR_CALIBRATION_FAILED;
}
/* Electrical offset at the current commanded angle, accumulated as a unit vector. */
static void sample_offset(sk_motor *m, float raw, int sweep) {
    float measured = raw * m->calibration.pole_pairs * m->calibration.direction;
    float offset = measured - m->electrical;
    m->offset_x[sweep] += cosf(offset);
    m->offset_y[sweep] += sinf(offset);
    m->offset_samples[sweep]++;
}
/* Align the d-axis, sweep three electrical turns to measure pole pairs and
 * direction, then sweep forward and back to average the electrical offset so
 * that friction and cogging lag cancel. The state machine never blocks. */
static enum sk_motor_event calibration_step(sk_motor *m, float raw, sk_motor_output *out) {
    const float v = m->limits.calibration_voltage;
    m->tick++;
    m->travel += remainderf(raw - m->previous_raw, SK_TAU);
    m->previous_raw = raw;
    if (m->phase == PHASE_ALIGN) {
        drive(m, 0, v, 0, out);
        if (m->tick >= 800) {
            m->start_travel = m->travel;
            m->calib_start = raw;
            m->electrical = 0;
            m->phase = PHASE_SWEEP;
            m->tick = 0;
        }
    } else if (m->phase == PHASE_SWEEP) {
        m->electrical += SWEEP_RATE;
        drive(m, 0, v, m->electrical, out);
        if (m->electrical >= 3 * SK_TAU) {
            m->electrical = 3 * SK_TAU;
            m->phase = PHASE_SETTLE;
            m->tick = 0;
        }
    } else if (m->phase == PHASE_SETTLE) {
        drive(m, 0, v, m->electrical, out);
        if (m->tick >= 800) {
            float moved = m->travel - m->start_travel;
            float pairs = 3 * SK_TAU / fabsf(moved);
            m->measured_travel = moved;
            m->measured_pairs = isfinite(pairs) ? pairs : 0;
            if (!isfinite(pairs) || pairs < 2.8f || pairs > 12.2f)
                return fail(m, SK_CAL_NO_MOTION, out);
            int rounded = (int)lroundf(pairs);
            if (rounded < 3 || rounded > 12 || fabsf(pairs - rounded) > .2f)
                return fail(m, SK_CAL_NOT_INTEGER, out);
            m->calibration = (sk_calibration){
                .pole_pairs = rounded, .direction = moved > 0 ? 1 : -1, .valid = false};
            m->end_mechanical = raw;
            /* Both alignment endpoints must agree in electrical coordinates. */
            if (fabsf(remainderf((raw - m->calib_start) * rounded, SK_TAU)) > 1)
                return fail(m, SK_CAL_ENDPOINTS, out);
            for (int i = 0; i < 2; i++) {
                m->offset_x[i] = m->offset_y[i] = 0;
                m->offset_samples[i] = 0;
            }
            m->phase = PHASE_OFFSET_FORWARD;
            m->tick = 0;
        }
    } else if (m->phase == PHASE_OFFSET_FORWARD || m->phase == PHASE_OFFSET_REVERSE) {
        bool forward = m->phase == PHASE_OFFSET_FORWARD;
        m->electrical += forward ? SWEEP_RATE : -SWEEP_RATE;
        drive(m, 0, v, m->electrical, out);
        if (m->tick > OFFSET_SKIP)
            sample_offset(m, raw, !forward);
        if (forward && m->electrical >= 5 * SK_TAU) {
            m->phase = PHASE_OFFSET_REVERSE;
            m->tick = 0;
        } else if (!forward && m->electrical <= 3 * SK_TAU) {
            m->electrical = 3 * SK_TAU;
            /* Friction lags each sweep by a constant angle, so each sweep's
             * offsets cluster tightly; scatter means the rotor did not track.
             * The two lags have opposite signs and cancel in the mean. */
            float x = 0, y = 0, length = 1;
            for (int i = 0; i < 2; i++) {
                float n = (float)m->offset_samples[i];
                length = fminf(length, hypotf(m->offset_x[i], m->offset_y[i]) / n);
                x += m->offset_x[i] / n;
                y += m->offset_y[i] / n;
            }
            m->consistency = length;
            m->hysteresis = fabsf(remainderf(atan2f(m->offset_y[0], m->offset_x[0]) -
                                                 atan2f(m->offset_y[1], m->offset_x[1]),
                                             SK_TAU));
            if (!(length > .9f) || m->hysteresis > 2)
                return fail(m, SK_CAL_INCONSISTENT, out);
            m->calibration.zero_electrical = atan2f(y, x);
            m->phase = PHASE_VERIFY;
            m->tick = 0;
        }
    } else {
        drive(m, 0, v, m->electrical, out);
        if (m->tick >= 500) {
            /* Back at the vector measured in SETTLE: the rotor must return there. */
            m->drift = remainderf(raw - m->end_mechanical, SK_TAU);
            if (fabsf(m->drift) * m->calibration.pole_pairs > 1)
                return fail(m, SK_CAL_DRIFT, out);
            m->calibration.valid = true;
            m->result = SK_CAL_OK;
            m->phase = PHASE_IDLE;
            sk_haptic_init(&m->haptic, &m->haptic.config, -raw);
            coast(out);
            return SK_MOTOR_CALIBRATED;
        }
    }
    return SK_MOTOR_IDLE;
}
enum sk_motor_event sk_motor_step(sk_motor *m, float raw, float dt, sk_motor_output *out) {
    coast(out);
    if (!m->enabled) {
        sk_haptic_step(&m->haptic, -raw, fminf(fmaxf(dt, .0001f), .02f));
        return SK_MOTOR_IDLE;
    }
    if (m->phase)
        return calibration_step(m, raw, out);
    if (!m->calibration.valid)
        return SK_MOTOR_IDLE;
    float q = sk_haptic_step(&m->haptic, -raw, dt);
    /* UI clockwise sign is opposite the raw encoder sign. */
    float voltage = -q * m->calibration.direction;
    float angle =
        raw * m->calibration.pole_pairs * m->calibration.direction - m->calibration.zero_electrical;
    if (fabsf(voltage) >= .001f)
        drive(m, voltage, 0, angle, out);
    out->q = q;
    return SK_MOTOR_IDLE;
}
const char *sk_calibration_result_name(uint8_t result) {
    static const char *const names[] = {"none",      "ok",    "no_motion", "not_integer",
                                        "endpoints", "drift", "aborted",   "inconsistent"};
    return result < sizeof(names) / sizeof(names[0]) ? names[result] : "unknown";
}
