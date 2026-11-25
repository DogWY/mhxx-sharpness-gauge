"""Build a paired MHGU 1.4.0 NSO + IPS32 from a local updated ExeFS dump."""
import argparse
import ctypes
import ctypes.util
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

ROOT = Path(__file__).resolve().parents[1]
MAIN_ID = bytes.fromhex("FB08F1D20FD1204FE7070672777F869B").ljust(32, b"\0")
SUBSDK0_ID = bytes.fromhex("C2774FA4CACE9ECC6A712BFC839214C19515F36E").ljust(32, b"\0")
TEXT_SHA = "09ff3ff102cf7aa14c9be6be8b7fa70304ba7ab57ccd1215b17f7fb106428df0"
# The released damage-display subsdk1 maps 0x5000 bytes. Two sets of
# PC-relative landing pads share the same absolute addresses across layouts.
DAMAGE_EXTENT = 0x5000
FIRST_PAD = 0x100
HOOKS = (
    (0xBBE674, 0xE1A04000, "capture"),
    (0xBBD92C, 0xE0850000, "worker"),
    (0xBBE778, 0xE0850000, "present"),
)


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def nso(path, expected):
    raw = path.read_bytes()
    if raw[:4] != b"NSO0" or raw[64:96] != expected:
        raise ValueError(f"{path}: expected MHGU Switch v1.4.0 {expected.hex()}")
    end = sum(struct.unpack_from("<III", raw, 0x34))
    return raw, (end + 4095) & ~4095


def retail_text(raw):
    offset, _, size = struct.unpack_from("<III", raw, 0x10)
    text = raw[offset:offset + u32(raw, 0x60)]
    if u32(raw, 12) & 1:
        name = ctypes.util.find_library("lz4") or "/opt/homebrew/opt/lz4/lib/liblz4.dylib"
        lib = ctypes.CDLL(name)
        out = ctypes.create_string_buffer(size)
        if lib.LZ4_decompress_safe(text, out, len(text), size) != size:
            raise ValueError("Cannot decode retail .text")
        text = out.raw
    if hashlib.sha256(text).hexdigest() != TEXT_SHA:
        raise ValueError("Retail .text does not match the validated MHGU v1.4.0 build")
    return text


