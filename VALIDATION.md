# Validation — 2026-10-14

## Browser installer — 2026-10-20

- Firmware packager: eight tests cover the manifest/checksums, runner offset, saved-data layout, wrong chip, oversized writes, unsafe versions, corrupt image checksums, and unsupported MCUboot layouts. The existing 500,564-byte ESP32-S3 cross-built image also passes packaging checks.
- ESP Web Tools 10.4.0 is pinned and served locally with dependency notices. Chromium runs its actual dialog and flash orchestration while mocking only USB/chip I/O: verified bytes at offset zero, no full erase by default, USB write failure, wrong-chip rejection, checksum failure/retry, unsupported/insecure browsers, unavailable firmware, canceled port selection, and editor USB release before installer navigation.
- Rebuilt the Rust/WASM editor; formatting and WASM Clippy pass. The Pages startup/renderer smoke test still passes with the installer changes.
- These are software checks. No physical device was flashed, backed up, restored, or exercised. Motor, LED, display, and actual bootloader/recovery behavior still need the hardware validation below.

## Passed locally

- Rust workspace: 8 unit tests, including project validation, CRC/framing, fragmented streams, upload sequencing, editor navigation, and a headless UI frame.
- Rust formatting and Clippy with warnings denied, including the WASM editor target.
- Rust editor release build for `wasm32-unknown-unknown`, packaged with matching wasm-bindgen 0.2.129.
- Native C control/model/protocol and LVGL rendering tests with AddressSanitizer and UndefinedBehaviorSanitizer. LeakSanitizer was disabled because this execution sandbox uses ptrace.
- Every truncation of the Rust-generated demo bundle and 30,000 CRC-valid mutated bundles decoded under sanitizers without a memory/undefined-behavior failure.
- The actual Rust CLI communicated with the actual C protocol through a pseudoterminal: valid upload/save, selecting view 4, telemetry, rejected corrupt upload, and preservation of the active view after rejection.
- Chromium/Playwright ran the built Rust editor and LVGL WASM: startup without browser errors, dragging a value widget, full-drag undo/redo, adding a view, persistence after reload, simulated rotation, circular alpha masking, and 72 RGB LED outputs.
- LVGL text remained present across repeated loading of edited bundles.
- Zephyr 4.4.2 firmware cross-build with LVGL 9.6.0 and SDK 1.0.1 for `esp32s3_devkitc/esp32s3/procpu`.
- esptool 5.5.0 recognizes the resulting image as ESP32-S3, 16 MB flash, DIO at 80 MHz, entry point in IRAM, and simple-boot address 0x0.

Firmware image: approximately 489 KiB (500,564 bytes). Linker RAM use: 230,496 bytes DRAM and 44,312 bytes IRAM. The build uses internal RAM and enables floating-point context sharing for the motor, LED, sensor, and UI tasks.

## Hardware validation still required

No physical SmartKnob was attached. Successful compilation and simulation do not validate physical PWM polarity/dead time, control-loop timing under hardware SPI/DMA load, electrical calibration, detent feel, load-cell thresholds, LED signaling, or power-loss recovery. [hardware.md](docs/hardware.md) provides the test sequence. The requested motor feedback and LED behavior are implemented, but remain unverified on a real Dev Kit.

GitHub CI configuration is included; the validation above was performed locally before publication. No physical firmware was flashed.
