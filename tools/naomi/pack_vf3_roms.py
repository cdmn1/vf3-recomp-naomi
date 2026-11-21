#!/usr/bin/env python3
"""Pack a verified VF3 Revision D ROM ZIP behind a Naomi executable image.

No game data is included. The input ZIP must be supplied by the user.
"""
from __future__ import annotations

import argparse
from array import array
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import sys
import zipfile
import zlib

ROOT = Path(__file__).resolve().parent
PROFILE_PATH = ROOT / "vf3_rev_d_roms.json"
MAGIC = b"VF3NROM\0"
VERSION = 1
HEADER_SIZE = 4096
DEFAULT_PAYLOAD_OFFSET = 0x01000000
ALIGNMENT = 32
REGIONS = {
    "fixed_crom": (1, 0x800000),
    "banked_crom": (2, 0x4000000),
    "vrom": (3, 0x2000000),
}


class PackError(Exception):
    pass


def align_up(value: int, alignment: int = ALIGNMENT) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def place_rom(region: bytearray, rom: dict, data: bytes) -> None:
    chunk = int(rom["chunk"])
    stride = int(rom["stride"])
    offset = int(rom["offset"])
    if chunk <= 0 or stride < chunk or offset < 0 or len(data) % chunk:
        raise PackError(f"Invalid placement geometry for {rom['name']}")
    for src in range(0, len(data), chunk):
        dst = offset + (src // chunk) * stride
        piece = data[src:src + chunk]
        if rom["swap"]:
            piece = piece[::-1]
        if dst + chunk > len(region):
            raise PackError(f"{rom['name']} exceeds its {rom['region']} region")
        region[dst:dst + chunk] = piece


def to_sh4_word_order(data: bytearray) -> bytearray:
    if len(data) % 4:
        raise PackError("CROM region length must be a multiple of four")
    words = array("I")
    if words.itemsize != 4:
        raise PackError("Host array unsigned-int width is not 32 bits")
    words.frombytes(data)
    words.byteswap()
    return bytearray(words.tobytes())


def load_verified_roms(zip_path: Path, profile: dict) -> dict[str, bytearray]:
    sizes = {"crom": 0x200000, "banked": 0x4000000,
             "vrom": 0x2000000, "sound": 0x80000, "samples": 0x800000}
    raw = {name: bytearray(size) for name, size in sizes.items()}
    with zipfile.ZipFile(zip_path, "r") as archive:
        entries = archive.infolist()
        for rom in profile["roms"]:
            expected_crc = int(rom["crc32"], 16)
            basename = Path(rom["name"]).name
            candidates = [e for e in entries if Path(e.filename).name == basename]
            exact = next((e for e in candidates if e.CRC == expected_crc), None)
            if exact is None:
                exact = next((e for e in entries if e.CRC == expected_crc), None)
            if exact is None:
                raise PackError(f"Missing ROM: {rom['name']} (CRC {expected_crc:08x})")
            expected_size = int(rom["size"])
            if exact.file_size != expected_size:
                raise PackError(
                    f"Wrong ZIP entry size for {exact.filename}: {exact.file_size} bytes, "
                    f"expected {expected_size}"
                )
            try:
                data = archive.read(exact)
            except (OSError, zipfile.BadZipFile, RuntimeError) as exc:
                raise PackError(f"Could not decompress {exact.filename}: {exc}") from exc
            actual_crc = zlib.crc32(data) & 0xFFFFFFFF
            if actual_crc != expected_crc:
                raise PackError(f"CRC mismatch for {exact.filename}: {actual_crc:08x}")
            if len(data) != int(rom["size"]):
                raise PackError(
                    f"Wrong size for {exact.filename}: {len(data)} bytes, "
                    f"expected {int(rom['size'])}"
                )
            if rom["region"] in raw:
                place_rom(raw[rom["region"]], rom, data)
    # Match romset.cpp: fixed CROM is 6 MiB of banked ROM followed by 2 MiB program CROM.
    fixed = bytearray(0x800000)
    fixed[:0x600000] = raw["banked"][:0x600000]
    fixed[0x600000:] = raw["crom"]
    return {
        "fixed_crom": to_sh4_word_order(fixed),
        "banked_crom": to_sh4_word_order(raw["banked"]),
        "vrom": raw["vrom"],
    }


def make_header(profile_id: int, payload_size: int, regions: list[dict]) -> bytes:
    header = bytearray(HEADER_SIZE)
    struct.pack_into("<8sIIIIII", header, 0, MAGIC, VERSION, HEADER_SIZE,
                     profile_id, len(regions), payload_size, 0)
    at = 32
    for region in regions:
        struct.pack_into("<IIII", header, at, region["id"], region["offset"],
                         region["size"], region["crc32"])
        at += 16
    crc = zlib.crc32(header) & 0xFFFFFFFF
    struct.pack_into("<I", header, 28, crc)
    return bytes(header)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def pack(executable: Path, zip_path: Path, output: Path,
         payload_offset: int = DEFAULT_PAYLOAD_OFFSET) -> dict:
    if payload_offset < 0 or payload_offset % ALIGNMENT:
        raise PackError("Payload offset must be non-negative and 32-byte aligned")
    if output.resolve() in (executable.resolve(), zip_path.resolve()):
        raise PackError("Output must not overwrite the executable or input ZIP")
    profile = json.loads(PROFILE_PATH.read_text(encoding="utf-8"))
    executable_size = executable.stat().st_size
    if executable_size > payload_offset:
        raise PackError(
            f"Executable is {executable_size} bytes, larger than reserved "
            f"payload offset {payload_offset}; choose a higher aligned offset"
        )
    regions_data = load_verified_roms(zip_path, profile)
    data_offset = payload_offset + HEADER_SIZE
    descriptors = []
    manifest_regions = []
    current = data_offset
    for name, (region_id, _) in REGIONS.items():
        data = regions_data[name]
        crc = zlib.crc32(data) & 0xFFFFFFFF
        descriptors.append({"id": region_id, "offset": current,
                            "size": len(data), "crc32": crc})
        manifest_regions.append({
            "name": name, "offset": current, "size": len(data),
            "crc32": f"{crc:08x}", "sha256": hashlib.sha256(data).hexdigest()
        })
        current = align_up(current + len(data))
    payload_size = current - payload_offset
    header = make_header(int(profile["profile_id"]), payload_size, descriptors)

    output.parent.mkdir(parents=True, exist_ok=True)
    with executable.open("rb") as source, output.open("wb") as target:
        shutil.copyfileobj(source, target, 1024 * 1024)
        target.write(b"\xFF" * (payload_offset - executable_size))
        target.write(header)
        for name in REGIONS:
            target.write(regions_data[name])
        target.truncate(current)

    manifest = {
        "format": "VF3NROM", "version": VERSION,
        "profile": profile["profile"], "title": profile["title"],
        "executable": str(executable), "executable_size": executable_size,
        "source_zip_sha256": sha256_file(zip_path),
        "payload_offset": payload_offset, "payload_size": payload_size,
        "image_size": current, "regions": manifest_regions,
        "omitted_regions": {
            "sound": "Validated against the profile but not packed: current VF3 board core does not consume the sound ROM.",
            "samples": "Validated against the profile but not packed: current VF3 board core does not consume sample ROMs."
        },
        "commercial_game_data_in_repository": False
    }
    manifest_path = output.with_suffix(output.suffix + ".json")
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path, help="built Naomi .bin executable")
    parser.add_argument("rom_zip", type=Path, help="user-supplied VF3 Revision D ROM ZIP")
    parser.add_argument("output", type=Path, help="combined NetDIMM image to create")
    parser.add_argument("--payload-offset", type=lambda x: int(x, 0),
                        default=DEFAULT_PAYLOAD_OFFSET,
                        help="reserved image offset for VF3 data (default: 0x01000000)")
    args = parser.parse_args()
    try:
        manifest = pack(args.executable, args.rom_zip, args.output, args.payload_offset)
    except (PackError, OSError, zipfile.BadZipFile, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    print(f"Created: {args.output}")
    print(f"Image size: {manifest['image_size']} bytes")
    print(f"Payload offset: 0x{manifest['payload_offset']:08x}")
    for region in manifest["regions"]:
        print(f"{region['name']}: offset=0x{region['offset']:08x} "
              f"size={region['size']} CRC32={region['crc32']}")
    print(f"Manifest: {args.output}.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
