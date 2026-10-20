"""Validate Zephyr's simple-boot layout and package a browser-installable build."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil

from esptool.bin_image import LoadFirmwareImage
import yaml


def package(build, output, version, commit):
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,79}", version):
        raise ValueError("Version must be a safe directory name")
    if not re.fullmatch(r"[a-f0-9]{40}", commit):
        raise ValueError("Commit must be a full Git SHA")
    zephyr = Path(build) / "zephyr"
    config = dict(re.findall(r"^(CONFIG_\w+)=(.+)$", (zephyr / ".config").read_text(), re.M))
    required = {"CONFIG_ESP_SIMPLE_BOOT": "y", "CONFIG_SOC_ESP32S3": "y",
                "CONFIG_FLASH_LOAD_OFFSET": "0x0", "CONFIG_FLASH_SIZE": "16777216"}
    if any(config.get(k) != v for k, v in required.items()):
        raise ValueError("Expected ESP32-S3 16 MB simple-boot image at offset 0")
    if config.get("CONFIG_BOOTLOADER_MCUBOOT") == "y":
        raise ValueError("MCUboot images need a different install manifest")
    runner = yaml.safe_load((zephyr / "runners.yaml").read_text())
    if "--esp-app-address=0x0" not in runner["args"]["esp32"]:
        raise ValueError("Runner flash offset disagrees with the install manifest")
    dts = re.sub(r"/\*.*?\*/", "", (zephyr / "zephyr.dts").read_text(), flags=re.S)
    storage = re.search(r"storage_partition:\s*\w+@\w+\s*\{[^}]*reg\s*=\s*<\s*(0x\w+)\s+(0x\w+)\s*>", dts)
    if not storage or tuple(int(v, 16) for v in storage.groups()) != (0xF00000, 0x40000):
        raise ValueError("Saved-data partition changed; review update compatibility first")
    binary = (zephyr / "zephyr.bin").read_bytes()
    # Erase/write operations are sector aligned. They must not reach saved data.
    if not binary or ((len(binary) + 4095) // 4096) * 4096 > 0xF00000:
        raise ValueError("Firmware would overlap saved views and calibration")
    image = LoadFirmwareImage("esp32s3", binary)
    if image.chip_id != 9 or image.flash_mode != 2 or image.flash_size_freq != 0x4F:
        raise ValueError("Expected ESP32-S3 image with DIO, 16 MB, 80 MHz header")
    if image.checksum != image.calculate_checksum():
        raise ValueError("Firmware image checksum is invalid")
    directory = Path(output) / version
    directory.mkdir(parents=True, exist_ok=False)
    (directory / "firmware.bin").write_bytes(binary)
    manifest = {"name": "SmartKnob Dev Kit v0.1 — Zephyr", "version": version,
                "new_install_prompt_erase": True, "new_install_improv_wait_time": 0,
                "builds": [{"chipFamily": "ESP32-S3", "parts": [{"path": "firmware.bin", "offset": 0}]}]}
    details = {"version": version, "commit": commit, "hardware": "Seedlabs SmartKnob Dev Kit v0.1 (16 MB)",
               "hardwareValidated": False, "chipFamily": "ESP32-S3", "offset": 0,
               "size": len(binary), "sha256": hashlib.sha256(binary).hexdigest(),
               "storage": {"offset": 0xF00000, "size": 0x40000}, "boot": "zephyr-simple"}
    for name, value in (("manifest.json", manifest), ("build.json", details)):
        (directory / name).write_text(json.dumps(value, indent=2) + "\n")
    (directory / "README.txt").write_text(
        f"SmartKnob Dev Kit v0.1 / {version}\nSource commit: {commit}\n"
        "Hardware operation has not been validated. See docs/hardware.md in the source repository.\n"
        "Single Zephyr simple-boot image; no separate bootloader or partition table.\n"
        "Normal writes preserve this project's NVS; full erase removes all saved data.\n"
        "Factory configuration is incompatible. Back up flash before replacing factory firmware.\n\n"
        "Verify: sha256sum -c SHA256SUMS\n"
        "Install: esptool --chip esp32s3 --port PORT write-flash 0x0 firmware.bin\n"
        "Backup: esptool --chip esp32s3 --port PORT read-flash 0x0 0x1000000 backup.bin\n"
        "Restore: esptool --chip esp32s3 --port PORT write-flash 0x0 backup.bin\n")
    files = sorted(directory.iterdir())
    (directory / "SHA256SUMS").write_text("".join(
        f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in files))
    (Path(output) / "latest.json").write_text(json.dumps({
        "version": version, "manifest": f"{version}/manifest.json", "details": f"{version}/build.json"
    }, indent=2) + "\n")
    archive = Path(output).parent / f"smartknob-devkit-v0.1-{version}"
    shutil.make_archive(str(archive), "zip", directory)
    print(f"Packaged {version}: {len(binary)} bytes at 0x0; SHA256 {details['sha256']}")
    return directory


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("build-firmware"))
    parser.add_argument("--output", type=Path, default=Path("build-firmware-package"))
    parser.add_argument("--version", required=True)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    package(args.build, args.output, args.version, args.commit)
