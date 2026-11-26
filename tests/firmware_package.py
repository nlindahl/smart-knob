"""Exercise install layout validation independently of the cross compiler."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

from esptool.bin_image import ESP32S3FirmwareImage, ImageSegment

spec = importlib.util.spec_from_file_location("package_firmware", Path(__file__).resolve().parents[1] / "scripts/package-firmware.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class FirmwarePackageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        self.zephyr = self.build / "zephyr"
        self.zephyr.mkdir(parents=True)
        (self.zephyr / ".config").write_text('CONFIG_ESP_SIMPLE_BOOT=y\nCONFIG_SOC_ESP32S3=y\nCONFIG_FLASH_LOAD_OFFSET=0\nCONFIG_ESPTOOLPY_FLASHSIZE="16MB"\n')
        (self.zephyr / "runners.yaml").write_text("args:\n  esp32: [--esp-app-address=0x0]\n")
        (self.zephyr / "zephyr.dts").write_text(self.dts())
        image = ESP32S3FirmwareImage()
        image.flash_mode = 2
        image.flash_size_freq = 0x4F
        image.segments = [ImageSegment(0x40370000, bytes(range(128)))]
        image.segments[0].name = ".text"
        image.save(str(self.zephyr / "zephyr.bin"))

    @staticmethod
    def dts(storage="reg = <0xf00000 0x40000>;", ranges="< 0x0 0x0 0x1000000 >"):
        return ("flash0: flash@0 { /* soc */ reg = < 0x0 0x1000000 >; ranges = " + ranges + "; "
                "partitions { storage_partition: partition@f00000 { " + storage + " }; }; };")

    def package(self, version="dev-123456789abc"):
        return module.package(self.build, self.root / "output", version, "a" * 40)

    def test_manifest_and_checksums(self):
        directory = self.package()
        manifest = json.loads((directory / "manifest.json").read_text())
        self.assertTrue(manifest["new_install_prompt_erase"])
        self.assertEqual(manifest["new_install_improv_wait_time"], 0)
        self.assertEqual(manifest["builds"], [{"chipFamily": "ESP32-S3", "parts": [{"path": "firmware.bin", "offset": 0}]}])
        import hashlib
        for line in (directory / "SHA256SUMS").read_text().splitlines():
            digest, name = line.split("  ")
            self.assertEqual(digest, hashlib.sha256((directory / name).read_bytes()).hexdigest())
        self.assertFalse(json.loads((directory / "build.json").read_text())["hardwareValidated"])

    def test_reject_wrong_runner_offset(self):
        (self.zephyr / "runners.yaml").write_text("args:\n  esp32: [--esp-app-address=0x10000]\n")
        with self.assertRaisesRegex(ValueError, "offset"):
            self.package()

    def test_reject_changed_storage(self):
        (self.zephyr / "zephyr.dts").write_text(self.dts("reg = <0xe00000 0x40000>;"))
        with self.assertRaisesRegex(ValueError, "partition changed"):
            self.package()

    def test_reject_wrong_chip_and_header(self):
        binary = bytearray((self.zephyr / "zephyr.bin").read_bytes())
        binary[12] = 0
        (self.zephyr / "zephyr.bin").write_bytes(binary)
        with self.assertRaises(ValueError):
            self.package()

    def test_reject_corrupt_image_checksum(self):
        binary = bytearray((self.zephyr / "zephyr.bin").read_bytes())
        binary[40] ^= 1
        (self.zephyr / "zephyr.bin").write_bytes(binary)
        with self.assertRaisesRegex(ValueError, "checksum"):
            self.package()

    def test_reject_default_2mb_header_config(self):
        # Zephyr 4.5 defaults the header to 2 MB unless the board/app sets it.
        config = (self.zephyr / ".config").read_text().replace('"16MB"', '"2MB"')
        (self.zephyr / ".config").write_text(config)
        with self.assertRaisesRegex(ValueError, "16 MB"):
            self.package()

    def test_reject_flash_ranges_smaller_than_reg(self):
        (self.zephyr / "zephyr.dts").write_text(self.dts(ranges="< 0x0 0x0 0x800000 >"))
        with self.assertRaisesRegex(ValueError, "reg and ranges"):
            self.package()

    def test_reject_mcuboot(self):
        with (self.zephyr / ".config").open("a") as stream:
            stream.write("CONFIG_BOOTLOADER_MCUBOOT=y\n")
        with self.assertRaisesRegex(ValueError, "MCUboot"):
            self.package()

    def test_reject_oversized_image(self):
        with (self.zephyr / "zephyr.bin").open("r+b") as stream:
            stream.truncate(0xF00001)
        with self.assertRaisesRegex(ValueError, "overlap"):
            self.package()

    def test_reject_unsafe_version(self):
        with self.assertRaisesRegex(ValueError, "safe directory"):
            self.package("../oops")


if __name__ == "__main__":
    unittest.main()
