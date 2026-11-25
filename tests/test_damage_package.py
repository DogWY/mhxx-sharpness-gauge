import io
from pathlib import Path
import struct
import sys
import unittest
from zipfile import ZipFile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from build_mod import MAIN_ID, check_damage_package


def module_image(compressed=False):
    # Each segment consists of one ARM word. A four-byte LZ4 literal run
    # takes five bytes on disk, so the last file bound differs across modes.
    word = bytes.fromhex("1eff2fe1")
    stored = b"\x40" + word if compressed else word
    header = bytearray(0x100)
    header[:4] = b"NSO0"
    struct.pack_into("<I", header, 12, 7 if compressed else 0)
    for i in range(3):
        struct.pack_into("<III", header, 16 + i * 16,
                         0x100 + i * len(stored), 0x1000 * i, len(word))
        struct.pack_into("<I", header, 0x60 + i * 4, len(stored))
    struct.pack_into("<I", header, 0x3c, 0x5000 - 0x2000 - len(word))
    return header + stored * 3


def check(module, extra_entries=()):
    prefix = "MHGU-Damage-Display/exefs/"
    patch = b"IPS32EEOF"
    buffer = io.BytesIO()
    with ZipFile(buffer, "w") as archive:
        archive.writestr(prefix + "subsdk1", module)
        archive.writestr(prefix + MAIN_ID.hex().upper() + ".ips", patch)
        for name in extra_entries:
            archive.writestr(name, b"")
    check_damage_package(buffer, patch)


class DamagePackageTest(unittest.TestCase):
    def test_reference_layout(self):
        for compressed in (False, True):
            with self.subTest(compressed=compressed):
                check(module_image(compressed))

    def test_extra_gauge_slot(self):
        for name in ("MHGU-Damage-Display/exefs/subsdk2", "Extra-Mod/ExeFS/Subsdk2"):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "subsdk2"):
                check(module_image(), [name])

    def test_truncated_header_and_payload(self):
        for module in (module_image()[:64], module_image()[:255],
                       module_image()[:-1], module_image(True)[:-1]):
            with self.subTest(length=len(module)), self.assertRaises(ValueError):
                check(module)

    def test_invalid_segment_ranges(self):
        for field, value in ((0x10, 0xf0), (0x20, 0x102), (0x24, 0), (0x34, 0x1000)):
            module = module_image()
            struct.pack_into("<I", module, field, value)
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "range"):
                check(module)

    def test_wrong_mapped_extent(self):
        module = module_image()
        struct.pack_into("<I", module, 0x3c, 0)
        with self.assertRaisesRegex(ValueError, "extent"):
            check(module)


if __name__ == "__main__":
    unittest.main()
