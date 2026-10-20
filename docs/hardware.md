# Dev Kit v0.1 bring-up

The firmware builds for ESP32-S3 N16R8 and uses the pin assignments in upstream `platformio.ini`. No physical Dev Kit is attached to this workspace. Cross-compilation and simulation do not establish correct gate polarity, electrical calibration, press sensitivity, or LED timing on a real unit.

| Peripheral | GPIO / interface |
|---|---|
| GC9A01 MOSI / SCLK / CS / DC / reset | 3 / 4 / 9 / 2 / 10; SPI3, 40 MHz |
| Display backlight | 5; LEDC channel 0, 40 kHz, 50% initial duty |
| MT6701 data / clock / CS | 14 / 13 / 11; SPI2, mode 1, 4 MHz, 24-bit SSI |
| Motor U high / low | 8 / 16 |
| Motor V high / low | 18 / 7 |
| Motor W high / low | 17 / 15 |
| HX711 data / clock | 21 / 1 |
| LED ring data | 12; I2S0 DMA, GRB, 72 pixels |
| I2S auxiliary WS / BCK | 40 / 41; reserved by this firmware |
| USB | ESP32-S3 built-in Serial/JTAG |

The motor controller owns MCPWM0 directly through Zephyr's pinned Espressif HAL. A single 160 MHz timer counts up/down to 4000 for 20 kHz PWM. All three phase operators share it. High gates turn off at comparator A on the up ramp; low gates turn on at B. The transitions reverse on the down ramp. `B−A` implements a 500 ns gap, configurable by `CONFIG_SK_MOTOR_DEADTIME_NS` (250–1500 ns). Both outputs are forced low when disabled. Comparator writes freeze shadow latching until the complete vector has been written.

The control loop runs in a priority-1 thread, paced by a 1 ms Zephyr timer. Display/USB run at priority 7, strain at 6, LEDs at 8. Floating-point context sharing is enabled. Sensor errors turn the gates off immediately; three consecutive errors latch an encoder fault. A loop period over 10 ms or execution over 3 ms latches a timing fault. Faults require explicit enable or recalibration. A watchdog is fed only while the main loop and motor loop remain responsive.

FOC operates in **voltage mode**, with a 5 V supply assumption and a hard ±2 V feedback request limit. No phase-current sensing is implemented. The calibration routine applies a 1 V d-axis vector, aligns the rotor, sweeps three electrical revolutions, estimates 3–12 pole pairs and sensor direction, verifies alignment consistency, and records electrical zero. It takes approximately five seconds. Calibration is an asynchronous state machine; USB and the display remain responsive. View changes and bundle commits are rejected while it runs. Flash writes pause the motor with gates low.

## First hardware session

1. Back up your existing firmware/configuration using your normal recovery process. Have the BOOT/RESET flashing procedure available.
2. Confirm the six gate pins, active-high input convention, and 5 V supply against your board. Check complementary PWM and the gap with a logic analyzer/oscilloscope before enabling the motor stage. Software waveform tests establish the comparator math, not physical propagation delay.
3. Flash this firmware and connect USB. On first boot, passive rotation should update the displayed value while the motor remains disabled because there is no valid calibration. The six example views are installed by default.
4. Use `knobctl state` or the editor's telemetry to inspect faults. Encoder CRC or magnetic-field errors set bit 0. Check the angle frame, SPI timing, and magnet before proceeding if it is set.
5. Select **Calibrate motor** and leave the knob untouched. Calibration telemetry is 0=uncalibrated, 1=running, 2=valid. Successful calibration is saved automatically. An invalid result leaves the gates off. **Stop motor** aborts motion; **Enable motor** requires a valid calibration.
6. Test free spin, fine/coarse detents, both end stops, magnetic positions, switch behavior, and press/release pulses. Inspect loop timing while changing display values and uploading views.
7. Tare the press sensor with the knob released. Short press occurs on release; long press occurs after 800 ms and suppresses short navigation on release. The threshold defaults to 50,000 raw counts with release hysteresis. Adjust `CONFIG_SK_PRESS_THRESHOLD` for your load cell; no factory gain is imported. The system automatically averages 16 samples for tare at startup.
8. Check LED order and direction with position mode. Test all 72 pixels, progress limits, ring offset, press/view flashes, and rainbow. The I2S output uses 3.2 MHz symbols: `1000` for zero and `1100` for one, followed by a reset interval. Verify that those timings match your EC15/WS2812-compatible LEDs.
9. Upload and save a project, reboot, and verify its start view. Exercise interruption during an upload and, on a recoverable setup, during a save. NVS retains the previous committed record if the new record is incomplete; physical power-loss behavior still needs this test.

## Faults and storage

Fault bits: encoder=1, timing=2, motor/calibration=4, strain=8, LED=16, storage=32, display-write=64. Motor faults are latched. Strain and LED flags reflect current adapter state. Storage errors are reported and RAM operation remains available.

### Encoder fault diagnosis

`Faults: 1` means the control loop rejected at least three consecutive encoder reads and turned the motor gates off. `calibration: 0` means no valid calibration is stored; calibrating cannot fix invalid encoder input. Older builds reported `max loop: 0 µs` on this path because failed reads skipped the timing measurement, not because the control thread was stopped. New builds record timing on failed reads too.

Studio requests read-only diagnostic opcode 12 when an encoder fault is present. The original 28-byte state reply is unchanged, and Studio tolerates older firmware without opcode 12. The diagnostic line shows the last SPI result, raw 24-bit SSI frame, received/expected CRC-6, loss-of-track and magnetic-field status, plus read/valid/SPI-error/CRC-error/status-error counters. Values are copied under a spinlock; requests do not initiate SPI transfers or motor motion.

- Nonzero `spi_rc` and increasing `spi_err`: the Zephyr SPI transfer itself failed.
- `spi_rc=0` with increasing `crc_err`: frames arrived but failed CRC; inspect SSI timing, CS, and data/clock pins.
- Matching CRC with increasing `status_err`: the sensor reports loss of tracking or a nonzero magnetic-field status. Inspect the raw status and magnet alignment before changing acceptance criteria.
- Increasing `valid` after recovery with fault bit 1 still set: the encoder fault is latched. Only clear it through calibration/enable after validating the readings and completing bring-up.

The original Seedlabs firmware accepts CRC-valid frames without rejecting magnetic-status flags. This implementation currently rejects loss-of-track and nonzero field status. Capture the diagnostic line before deciding whether the stricter policy explains a fault; do not disable CRC/status checks as a troubleshooting shortcut.

View bundles use NVS ID 1; calibration uses ID 2. The NVS partition occupies `0xF00000`–`0xF3FFFF` (256 KiB) in the 16 MB flash. It uses eight 32 KiB sectors with NVS data CRC enabled. Each view bundle has a second CRC and undergoes full validation before application. This layout and calibration format differ from factory firmware.

The release's compiled artifacts and host/browser tests are recorded in `VALIDATION.md`. Hardware timing, haptic tuning, load-cell thresholds, and recovery under real power loss are remaining validation work.
