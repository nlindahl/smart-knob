# Motor feedback test candidate

Physical testing reported poor feedback and audible motor noise with the Zephyr firmware, while stock Seedlabs firmware works on the same Dev Kit. This report does not establish which control path produces the sound. Keep stock firmware as the reference until a candidate is physically validated.

## Findings from source comparison

- The Seedlabs MT6701 driver filters the angle using an approximately 0.4 sample weight; the initial Zephyr feedback calculation used raw readings.
- The Zephyr driver forced all six gates low below a 1 mV torque command, then released the force as the command changed. Stock SimpleFOC maintains PWM at zero requested torque.
- After rejected samples, feedback used the next loop tick's interval instead of the elapsed time since the previous usable reading. This overestimates velocity and damping on recovery.
- Both the Zephyr timer and stock SimpleFOC 2.3.3 six-PWM default use a 20 kHz carrier. This candidate keeps that frequency, the existing 500 ns dead time, voltage limits, phase mapping, calibration, and encoder acceptance policy.

Reference sources: [Seedlabs encoder](https://github.com/SeedLabs-it/smartknob-firmware/blob/afd6632dca517cb9b8a20c761e033d8f3181c47e/firmware/src/motor_foc/mt6701_sensor.cpp), [Seedlabs feedback loop](https://github.com/SeedLabs-it/smartknob-firmware/blob/afd6632dca517cb9b8a20c761e033d8f3181c47e/firmware/src/motor_foc/motor_task.cpp), and [SimpleFOC ESP32 backend](https://github.com/simplefoc/Arduino-FOC/blob/v2.3.3/src/drivers/hardware_specific/esp32/esp32_mcu.cpp).

## Candidate changes

The feedback path uses a shortest-arc angle low-pass filter with a 1.5 ms time constant (0.4 weight at the nominal 1 kHz sample interval). Commutation and motor calibration continue to use the raw valid angle, so this filter introduces no commutation phase lag. Feedback and velocity filtering use actual usable-sample intervals, including short gaps. A long gap resets the angle conditioner. Config, enable, and calibration completion recenter on the conditioned feedback angle.

An enabled, calibrated motor keeps synchronous PWM at zero requested torque, with equal phase duties. Gate forcing changes only when the enabled state changes, rather than being rewritten every loop. Stop, faults, invalid encoder reads, flash pauses, and an uncalibrated/disabled motor still force the outputs low.

Encoder rejection can still interrupt torque. The earlier reported tracking-loss frames are not accepted by this candidate; repeated output interruptions may therefore remain audible. This candidate does not claim that filtering fixes the tracking indication, electrical calibration, physical PWM, or stock-equivalent haptic feel.

## Validation and hardware follow-up

Core regression tests cover stationary jitter attenuation, angle-wrap continuity over many turns, long-gap reset, nonfinite input, and velocity updates after a multi-millisecond sample gap. Tests and cross-compilation do not verify motor sound, phase polarity, dead time, commutation alignment, or subjective feedback.

Before requesting more motor tests, record whether noise occurs at rest, during rotation, or both; whether free mode is affected; firmware version; fault/calibration/loop-time status; and the complete encoder diagnostic line. Compare matched views and settings with stock, and distinguish quietness from detent strength and snap behavior. The supplied encoder burst counters help determine whether torque interruptions correlate with noise. Use the [hardware bring-up procedure](hardware.md) for physical validation; stop a candidate that vibrates, heats, or behaves unexpectedly.
