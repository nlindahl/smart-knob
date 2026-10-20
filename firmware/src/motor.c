#include "hardware.h"
#include <esp_rom_gpio.h>
#include <hal/mcpwm_ll.h>
#include <math.h>
#include <soc/gpio_sig_map.h>
#include <string.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>

/* Three operators share one 160 MHz center-aligned timer (20 kHz).
 * Two compare edges per phase produce complementary outputs with 500 ns
 * non-overlap on both transitions. Updates latch together at timer zero.
 * This controller exclusively owns MCPWM0; the generic PWM driver is disabled. */
#define PWM_PEAK 4000u
static const uint8_t pins[6] = {8, 16, 18, 7, 17, 15};
static mcpwm_dev_t *pwm;
static const struct device *gpio = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static bool pwm_ready;
static void gates_off(void) {
    if (!pwm_ready)
        return;
    for (int phase = 0; phase < 3; phase++)
        for (int gen = 0; gen < 2; gen++)
            mcpwm_ll_gen_set_continue_force_level(pwm, phase, gen, 0);
}
static void gates_on(void) {
    for (int phase = 0; phase < 3; phase++)
        for (int gen = 0; gen < 2; gen++)
            mcpwm_ll_gen_disable_continue_force_action(pwm, phase, gen);
}
static void duties(const float duty[3]) {
    unsigned dead = (CONFIG_SK_MOTOR_DEADTIME_NS * 160u + 999u) / 1000u;
    unsigned key = irq_lock();
    /* Freeze all comparator latches while assembling the next vector. */
    pwm->update_cfg.global_up_en = 0;
    for (int phase = 0; phase < 3; phase++) {
        unsigned a, b;
        sk_pwm_compare(duty[phase], PWM_PEAK, dead, &a, &b);
        mcpwm_ll_operator_set_compare_value(pwm, phase, 0, a);
        mcpwm_ll_operator_set_compare_value(pwm, phase, 1, b);
    }
    pwm->update_cfg.global_up_en = 1;
    irq_unlock(key);
}
static int pwm_init(void) {
    if (!device_is_ready(gpio))
        return -ENODEV;
    for (int i = 0; i < 6; i++) {
        int rc = gpio_pin_configure(gpio, pins[i], GPIO_OUTPUT_INACTIVE);
        if (rc)
            return rc;
    }
    const struct device *clock = DEVICE_DT_GET(DT_CLOCKS_CTLR(DT_NODELABEL(mcpwm0)));
    int rc = clock_control_on(clock,
                              (clock_control_subsys_t)DT_CLOCKS_CELL(DT_NODELABEL(mcpwm0), offset));
    if (rc)
        return rc;
    pwm = MCPWM_LL_GET_HW(0);
    mcpwm_ll_group_enable_clock(0, true);
    mcpwm_ll_group_set_clock_source(0, MCPWM_TIMER_CLK_SRC_DEFAULT);
    mcpwm_ll_group_set_clock_prescale(0, 1);
    mcpwm_ll_timer_set_clock_prescale(pwm, 0, 1);
    mcpwm_ll_timer_set_count_mode(pwm, 0, MCPWM_TIMER_COUNT_MODE_UP_DOWN);
    mcpwm_ll_timer_set_peak(pwm, 0, PWM_PEAK, false);
    mcpwm_ll_timer_update_period_at_once(pwm, 0);
    for (int phase = 0; phase < 3; phase++) {
        mcpwm_ll_operator_connect_timer(pwm, phase, 0);
        for (int gen = 0; gen < 2; gen++) {
            mcpwm_ll_generator_reset_actions(pwm, phase, gen);
            mcpwm_ll_gen_set_continue_force_level(pwm, phase, gen, 0);
            mcpwm_ll_deadtime_bypass_path(pwm, phase, gen, true);
            mcpwm_ll_deadtime_invert_outpath(pwm, phase, gen, false);
            mcpwm_ll_deadtime_swap_out_path(pwm, phase, gen, false);
            mcpwm_ll_operator_stop_update_compare(pwm, phase, gen, false);
            mcpwm_ll_operator_enable_update_compare_on_tez(pwm, phase, gen, true);
            mcpwm_ll_generator_set_action_on_timer_event(
                pwm, phase, gen, MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY,
                gen ? MCPWM_GEN_ACTION_LOW : MCPWM_GEN_ACTION_HIGH);
        }
        mcpwm_ll_generator_set_action_on_compare_event(pwm, phase, 0, MCPWM_TIMER_DIRECTION_UP, 0,
                                                       MCPWM_GEN_ACTION_LOW);
        mcpwm_ll_generator_set_action_on_compare_event(pwm, phase, 0, MCPWM_TIMER_DIRECTION_DOWN, 0,
                                                       MCPWM_GEN_ACTION_HIGH);
        mcpwm_ll_generator_set_action_on_compare_event(pwm, phase, 1, MCPWM_TIMER_DIRECTION_UP, 1,
                                                       MCPWM_GEN_ACTION_HIGH);
        mcpwm_ll_generator_set_action_on_compare_event(pwm, phase, 1, MCPWM_TIMER_DIRECTION_DOWN, 1,
                                                       MCPWM_GEN_ACTION_LOW);
        esp_rom_gpio_connect_out_signal(pins[phase * 2], PWM0_OUT0A_IDX + phase * 2, false, false);
        esp_rom_gpio_connect_out_signal(pins[phase * 2 + 1], PWM0_OUT0B_IDX + phase * 2, false,
                                        false);
    }
    pwm_ready = true;
    const float neutral[3] = {.5f, .5f, .5f};
    duties(neutral);
    mcpwm_ll_group_enable_shadow_mode(pwm);
    mcpwm_ll_group_flush_shadow(pwm);
    mcpwm_ll_timer_set_start_stop_command(pwm, 0, MCPWM_TIMER_START_NO_STOP);
    gates_off();
    return 0;
}

