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

## Naomi runtime reader

`platform/naomi/cart_image_format.c` parses and validates the VF3NROM v1 header without allocation. It checks the magic, version, fixed profile ID, header CRC, exact region sizes, 32-byte alignment, address-space bounds and region overlap. `platform/naomi/cart_image.c` mounts the header through the shared G1 DMA reader, provides bounded region reads, and can verify each complete region against its descriptor CRC using a caller-owned 64 KiB scratch buffer.

The API deliberately requires aligned source offsets, lengths and destinations. That matches the packed image layout and allows the DMA reader to transfer directly into caller-owned RAM without an implicit large allocation. Small or unaligned PPC reads still need to be served by a resident CROM window or an explicit aligned staging block.

## Runtime integration status

The header parser and region reader are implemented and compile-tested as part of the Naomi cartridge probe. The parser has host-side tests for valid headers, corrupted header CRCs, overlapping regions and incorrect region sizes. The reader has not yet been exercised against a complete synthetic VF3NROM image in Flycast or on Naomi. The Model 3 board core does not yet use the reader: fixed-CROM loading/patching, synchronous 8 MiB bank-window refills, and VROM FIFO streaming remain to be integrated. The packed image is therefore not yet a playable VF3 image.
