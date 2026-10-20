# Browser firmware artifacts

After building the Dev Kit v0.1 firmware, install `requirements-dev.txt` and run:

```sh
python3 tests/firmware_package.py
python3 scripts/package-firmware.py --version "dev-$(git rev-parse --short=12 HEAD)" --commit "$(git rev-parse HEAD)"
```

The packager checks the generated Kconfig, runner arguments, device tree, image header, checksum, and sector-aligned write boundary. It accepts only ESP32-S3, 16 MB flash, DIO at 80 MHz, Zephyr simple boot at offset `0x0`, and the current `0xF00000`–`0xF3FFFF` saved-data partition. A layout change must be reviewed before changing those checks.

Simple boot puts boot code and application in one `zephyr.bin`. Do not add ESP-IDF bootloader or partition-table files: their conventional offsets would overwrite this image. The generated ESP Web Tools manifest has one part, `firmware.bin`, at offset zero. The image's flash settings are retained by the installer.

Output is `build-firmware-package/<version>/` with a manifest, binary, build metadata, recovery commands, and SHA-256 checksums. `latest.json` identifies that exact version. A separate `smartknob-devkit-v0.1-<version>.zip` is suitable for release downloads; extract it before using the manifest locally. Metadata explicitly records that hardware has not been validated.

The manifest asks whether to erase the device and skips Improv/Wi-Fi provisioning, which this firmware does not implement. A normal write erases only firmware sectors and preserves this project's current NVS layout. Full erase removes saved views, calibration, and factory data. Factory configuration is incompatible with this project.

Checksums identify downloaded bytes; they are not firmware signatures. Install only builds from a source you trust. Hardware bring-up and real-device recovery remain documented in [hardware.md](hardware.md).

## CI and releases

The reusable `Build installable firmware` workflow cross-builds and packages the firmware. Check CI retains the versioned archive and manifest directory as `firmware-download` and `firmware-site` artifacts for 90 days. Pages builds firmware from the same commit as the editor, downloads that manifest directory, and publishes it at `firmware/<version>/` with a `latest.json` pointer. Browser integration tests exercise the actual ESP Web Tools dialog with mocked USB/chip I/O before deployment.

The hosted installer follows `main`, with a visible `dev-<commit>` version; it does not imply a stable release. To publish a retained version, push a `v*` tag pointing at the reviewed commit. The `Release firmware downloads` workflow builds that tag and creates a GitHub release containing its versioned ZIP archive. Older release downloads remain on GitHub Releases when Pages is updated. A tag does not replace the Pages deployment, which continues to track `main`. No release tags are created by the implementation itself.

For a complete local web build, run `scripts/build-web.sh`, package a firmware build as above, then run `python3 scripts/package-pages.py`. Serve `build-pages` over localhost. `scripts/serve.py` serves the editor-only `web` directory; it does not stage firmware artifacts automatically.
