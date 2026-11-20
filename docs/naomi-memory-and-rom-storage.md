# Naomi memory and ROM-storage investigation

Pinned upstream source: `mr-finster/vf3-recomp@8cb7cc5f5919c9cc33076b5c7bad3ab515654907`

This note records the constraints found while evaluating the Model 3 board core for Naomi. It is an investigation, not a claim that ROM streaming is implemented.

## 1. Upstream ROM allocations

`src/machine/romset.cpp` expands the ZIP into complete host-side vectors:

| Region | Final allocation | Notes |
|---|---:|---|
| Fixed CROM | 8 MiB | Program and fixed code/data |
| Banked CROM | 128 MiB | Two 64 MiB copies of the banked region |
| VROM | 64 MiB | Two 32 MiB copies |
| Sound ROM | 512 KiB | 68000 program |
| Samples | 16 MiB | Two copies of an 8 MiB region |
| **Total** | **216.5 MiB** | Excludes the source ZIP and temporary decompression buffers |

The ZIP reader itself reads the entire archive into a vector (the source explicitly allows up to 512 MiB). During loading, the expanded region vectors coexist with the final vectors until the copies/moves complete. This loader cannot be reused as-is on Naomi.

## 2. Board working-memory allocations

In `Model3::load()`, before any renderer or host application allocations:

| Working region | Allocation |
|---|---:|
| PPC RAM | 8 MiB |
| Backup RAM | 128 KiB |
| Security RAM | 128 KiB |
| Tile RAM | 1.125 MiB |
| Low culling RAM plus guard | 4.0625 MiB |
| High culling RAM plus guard | 1.0625 MiB |
| Polygon RAM | 4 MiB |
| Texture FIFO capacity | up to 1 MiB |
| Texture RAM | 8 MiB |
| **Total reserved/allocated** | **about 27.5 MiB** |

This is already close to Naomi's 32 MiB main-RAM budget. It excludes stack, executable, allocator overhead, renderer state, audio, and any host-side framebuffers. Reducing only the ROM vectors is therefore insufficient: the board's mutable working set also needs a Naomi-specific plan.

## 3. ROM access patterns in the board core

### CROM

The PPC bus handlers `read8/read16/read32` access fixed CROM and the currently selected 8 MiB bank through direct pointers. `set_crom_bank()` points into one of sixteen 8 MiB windows in the 128 MiB banked vector.

A storage abstraction must preserve fast byte/halfword/word reads and bank switching. A sector read on every PPC memory access is not viable. A small page cache (or a mapped window) is the likely shape, but cache size and replacement policy need workload measurements.

### VROM

VROM is not read through the ordinary PPC bus handlers. `r3d_texture_port()` treats the register value as a VROM word address, reads a length header, then copies sequential words into the texture FIFO. This is a good candidate for bounded sequential reads/pages rather than a permanently resident 64 MiB vector.

The current code uses a 24-bit word address and wraps it with `0xFFFFFF`; the backing VROM image is duplicated to implement that address space. A Naomi implementation should preserve this wrap behaviour without duplicating the image.

### Sound ROM and samples

The current board loader stores these as vectors, but the current Model 3 board core does not consume them in its PPC read handlers. The sound board is explicitly not yet emulated (the board sends MIDI bytes to it). These regions should not be allocated until a Naomi sound-board implementation actually needs them.

## 4. NetDIMM access is not yet established

The Dreamcast-oriented KallistiOS `dc/cdrom.h` interface is not a safe assumption for Naomi NetDIMM media. Its own documentation says the high-density GD-ROM area is deliberately not accessible through that CD-ROM API. Naomi NetDIMM is a different target and needs its actual access path verified before selecting a storage backend.

Do not assume that:
- the complete NetDIMM image is directly addressable as ordinary SH-4 RAM;
- the Dreamcast `cdrom_read_sectors()` API can read Naomi NetDIMM content;
- a 128 MiB CROM vector can be replaced by an ordinary filesystem file without proving the media interface and latency.

The existing Daytona Naomi work establishes useful Naomi boot, BIOS-helper, and build lessons, but does not establish a general NetDIMM file-reading API.

## 5. Next implementation sequence

1. Establish the supported Naomi-side way to read arbitrary offsets from the loaded NetDIMM game image (or prove that no such API exists and identify the actual hardware path).
2. Build a tiny on-screen Naomi probe that reads known offsets, verifies bytes/checksums, and measures repeated/random/sequential read latency. No serial output is assumed.
3. Define a read-only ROM source interface with explicit bounded buffers; keep the desktop vector-backed implementation unchanged.
4. Implement banked CROM page/window caching and VROM sequential page reads against that interface.
5. Measure the mutable working set separately. Investigate reuse/aliasing of Naomi PVR2 VRAM only where hardware semantics permit it; do not move emulated Model 3 RAM into PVR memory by assumption.
6. Only then attempt a full Model 3 board link and frame test.

No ROMs or commercial game assets belong in this repository.


## 6. First physical Naomi PIO result (v55)

The read-only v55 probe ran on real Naomi hardware. Reads at offsets 0x00000000, 0x00000100, and 0x00010000 returned stable, repeatable words. Reads at 0x00100000 returned 0xFFFF for all four words, also repeatably. The FPU probe and PPC interpreter smoke test passed on the same v55 test session.

The v55 PIO screen labelled each read as "READ OK", but that label was unconditional: the PIO interface does not return a read-status bit. The only measured property was repeatability. The 0xFFFF result at 1 MiB is consistent with reading beyond the end of the small probe ROM and does not establish a NetDIMM address-space limit.

### v56 follow-up: known markers in a 4 MiB image

The next probe build keeps the executable segment and Naomi header intact, then extends the ROM image to exactly 4 MiB with three 8-byte markers outside the executable segment:

| File offset | Expected bytes |
|---|---|
| 0x00100000 | `VF3PIO1!` |
| 0x00200000 | `VF3PIO2!` |
| 0x00300000 | `VF3PIO3!` |
| 0x00400000 | End of image; expected PIO value is 0xFFFF |

The on-screen probe compares the returned words against these known markers, checks repeated-read stability, and labels the end-of-image read separately. A successful marker read demonstrates that PIO can access appended NetDIMM image data at that offset; it is stronger evidence than repeatability alone. This still does not establish the full 64/128 MiB ranges needed by VROM and banked CROM, which require a later large-image test.
