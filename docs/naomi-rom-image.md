# VF3 Naomi NetDIMM ROM image

The offline packer creates a combined Naomi executable + VF3 Revision D ROM image. It never modifies the supplied executable or ZIP, and no commercial ROM bytes are stored in this repository.

## Usage

Run on a host machine with Python 3:

    python3 tools/naomi/pack_vf3_roms.py vf3-naomi.bin vf3.zip vf3-naomi-netdimm.bin

The executable must fit before the default payload base at 0x01000000 (16 MiB). Use --payload-offset to reserve a different 32-byte-aligned base if needed. The tool writes a JSON sidecar containing absolute cartridge offsets, region CRC32/SHA-256 values, source ZIP SHA-256, and image size.

The input ZIP must contain the complete VF3 Revision D set. The packer checks all 39 ROMs by CRC32 and exact byte length before producing the output. ZIP paths may contain subdirectories; ROMs are matched by basename and CRC, with CRC-only fallback matching.

## Image layout

At the configured payload base is a 4096-byte little-endian VF3NROM v1 header. Its three descriptors contain region ID, absolute cartridge-image offset, byte length and CRC32. The header CRC32 is calculated with its own field zeroed.

The packed regions follow in this order:

| Region | Size | Representation |
|---|---:|---|
| Fixed CROM | 8 MiB | Host-order 32-bit words |
| Banked CROM | 64 MiB | Host-order 32-bit words |
| VROM | 32 MiB | Original big-endian byte stream |

Fixed CROM is assembled exactly as the pinned upstream loader does: the first 6 MiB of banked CROM followed by the 2 MiB program CROM. CROM and banked CROM receive the same per-ROM interleave, pair swap and final 32-bit word-order conversion as upstream. VROM retains its byte order and interleaving.

The sound program and sample ROMs are still validated against the profile but are not included in the image: the current Model 3 PPC board core sends MIDI bytes and does not consume those ROM regions.

## Runtime integration status

This establishes a deterministic ROM image format and host-side packer. The Naomi runtime does not yet parse this header or serve these regions to the Model 3 board core. The next integration work is a small header reader, fixed-CROM access strategy, banked-CROM window/cache, and VROM streaming into the texture FIFO. The packed image is therefore a build artifact format, not yet a playable VF3 image.