def arm_branch(site, target, link):
    delta = target - site - 8
    if delta % 4 or not -(1 << 25) <= delta < (1 << 25):
        raise ValueError(f"Unreachable ARM branch {site:#x} -> {target:#x}")
    return (0xEB000000 if link else 0xEA000000) | (delta // 4 & 0xffffff)


def package_ips(text, module_offset, symbols, image):
    data = bytearray(b"IPS32")
    for index, (site, expected, name) in enumerate(HOOKS):
        if u32(text, site) != expected:
            raise ValueError(f"Unexpected instruction for {name} at {site:#x}")
        pad = FIRST_PAD + 4 * index
        target = module_offset + DAMAGE_EXTENT + pad
        for preceding_extent, label in ((0, "solo"), (DAMAGE_EXTENT, "paired")):
            landing = target - (module_offset + preceding_extent)
            if landing != symbols[f"{label}_{name}"]:
                raise ValueError(f"Unexpected {label} layout for {name}")
            expected_branch = arm_branch(landing, symbols[f"handle_{name}"], False)
            if u32(image, u32(image, 16) + landing) != expected_branch:
                raise ValueError(f"Invalid {label} landing pad for {name}")
        word = arm_branch(site, target, True)
        data += struct.pack(">IH", site + 0x100, 4) + struct.pack("<I", word)
    return data + b"EEOF"


def package_nso(elf):
    phoff = u32(elf, 28)
    phsize, count = struct.unpack_from("<HH", elf, 42)
    loads = [struct.unpack_from("<8I", elf, phoff + i * phsize) for i in range(count)]
    loads = [s for s in loads if s[0] == 1]
    if [s[6] for s in loads] != [5, 4, 6]:
        raise ValueError("Unexpected module ELF segments")
    header, payload = bytearray(256), bytearray()
    struct.pack_into("<4sIII", header, 0, b"NSO0", 0, 0, 0x38)
    for i, (_, pos, addr, _, size, _, _, _) in enumerate(loads):
        part = elf[pos:pos + size]
        struct.pack_into("<III", header, 16 + i*16, 256 + len(payload), addr, size)
        struct.pack_into("<I", header, 96 + i*4, size)
        header[160+i*32:192+i*32] = hashlib.sha256(part).digest()
        payload.extend(part)
    struct.pack_into("<I", header, 60, loads[-1][5] - loads[-1][4])
    header[64:96] = hashlib.sha256(payload).digest()
    return header + payload


def link(objects, output):
    path = os.environ.get("LD_LLD") or shutil.which("ld.lld")
    if path:
        linker = [path]
    else:
        rust = subprocess.check_output(["rustc", "--print", "sysroot"], text=True).strip()
        linker = [str(next((Path(rust)/"lib/rustlib").glob("*/bin/rust-lld"))), "-flavor", "gnu"]
    subprocess.run(linker + ["-T", ROOT/"src/module.ld", "--no-undefined", *objects, "-o", output],
                   check=True)


def ips_ranges(data):
    if not data.startswith(b"IPS32") or not data.endswith(b"EEOF"):
        raise ValueError("Expected an IPS32 patch")
    pos = 5
    limit = len(data) - 4
    while pos < limit:
        site, size = struct.unpack_from(">IH", data, pos)
        pos += 6
        if size:
            pos += size
        else:
            size = struct.unpack_from(">H", data, pos)[0]
            pos += 3
        if pos > limit or not size:
            raise ValueError("Malformed IPS32 patch record")
        yield site, site + size


def check_damage_package(path, gauge_ips):
    prefix = "MHGU-Damage-Display/exefs/"
    with ZipFile(path) as archive:
        module = archive.read(prefix + "subsdk1")
        if module[:4] != b"NSO0":
            raise ValueError("Invalid damage-display module")
        extent = (sum(struct.unpack_from("<III", module, 0x34)) + 4095) & ~4095
        if extent != DAMAGE_EXTENT:
            raise ValueError(f"Unsupported damage-display module extent {extent:#x}")
        damage_ips = archive.read(prefix + MAIN_ID.hex().upper() + ".ips")
    gauge_ranges = tuple(ips_ranges(gauge_ips))
    for start, end in ips_ranges(damage_ips):
        if any(start < g_end and g_start < end for g_start, g_end in gauge_ranges):
            raise ValueError(f"Damage-display IPS overlap at {start:#x}..{end:#x}")


def build(exefs, probe=False, diagnostic=False, damage_zip=None):
    main, main_extent = nso(exefs/"main", MAIN_ID)
    _, middleware_extent = nso(exefs/"subsdk0", SUBSDK0_ID)
    module_offset = main_extent + middleware_extent
    if module_offset != 0x2715000 or (exefs/"subsdk1").exists():
        raise ValueError("This mod requires the standard retail main/subsdk0 layout")
    text = retail_text(main)
    name = "MHGU-Sharpness-Gauge"
    if probe:
        name = "MHGU-Sharpness-Probe"
    elif diagnostic:
        name = "MHGU-Sharpness-Diagnostic"
    destination = ROOT / "dist" / (name + ".zip")
    with tempfile.TemporaryDirectory(prefix="mhgu-build-") as tmp:
        work = Path(tmp)
        objects = []
        flags = ["--target=armv7a-none-eabi", "-marm", "-Os", "-fPIC", "-ffreestanding",
                 "-fno-unwind-tables", "-fno-asynchronous-unwind-tables", "-Wall",
                 "-Wextra", "-Werror", "-fvisibility=hidden"]
        if probe:
            flags.append("-DMHGU_SHARPNESS_PROBE")
        if diagnostic:
            flags.append("-DMHGU_SHARPNESS_DIAGNOSTIC")
        for src in ("entry.S", "entry.c", "sharpness.c", "render.c"):
            obj = work / (src + ".o")
            subprocess.run(["clang", *flags, "-c", ROOT/"src"/src, "-o", obj], check=True)
            objects.append(obj)
        elf = work/"mod.elf"
        link(objects, elf)
        symbols = {}
        for line in subprocess.check_output(["nm", "-n", elf], text=True).splitlines():
            fields = line.split()
            if len(fields) == 3 and fields[2].startswith(("solo_", "paired_", "handle_")):
                symbols[fields[2]] = int(fields[0], 16)
        image = package_nso(elf.read_bytes())
        ips = package_ips(text, module_offset, symbols, image)
        if damage_zip:
            check_damage_package(damage_zip, ips)
        destination.parent.mkdir(exist_ok=True)
        # Stable entry metadata keeps identical builds byte-for-byte reproducible.
        with tempfile.TemporaryDirectory(dir=destination.parent) as staging:
            package = Path(staging) / destination.name
            with ZipFile(package, "w") as archive:
                for filename, content in (("subsdk2", image), (MAIN_ID.hex().upper() + ".ips", ips)):
                    entry = ZipInfo(destination.stem + "/exefs/" + filename)
                    entry.external_attr = 0o100644 << 16
                    archive.writestr(entry, content, compress_type=ZIP_DEFLATED)
            package.replace(destination)
    print(destination)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exefs", type=Path, help="local Switch GU v1.4.0 ExeFS directory")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--probe", action="store_true", help="draw a square to verify module loading and coexistence")
    mode.add_argument("--diagnostic", action="store_true", help="display gauge values for gameplay validation")
    parser.add_argument("--check-damage-zip", type=Path, help="verify a damage-display release ZIP for coexistence")
    args = parser.parse_args()
    build(args.exefs, args.probe, args.diagnostic, args.check_damage_zip)
