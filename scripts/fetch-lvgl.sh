#!/usr/bin/env bash
# Fetch the LVGL source used by host tests and the browser preview.
# It must be the exact module Zephyr's manifest pins for firmware, so the
# preview renders with the same LVGL as the device. The firmware workflow
# checks this revision against the west workspace.
set -euo pipefail
LVGL_REPO=https://github.com/zephyrproject-rtos/lvgl
LVGL_REVISION=c9ffd4692de246eb911c7ba49b78a331b2c63c74 # Zephyr v4.5.0, LVGL 9.6.0
if [ "${1:-}" = "--revision" ]; then
  echo "$LVGL_REVISION"
  exit 0
fi
dest="${1:?usage: fetch-lvgl.sh DEST | --revision}"
git init -q "$dest"
git -C "$dest" fetch -q --depth 1 "$LVGL_REPO" "$LVGL_REVISION"
git -C "$dest" checkout -q FETCH_HEAD
