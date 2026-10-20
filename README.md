# SmartKnob Zephyr + Studio

Zephyr firmware for the **Seedlabs SmartKnob Dev Kit v0.1**, with an LVGL UI and a **Rust/WASM browser view editor**. The first release includes motor feedback, press sensing, the 240×240 display, USB view upload, persistent views/calibration, and the 72-LED ring.

The firmware cross-compiles for ESP32-S3. The editor, shared renderer, model, and serial protocol have automated tests. **Physical hardware operation has not been validated in this workspace.** Follow [hardware bring-up](docs/hardware.md) before treating this as a replacement for the factory firmware.

[View editor screenshots and SmartKnob display previews](docs/screenshots/README.md).

## Use the prebuilt editor

The delivery archive includes `web/pkg`, `web/preview.js`, and `web/preview.wasm`. From the extracted project:

```sh
python3 scripts/serve.py
```

Open **http://localhost:8080**. Create or duplicate views, drag widgets on the circular canvas, edit properties, and configure motor feedback and LEDs. Projects are saved in browser local storage; export JSON for a portable backup. Undo/redo buttons and Ctrl/Cmd+Z and Ctrl/Cmd+Y are available.

The preview runs **the same C LVGL renderer and haptic/LED algorithms as firmware**, compiled with Emscripten. Its simulation shows positions and requested voltage; physical feedback is tested on a connected knob. Firmware renders RGB565; the browser renders XRGB8888, so color quantization differs slightly.

Use **Connect USB** in desktop Chrome/Edge on localhost or HTTPS. **Test views** applies an upload in RAM. **Upload & save** persists it through reboot. **Show selected view** changes the device view. You can calibrate, tare, stop, and enable the motor, and follow live device state. Other browsers can edit and export files; use `knobctl` for USB when Web Serial is unavailable.

## Included behavior

- Free rotation, fine/coarse detents, bounded rotation with end stops, magnetic detents at selected positions, and a two-position switch.
- Adjustable spacing, detent/end-stop strength, snap threshold, damping, and initial position per view.
- Short/long press navigation and press/release haptic pulses.
- LED off, solid, position, progress, and rainbow modes; color, accent, brightness, rotation offset, direction, and press/view-change flashes per view.
- Label, numeric value with suffix, arc, bar, rectangle, and circle widgets. Numeric/arc/bar widgets bind to the active knob value.
- Up to 8 views, 16 widgets per view, and 16 magnetic detent positions. Built-in fonts are 16/24/32 px Montserrat; text is printable ASCII in v1.

The LED renderer applies a conservative estimated 180 mA ring budget. High brightness and full-white patterns scale down automatically. Wi-Fi, MQTT/Home Assistant, Spotify, OTA, ambient-light and proximity behavior are planned after this core release.

## Pinned versions

Latest stable releases verified on **2026-10-14**:

| Dependency/tool | Version |
|---|---|
| Zephyr | 4.4.2 |
| LVGL | 9.6.0 |
| Rust | 1.99.0 |
| egui / eframe | 0.36.2 |
| Zephyr SDK | 1.0.1 |
| Emscripten | 6.0.12 |
| wasm-bindgen / CLI | 0.2.129 |
| west | 1.5.0 |
| CMake | 4.4.4 |
| Ninja | 1.13.2 |
| esptool | 5.5.0 |
| clang-format | 23.1.3 |
| Playwright | 1.64.0 |

`Cargo.lock`, `rust-toolchain.toml`, `requirements-dev.txt`, and `west.yml` pin dependencies. [west-lock.yml](docs/west-lock.yml) records exact source commits. Espressif HAL follows Zephyr's supported revision. Zephyr 4.4's built-in LVGL integration targets an older source layout/Kconfig; this project builds LVGL 9.6 directly and supplies a small Zephyr display adapter. `CONFIG_LVGL=n` disables that older integration; the application still uses LVGL throughout.

## Build firmware

Use a west workspace with this repository named `smart-knob`:

