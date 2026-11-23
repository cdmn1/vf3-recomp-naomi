#!/usr/bin/env python3
"""Append a synthetic VF3NROM image for Naomi NetDIMM/VROM integration tests.

The image contains zero-filled CROM/VROM regions and one tiny synthetic
triangle model. It contains no commercial game data.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import struct
import sys
import zlib

PAYLOAD_OFFSET = 0x01000000
HEADER_SIZE = 4096
ALIGNMENT = 32
FIXED_SIZE = 0x00800000
BANKED_SIZE = 0x04000000
VROM_SIZE = 0x02000000
MODEL_WORD_ADDRESS = 0x00100000
MODEL_BYTE_OFFSET = MODEL_WORD_ADDRESS * 4
CHUNK = 1024 * 1024

MODEL_WORDS = (
    0x00000000, 0x00000006, 0x00000000, 0x00000000,
    0xFFFFFFFF, 0x00000000, 0x00800000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00010000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00010000, 0x00000000, 0x00000000,
)
MODEL_BYTES = struct.pack("<19I", *MODEL_WORDS)


def align_up(value: int) -> int:
    return (value + ALIGNMENT - 1) & ~(ALIGNMENT - 1)


def crc_zero_region(size: int) -> int:
    block = bytes(CHUNK)
    crc = 0
    remaining = size
    while remaining:
        take = min(remaining, len(block))
        crc = zlib.crc32(block[:take], crc)
        remaining -= take
    return crc & 0xFFFFFFFF


def crc_vrom_region() -> int:
    crc = 0
    patch_start = MODEL_BYTE_OFFSET
    patch_end = patch_start + len(MODEL_BYTES)
    for offset in range(0, VROM_SIZE, CHUNK):
        length = min(CHUNK, VROM_SIZE - offset)
        block = bytearray(length)
        start = max(offset, patch_start)
        end = min(offset + length, patch_end)
        if start < end:
            block[start - offset:end - offset] = MODEL_BYTES[start - patch_start:end - patch_start]
        crc = zlib.crc32(block, crc)
    return crc & 0xFFFFFFFF


def make_header(payload_size: int, regions: list[dict]) -> bytes:
    header = bytearray(HEADER_SIZE)
    struct.pack_into("<8sIIIIII", header, 0, b"VF3NROM\\0", 1,
                     HEADER_SIZE, 1, len(regions), payload_size, 0)
    at = 32
    for region in regions:
        struct.pack_into("<IIII", header, at, region["id"], region["offset"],
                         region["size"], region["crc32"])
        at += 16
    struct.pack_into("<I", header, 28, zlib.crc32(header) & 0xFFFFFFFF)
    return bytes(header)


def build(executable: Path, output: Path) -> dict:
    executable_size = executable.stat().st_size
    if executable_size > PAYLOAD_OFFSET:
        raise ValueError(f"Executable ({executable_size}) exceeds 16 MiB payload offset")
    if executable.resolve() == output.resolve():
        raise ValueError("Output must not overwrite the executable")

    fixed_offset = PAYLOAD_OFFSET + HEADER_SIZE
    banked_offset = align_up(fixed_offset + FIXED_SIZE)
    vrom_offset = align_up(banked_offset + BANKED_SIZE)
    image_end = vrom_offset + VROM_SIZE
    payload_size = image_end - PAYLOAD_OFFSET
    regions = [
        {"id": 1, "offset": fixed_offset, "size": FIXED_SIZE,
         "crc32": crc_zero_region(FIXED_SIZE)},
        {"id": 2, "offset": banked_offset, "size": BANKED_SIZE,
         "crc32": crc_zero_region(BANKED_SIZE)},
        {"id": 3, "offset": vrom_offset, "size": VROM_SIZE,
         "crc32": crc_vrom_region()},
    ]
    header = make_header(payload_size, regions)

    output.parent.mkdir(parents=True, exist_ok=True)
    with executable.open("rb") as source, output.open("wb") as target:
        shutil.copyfileobj(source, target, CHUNK)
        remaining = PAYLOAD_OFFSET - executable_size
        pad = b"\\xFF" * CHUNK
        while remaining:
            take = min(remaining, len(pad))
            target.write(pad[:take])
            remaining -= take
        target.write(header)
        target.truncate(image_end)
        target.seek(vrom_offset + MODEL_BYTE_OFFSET)
        target.write(MODEL_BYTES)

    manifest = {
        "format": "VF3NROM", "version": 1,
        "purpose": "synthetic Naomi NetDIMM VROM/SceneBuilder integration test",
        "executable": str(executable), "executable_size": executable_size,
        "payload_offset": PAYLOAD_OFFSET, "payload_size": payload_size,
        "image_size": image_end, "model_word_address": MODEL_WORD_ADDRESS,
        "model_words": len(MODEL_WORDS),
        "commercial_game_data_included": False,
        "regions": regions,
    }
    output.with_suffix(output.suffix + ".json").write_text(
        json.dumps(manifest, indent=2) + "\\n", encoding="utf-8")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        manifest = build(args.executable, args.output)
    except (OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    print(f"Created: {args.output}")
    print(f"Image size: {manifest['image_size']} bytes")
    print(f"VROM model: word address 0x{MODEL_WORD_ADDRESS:06x}, {len(MODEL_WORDS)} words")
    print("Commercial game data: not included")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
