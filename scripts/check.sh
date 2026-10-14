#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cargo fmt --all -- --check
cargo clippy --locked --workspace --all-targets -- -D warnings
cargo clippy --locked -p knob-editor --target wasm32-unknown-unknown -- -D warnings
cargo test --locked --workspace
cargo build --locked -p knob-editor --target wasm32-unknown-unknown
cmake -S . -B build-host -G Ninja -DLVGL_DIR="${LVGL_DIR:-../modules/lib/gui/lvgl}" -DSK_SANITIZE=ON
cmake --build build-host --parallel "${BUILD_JOBS:-4}"
ctest --test-dir build-host --output-on-failure
cargo run --locked -p knobctl -- compile examples/demo.json build-host/demo.skv
build-host/bundle_check build-host/demo.skv
build-host/fuzz_bundle build-host/demo.skv
python3 tests/integration.py