```sh
python3 -m venv .venv
. .venv/bin/activate
pip install -r smart-knob/requirements-dev.txt
west init -l smart-knob
west update --narrow -o=--depth=1
pip install -r zephyr/scripts/requirements-base.txt
west blobs fetch hal_espressif
west sdk install --version 1.0.1 --gnu-toolchains xtensa-espressif_esp32s3_zephyr-elf
west build -b esp32s3_devkitc/esp32s3/procpu smart-knob/firmware -d smart-knob/build-firmware
```

On Linux, install the [Zephyr host prerequisites](https://docs.zephyrproject.org/latest/develop/getting_started/index.html), including a device-tree compiler. If the SDK is installed elsewhere, set `ZEPHYR_SDK_INSTALL_DIR`. The board overlay configures 16 MB flash; PSRAM is not required by this release.

The image is `build-firmware/zephyr/zephyr.bin`. To flash your connected device after completing bring-up checks:

```sh
west flash -d smart-knob/build-firmware --esp-device /dev/ttyACM0
```

The delivery archive also includes `artifacts/firmware.bin` for the same board. It uses ESP32 simple boot at address `0x0`:

```sh
esptool --chip esp32s3 --port /dev/ttyACM0 write-flash 0x0 artifacts/firmware.bin
```

Close the browser USB connection before flashing. This firmware uses its own NVS calibration format; recalibrate after replacing the factory firmware.

## Rust CLI

Install Rust using rustup; the repository toolchain file selects 1.99.0.

```sh
cargo run --locked -p knobctl -- ports
cargo run --locked -p knobctl -- validate examples/demo.json
cargo run --locked -p knobctl -- compile examples/demo.json demo.skv
cargo run --locked -p knobctl -- upload demo.skv --port /dev/ttyACM0 --save
cargo run --locked -p knobctl -- state --port /dev/ttyACM0
cargo run --locked -p knobctl -- calibrate --port /dev/ttyACM0
cargo run --locked -p knobctl -- select 4 --port /dev/ttyACM0
```

Additional commands: `demo OUTPUT`, `tare`, `stop`, `enable`, and `save`. Serial commands use the new protocol described in [protocol.md](docs/protocol.md); the factory Python/web clients use a different protocol.

## Build the browser editor

Activate Emscripten 6.0.12 in your shell, then:

```sh
cargo install wasm-bindgen-cli --version 0.2.129 --locked
scripts/build-web.sh
python3 scripts/serve.py
```

`LVGL_DIR` can point at a separate LVGL 9.6.0 checkout. Otherwise the script uses the west module. The editor's UI/model are Rust; `web/bridge.js` handles browser file dialogs, Web Serial, and the LVGL WASM module.

## Verify

```sh
scripts/check.sh
# With the editor served on localhost:8080:
npm ci --prefix tests
node tests/browser.mjs
```

Set `CHROMIUM_PATH` if Chromium is not at `/usr/bin/chromium`. On machines without an installed browser, install Chromium through Playwright and adjust the executable path. In ptrace-based sandboxes LeakSanitizer cannot enumerate threads; use `ASAN_OPTIONS=detect_leaks=0` there. Address and undefined-behavior checks remain active.

Tests cover bounds, fast detent transitions, angle wraparound, magnetic mode, voltage limits, PWM comparator spacing, encoder CRC, LED budget/direction/flashes, model validation, truncated/mutated bundles, Rust-to-C serial uploads, rejection preserving active views, actual LVGL rendering, and browser editing/undo/redo/persistence.

## Layout and attribution

`core/` contains portable C behavior, bundle decoding, serial protocol, and LVGL rendering. `firmware/` contains Zephyr hardware adapters. `crates/view-model` defines editable JSON and bundle compilation; `crates/editor` is the browser app; `crates/knobctl` is the CLI. `examples/demo.json` provides the six feedback presets.

Based on the hardware and behavior of [Seedlabs' firmware](https://github.com/SeedLabs-it/smartknob-firmware), inspected at commit `afd6632dca517cb9b8a20c761e033d8f3181c47e`, and the original [Scott Bez SmartKnob](https://github.com/scottbez1/smartknob). The upstream [license](LICENSE.md) is retained. Dependencies retain their own licenses; see [THIRD_PARTY.md](THIRD_PARTY.md).
