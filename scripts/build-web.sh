#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
task_lvgl_dir="${LVGL_DIR:-../modules/lib/gui/lvgl}"
npm ci --prefix web
python3 scripts/build-installer.py
command -v emcmake >/dev/null || { echo 'Activate Emscripten 6.0.12 first.' >&2; exit 1; }
command -v wasm-bindgen >/dev/null || { echo 'Install wasm-bindgen-cli 0.2.129 first.' >&2; exit 1; }
emcmake cmake -S . -B build-preview -G Ninja -DLVGL_DIR="$task_lvgl_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build build-preview --parallel "${BUILD_JOBS:-4}"
cargo build --locked -p knob-editor --target wasm32-unknown-unknown --release
wasm-bindgen --target web --out-dir web/pkg target/wasm32-unknown-unknown/release/knob_editor.wasm
cp build-preview/preview.js build-preview/preview.wasm web/
echo 'Ready: python3 -m http.server 8080 --directory web'