enum command_type { CMD_CONFIG, CMD_PULSE, CMD_CALIBRATE, CMD_ENABLE };
typedef struct {
    uint8_t type;
    bool press, long_press;
    sk_haptic_config config;
} motor_command;
K_MSGQ_DEFINE(commands, sizeof(motor_command), 8, 4);
static struct k_thread thread;
K_THREAD_STACK_DEFINE(stack, 4096);
static struct k_spinlock status_lock;
static motor_status status;
static sk_calibration calibration;
static sk_haptic_state haptic;
static atomic_t pause_requested, paused, enabled, dirty, stop_requested;
K_SEM_DEFINE(control_tick, 0, 1);
static void timer_tick(struct k_timer *timer) {
    (void)timer;
    k_sem_give(&control_tick);
}
K_TIMER_DEFINE(control_timer, timer_tick, NULL);
static uint8_t calib_phase;
static float electrical, previous_raw, travel, start_travel, calib_start, end_mechanical;
static uint32_t calibration_tick;
static void snapshot(float elapsed, float voltage) {
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    status.position = haptic.position;
    status.sub = haptic.sub_position;
    status.voltage = voltage;
    status.loops++;
    if (elapsed > status.max_loop_us)
        status.max_loop_us = elapsed;
    status.calibration = calib_phase ? 1 : calibration.valid ? 2 : 0;
    k_spin_unlock(&status_lock, key);
}
static void fault(uint8_t value) {
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    status.faults |= value;
    k_spin_unlock(&status_lock, key);
    gates_off();
    atomic_clear(&enabled);
    calib_phase = 0;
}
static void drive(float q, float d, float angle) {
    float duty[3];
    sk_foc(q, d, angle, 5, duty);
    duties(duty);
    gates_on();
}
static void calibration_step(float raw) {
    /* Align d-axis, sweep three electrical turns, measure pole pairs, then
     * return to electrical zero. This state machine never blocks the loop. */
    calibration_tick++;
    travel += remainderf(raw - previous_raw, SK_TAU);
    previous_raw = raw;
    if (calib_phase == 1) {
        drive(0, 1.0f, 0);
        if (calibration_tick >= 800) {
            start_travel = travel;
            calib_start = raw;
            electrical = 0;
            calib_phase = 2;
            calibration_tick = 0;
        }
    } else if (calib_phase == 2) {
        electrical += .006f;
        drive(0, 1.0f, electrical);
        if (electrical >= 3 * SK_TAU) {
            calib_phase = 3;
            calibration_tick = 0;
        }
    } else if (calib_phase == 3) {
        drive(0, 1.0f, 3 * SK_TAU);
        if (calibration_tick >= 800) {
            float moved = travel - start_travel;
            float pairs = 3 * SK_TAU / fabsf(moved);
            if (!isfinite(pairs) || pairs < 2.8f || pairs > 12.2f) {
                fault(FAULT_MOTOR);
                return;
            }
            int rounded = (int)lroundf(pairs);
            if (!isfinite(pairs) || rounded < 3 || rounded > 12 || fabsf(pairs - rounded) > .2f) {
                fault(FAULT_MOTOR);
                return;
            }
            k_spinlock_key_t cal_key = k_spin_lock(&status_lock);
            calibration = (sk_calibration){
                .pole_pairs = rounded, .direction = moved > 0 ? 1 : -1, .valid = false};
            k_spin_unlock(&status_lock, cal_key);
            end_mechanical = raw;
            /* Both alignment endpoints must agree in electrical coordinates. */
            if (fabsf(remainderf((raw - calib_start) * rounded, SK_TAU)) > .35f) {
                fault(FAULT_MOTOR);
                return;
            }
            cal_key = k_spin_lock(&status_lock);
            calibration.zero_electrical = remainderf(raw * rounded * calibration.direction, SK_TAU);
            k_spin_unlock(&status_lock, cal_key);
            calib_phase = 4;
            calibration_tick = 0;
        }
    } else {
        drive(0, 1.0f, 0);
        if (calibration_tick >= 300) {
            if (fabsf(remainderf(raw - end_mechanical, SK_TAU)) > .1f) {
                fault(FAULT_MOTOR);
                return;
            }
            k_spinlock_key_t key = k_spin_lock(&status_lock);
            calibration.valid = true;
            k_spin_unlock(&status_lock, key);
            atomic_set(&dirty, 1);
            calib_phase = 0;
            sk_haptic_init(&haptic, &haptic.config, -raw);
            gates_off();
        }
    }
}
static void run(void *a, void *b, void *c) {
    (void)a;
    (void)b;
    (void)c;
    float raw = 0;
    sk_haptic_config default_config = {
        .mode = SK_FREE, .width_deg = 6, .snap = 1.1f, .endstop = .8f};
    sk_haptic_init(&haptic, &default_config, 0);
    uint64_t last = k_cycle_get_64();
    uint32_t bad_samples = 0;
    while (true) {
        k_sem_take(&control_tick, K_FOREVER);
        uint64_t begin = k_cycle_get_64();
        float dt = (float)k_cyc_to_us_floor64(begin - last) / 1000000;
        last = begin;
        if (atomic_get(&stop_requested)) {
            gates_off();
            atomic_clear(&enabled);
            calib_phase = 0;
            k_msgq_purge(&commands);
            atomic_clear(&stop_requested);
        }
        if (atomic_get(&pause_requested)) {
            gates_off();
            atomic_set(&paused, 1);
            continue;
        }
        if (atomic_cas(&paused, 1, 0)) {
            last = k_cycle_get_64();
            dt = .001f;
        }
        int rc = encoder_read(&raw);
        if (rc) {
            gates_off();
            if (++bad_samples >= 3)
                fault(FAULT_ENCODER);
            uint32_t elapsed = k_cyc_to_us_floor64(k_cycle_get_64() - begin);
            snapshot(fmaxf(elapsed, dt * 1000000), 0);
            continue;
        }
        bad_samples = 0;
        motor_command cmd;
        while (!k_msgq_get(&commands, &cmd, K_NO_WAIT)) {
            if (cmd.type == CMD_CONFIG) {
                if (!calib_phase)
                    sk_haptic_init(&haptic, &cmd.config, -raw);
            } else if (cmd.type == CMD_PULSE) {
                if (!calib_phase)
                    sk_haptic_pulse(&haptic, cmd.press, cmd.long_press);
            } else if (cmd.type == CMD_ENABLE) {
                if (calibration.valid) {
                    k_spinlock_key_t key = k_spin_lock(&status_lock);
                    status.faults = 0;
                    k_spin_unlock(&status_lock, key);
                    atomic_set(&enabled, 1);
                    sk_haptic_init(&haptic, &haptic.config, -raw);
                }
            } else {
                k_spinlock_key_t key = k_spin_lock(&status_lock);
                status.faults = 0;
                calibration.valid = false;
                k_spin_unlock(&status_lock, key);
                calib_phase = 1;
                calibration_tick = 0;
                previous_raw = raw;
                travel = 0;
                atomic_set(&enabled, 1);
            }
        }
        float q = 0;
        if (atomic_get(&enabled)) {
            if (dt > .01f || dt <= 0)
                fault(FAULT_TIMING);
            else if (calib_phase)
                calibration_step(raw);
            else if (calibration.valid) {
                q = sk_haptic_step(&haptic, -raw, dt);
                /* UI clockwise sign is opposite the raw encoder sign. */
                float voltage = -q * calibration.direction;
                float angle = raw * calibration.pole_pairs * calibration.direction -
                              calibration.zero_electrical;
                if (fabsf(voltage) < .001f)
                    gates_off();
                else
                    drive(voltage, 0, angle);
            } else
                gates_off();
        } else {
            sk_haptic_step(&haptic, -raw, fminf(fmaxf(dt, .0001f), .02f));
            gates_off();
        }
        uint32_t elapsed = k_cyc_to_us_floor64(k_cycle_get_64() - begin);
        snapshot(fmaxf(elapsed, dt * 1000000), q);
        if (elapsed > 3000 && atomic_get(&enabled))
            fault(FAULT_TIMING);
    }
}
int motor_start(void) {
    int rc = encoder_init();
    if (rc)
        return rc;
    rc = pwm_init();
    if (rc)
        return rc;
    storage_load_calibration(&calibration);
    atomic_set(&enabled, calibration.valid);
    k_timer_start(&control_timer, K_MSEC(1), K_MSEC(1));
    k_thread_create(&thread, stack, K_THREAD_STACK_SIZEOF(stack), run, NULL, NULL, NULL, 1,
                    K_FP_REGS, K_NO_WAIT);
    return 0;
}
bool motor_configure(const sk_haptic_config *c) {
    if (!sk_haptic_valid(c))
        return false;
    motor_command cmd = {.type = CMD_CONFIG, .config = *c};
    return k_msgq_put(&commands, &cmd, K_NO_WAIT) == 0;
}
void motor_pulse(bool press, bool long_press) {
    motor_command cmd = {.type = CMD_PULSE, .press = press, .long_press = long_press};
    k_msgq_put(&commands, &cmd, K_NO_WAIT);
}
bool motor_calibrate(void) {
    motor_status s;
    motor_snapshot(&s);
    if (s.calibration == 1)
        return false;
    motor_command cmd = {.type = CMD_CALIBRATE};
    return k_msgq_put(&commands, &cmd, K_NO_WAIT) == 0;
}
bool motor_enable(bool enable) {
    if (!enable) {
        atomic_set(&stop_requested, 1);
        return true;
    }
    motor_command cmd = {.type = CMD_ENABLE};
    return k_msgq_put(&commands, &cmd, K_NO_WAIT) == 0;
}
bool motor_pause(bool pause) {
    atomic_set(&pause_requested, pause);
    if (!pause)
        return true;
    for (int i = 0; i < 100; i++) {
        if (atomic_get(&paused))
            return true;
        k_sleep(K_MSEC(1));
    }
    atomic_clear(&pause_requested);
    return false;
}
void motor_snapshot(motor_status *out) {
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    *out = status;
    k_spin_unlock(&status_lock, key);
}
bool motor_get_calibration(sk_calibration *out) {
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    *out = calibration;
    k_spin_unlock(&status_lock, key);
    return out->valid;
}
bool motor_calibration_dirty(void) {
    return atomic_get(&dirty) != 0;
}
void motor_calibration_saved(void) {
    atomic_clear(&dirty);
}
