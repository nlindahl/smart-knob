# Attribution

The Dev Kit v0.1 pin map, haptic behavior, encoder framing, and calibration approach were informed by [Seedlabs smartknob-firmware](https://github.com/SeedLabs-it/smartknob-firmware) at `afd6632dca517cb9b8a20c761e033d8f3181c47e`. Its license is retained in `LICENSE.md`. Seedlabs credits the original [Scott Bez SmartKnob](https://github.com/scottbez1/smartknob). This implementation does not include the Arduino runtime or link SimpleFOC.

External dependencies are fetched through west/Cargo and retain their notices and licenses. Important dependencies include Zephyr (Apache-2.0), LVGL (MIT), egui/eframe (MIT OR Apache-2.0), Rust tooling, wasm-bindgen, minicbor, serde/serde_json, clap, serialport, and the Espressif HAL and its optional binary components. Use their source distributions and license files as authoritative; `Cargo.lock` and `docs/west-lock.yml` identify the exact dependencies.

The prebuilt browser artifacts include LVGL and Rust dependencies. Source and rebuilding instructions are supplied alongside them.
