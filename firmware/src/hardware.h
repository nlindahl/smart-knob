#ifndef SK_HARDWARE_H
#define SK_HARDWARE_H
#include "sk.h"
#include <zephyr/kernel.h>
enum {
    FAULT_ENCODER = 1,
    FAULT_TIMING = 2,
    FAULT_MOTOR = 4,
    FAULT_STRAIN = 8,
    FAULT_LED = 16,
    FAULT_STORAGE = 32,
    FAULT_DISPLAY = 64
};
typedef struct {
    int32_t position;
    float sub, voltage, max_loop_us;
    uint32_t loops;
    uint8_t faults, calibration;
} motor_status;
int motor_start(void);
bool motor_configure(const sk_haptic_config *c);
bool motor_calibrate(void);
bool motor_enable(bool enable);
bool motor_pause(bool pause);
void motor_pulse(bool pressed, bool long_press);
void motor_snapshot(motor_status *out);
bool motor_get_calibration(sk_calibration *out);
bool motor_calibration_dirty(void);
void motor_calibration_saved(void);
int encoder_init(void);
int encoder_read(float *angle);
size_t encoder_diagnostic_text(char *out, size_t size);
int strain_start(void);
bool strain_tare(void);
uint8_t strain_event(void); /* 1 press, 2 release, 3 long, 4 long release */
bool strain_failed(void);
int storage_init(void);
size_t storage_load(uint8_t *out, size_t max);
bool storage_save(const uint8_t *data, size_t len);
bool storage_load_calibration(sk_calibration *out);
bool storage_save_calibration(const sk_calibration *c);
#endif
