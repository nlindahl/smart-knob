#ifndef SK_MOTOR_H
#define SK_MOTOR_H
#include "sk.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Hardware-independent motor controller: calibration state machine, haptic
 * torque and voltage-mode FOC. Firmware feeds it encoder angles at 1 kHz and
 * applies the returned duties; host tests drive it against a motor model. */
typedef struct {
    float supply;              /* driver supply in volts */
    float calibration_voltage; /* d-axis alignment voltage */
} sk_motor_limits;
enum sk_calibration_result {
    SK_CAL_NONE,
    SK_CAL_OK,
    SK_CAL_NO_MOTION,   /* measured pole pairs outside 3..12: rotor did not follow */
    SK_CAL_NOT_INTEGER, /* measured pole pairs not close to an integer */
    SK_CAL_ENDPOINTS,   /* start/end alignment disagree electrically */
    SK_CAL_DRIFT,       /* rotor moved while holding the final vector */
    SK_CAL_ABORTED
};
enum sk_motor_event { SK_MOTOR_IDLE, SK_MOTOR_CALIBRATED, SK_MOTOR_CALIBRATION_FAILED };
typedef struct {
    sk_motor_limits limits;
    sk_calibration calibration;
    sk_haptic_state haptic;
    bool enabled;
    uint8_t phase;
    uint32_t tick;
    float electrical, previous_raw, travel, start_travel, calib_start, end_mechanical;
    /* Last calibration diagnostics, retained after success or failure. */
    uint8_t result;
    float measured_pairs, measured_travel, drift;
} sk_motor;
typedef struct {
    bool drive; /* false: all gates off (coast) */
    float duty[3];
    float q, d; /* requested voltages */
} sk_motor_output;
void sk_motor_init(sk_motor *m, const sk_motor_limits *limits, const sk_calibration *cal,
                   const sk_haptic_config *config, float raw);
void sk_motor_configure(sk_motor *m, const sk_haptic_config *config, float raw);
void sk_motor_calibrate(sk_motor *m, float raw);
bool sk_motor_enable(sk_motor *m, float raw);
void sk_motor_stop(sk_motor *m);
bool sk_motor_calibrating(const sk_motor *m);
/* One control period. raw is the accepted encoder angle (radians, 0..2pi). */
enum sk_motor_event sk_motor_step(sk_motor *m, float raw, float dt, sk_motor_output *out);
const char *sk_calibration_result_name(uint8_t result);
#ifdef __cplusplus
}
#endif
#endif
