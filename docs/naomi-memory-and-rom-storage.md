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
| Low culling RAM plus guard | 4.25 MiB |
| High culling RAM plus guard | 1.25 MiB |
| Polygon RAM | 4 MiB |
| Texture FIFO capacity | up to 1 MiB |
| Texture RAM | 8 MiB |
| **Total reserved/allocated** | **about 27.875 MiB** |

This is already close to Naomi's 32 MiB main-RAM budget. The guard sizes are 0x10000 *words* each (256 KiB per culling RAM), not 0x10000 bytes; the corrected total is 27.875 MiB. It excludes stack, executable, allocator overhead, renderer state, audio, and any host-side framebuffers. Reducing only the ROM vectors is therefore insufficient: the board's mutable working set also needs a Naomi-specific plan.

## 3. ROM access patterns in the board core

### CROM

The PPC bus handlers `read8/read16/read32` access fixed CROM and the currently selected 8 MiB bank through direct pointers. `set_crom_bank()` points into one of sixteen 8 MiB windows in the 128 MiB banked vector.

A storage abstraction must preserve fast byte/halfword/word reads and bank switching. A sector read on every PPC memory access is not viable. A small page cache (or a mapped window) is the likely shape, but cache size and replacement policy need workload measurements.

### VROM

VROM is not read through the ordinary PPC bus handlers. `r3d_texture_port()` treats the register value as a VROM word address, reads a length header, then copies sequential words into the texture FIFO. This is a good candidate for bounded sequential reads/pages rather than a permanently resident 64 MiB vector.

The current code uses a 24-bit word address and wraps it with `0xFFFFFF`; the backing VROM image is duplicated to implement that address space. A Naomi implementation should preserve this wrap behaviour without duplicating the image.

### Sound ROM and samples

The current board loader stores these as vectors, but the current Model 3 board core does not consume them in its PPC read handlers. The sound board is explicitly not yet emulated (the board sends MIDI bytes to it). These regions should not be allocated until a Naomi sound-board implementation actually needs them.

## 4. Naomi cartridge PIO access: partially established

The Dreamcast-oriented KallistiOS `dc/cdrom.h` interface is not a safe assumption for Naomi NetDIMM media. Its own documentation says the high-density GD-ROM area is deliberately not accessible through that CD-ROM API. Naomi NetDIMM is a different target. A read-only physical probe now confirms that the Naomi cartridge PIO register path can retrieve repeatable image data at offsets through 3 MiB (v59). The v60 64 MiB probe has compiled and is awaiting the corresponding physical result. Arbitrary high-offset access is therefore partially established, but full address range, sustained throughput, and latency are still unmeasured.

Do not assume that:
- the complete NetDIMM image is directly addressable as ordinary SH-4 RAM;
- the Dreamcast `cdrom_read_sectors()` API can read Naomi NetDIMM content;
- a 128 MiB CROM vector can be replaced by an ordinary filesystem file without proving the media interface and latency.

The existing Daytona Naomi work establishes useful Naomi boot, BIOS-helper, and build lessons, but does not establish a general NetDIMM file-reading API.

## 5. Next implementation sequence

1. Complete physical PIO range validation with v60 (64 MiB); only then extend the test toward the full 64 MiB banked-CROM and 32 MiB VROM source regions.
2. Measure sequential and random PIO throughput on hardware, using checksummed blocks and on-screen results.
3. Define a read-only ROM source interface with explicit bounded buffers; keep the desktop vector-backed implementation unchanged.
4. Implement an 8 MiB banked-CROM window backed by PIO reads. Keep the 8 MiB fixed CROM in main RAM because it is patched and is used directly by the PPC boot path.
5. Implement VROM as bounded sequential PIO reads in `r3d_texture_port()`, preserving the 24-bit word-address wrap and texture FIFO limit.
6. Measure and reduce the mutable working set separately. Investigate PVR2 VRAM only where its real hardware semantics permit it.
7. Only then attempt a full Model 3 board link and frame test.

No ROMs or commercial game assets belong in this repository.


## 6. First physical Naomi PIO result (v55)

The read-only v55 probe ran on real Naomi hardware. Reads at offsets 0x00000000, 0x00000100, and 0x00010000 returned stable, repeatable words. Reads at 0x00100000 returned 0xFFFF for all four words, also repeatably. The FPU probe and PPC interpreter smoke test passed on the same v55 test session.

The v55 PIO screen labelled each read as "READ OK", but that label was unconditional: the PIO interface does not return a read-status bit. The only measured property was repeatability. The 0xFFFF result at 1 MiB is consistent with reading beyond the end of the small probe ROM and does not establish a NetDIMM address-space limit.

### v59 follow-up: known markers in a 4 MiB image (CI build passed)

The next probe build keeps the executable segment and Naomi header intact, then extends the ROM image to exactly 4 MiB with three 8-byte markers outside the executable segment:

| File offset | Expected bytes |
|---|---|
| 0x00100000 | `VF3PIO1!` |
| 0x00200000 | `VF3PIO2!` |
| 0x00300000 | `VF3PIO3!` |
| 0x00400000 | End of image; expected PIO value is 0xFFFF |

The on-screen probe compares the returned words against these known markers, checks repeated-read stability, and labels the end-of-image read separately. A successful marker read demonstrates that PIO can access appended NetDIMM image data at that offset; it is stronger evidence than repeatability alone. This still does not establish the full 64/128 MiB ranges needed by VROM and banked CROM, which require a later large-image test.


## 7. Real Naomi result: v59 marker test passed

The v59 probe was run on physical Naomi hardware. It returned the expected marker words at 1, 2, and 3 MiB, all repeated reads matched, and the 4 MiB end-of-image read returned 0xFFFF. The PPC interpreter smoke test and SH-4 FPU probe also passed in the same test session.

This proves that the PIO path can read appended NetDIMM image data beyond the executable segment at those tested offsets. It does not yet establish the maximum address, sustained throughput, or full-size random/sequential access performance.

## 8. v60: 64 MiB PIO address-range probe (CI build passed)

The v60 probe image was built and packaged successfully by CI. It is padded to exactly 64 MiB. It retains the executable segment and places unique 8-byte markers at 1, 2, 3, 4, 8, 16, 32, and 63 MiB. It checks repeated reads at each marker and verifies 0xFFFF at the 64 MiB end boundary.

This tests a much wider portion of the PIO address range while remaining below the common 256 MiB and 512 MiB Naomi NetDIMM memory configurations. Capacity variants are documented by NetDIMM references; the actual installed capacity must still be respected. The PIO high-offset register uses a 15-bit offset field in the Flycast hardware model, which is consistent with a 31-bit byte offset, but physical range and timing remain subject to hardware validation.

The 64 MiB probe is still a sparse logical image with only a few marker bytes populated. It tests addressability, not bulk transfer speed or the cost of loading a full 64 MiB image.


## 9. Source-level ROM backing design (pinned upstream)

Inspection of the pinned upstream implementation narrows the first Naomi storage implementation substantially:

- `romset::load()` creates complete temporary ROM regions, then copies them into final vectors. The temporary region sizes are 2 MiB CROM, 64 MiB banked CROM, 32 MiB VROM, 512 KiB sound, and 8 MiB samples. The banked CROM, VROM, and samples are then duplicated in their final vectors. The ZIP is also read completely into memory. This loading path should not be ported to Naomi.
- The final 8 MiB fixed CROM consists of the 2 MiB fixed program ROM region at its upper end and the first 6 MiB of banked CROM at its lower end. It is modified by the existing free-play and Dural patch routines, so keeping this 8 MiB region resident is the lowest-risk first step.
- The 128 MiB banked-CROM vector is two copies of a 64 MiB logical region. `set_crom_bank()` selects one of sixteen 8 MiB windows; windows 0–7 address the first 64 MiB and windows 8–15 mirror it. A Naomi implementation therefore needs only one resident 8 MiB bank window, plus fixed CROM, if bank changes synchronously refill that window before PPC execution continues. The bank window must remain stable for the duration of each PPC memory access and must preserve the current host-word byte order.
- VROM's 64 MiB vector is two copies of a 32 MiB logical region. `r3d_texture_port()` is its only direct consumer in the inspected board core: it reads a length word and then sequential words into a FIFO capped at 1 MiB. A bounded scratch buffer can feed the FIFO from PIO without keeping all VROM resident. The 24-bit word-address wrap must be retained, and each request may cross the end of the 32 MiB logical image.
- Sound ROM and samples can remain unallocated for the current board-core scope because no current PPC read handler consumes them; audio is not yet implemented.

### Important remaining loader decision

The current desktop path accepts a ZIP and performs ROM-set identification, CRC checks, interleaving, byte swaps, mirroring, and profile-specific placement. A raw appended ZIP could be parsed through PIO, but it would still require decompression buffers and full region construction unless the loader is redesigned. The cleaner Naomi route is likely an offline packer that accepts the user's existing ROM ZIP and emits a compact, deterministic raw-region payload (with a small manifest containing profile, region offsets/sizes, and checksums). The Naomi executable can then read fixed CROM, selected bank windows, and VROM ranges directly from that payload. This avoids shipping a general ZIP/deflate implementation in the Naomi runtime and avoids duplicating mirrored regions.

The payload must preserve the exact byte/word ordering expected by the PPC bus handlers and Real3D texture port. The packer should reuse or faithfully port the upstream ROM placement metadata and validate every source ROM CRC before producing output. No game ROMs or generated commercial payloads are to be committed to this repository.

### RAM budget implication

With an 8 MiB fixed CROM and one 8 MiB bank window resident, ROM working memory would be approximately 16 MiB, before VROM scratch buffers. That is a substantial reduction from the current 216.5 MiB final ROM vectors, but it leaves only about 16 MiB of Naomi main RAM for the 27.5 MiB current mutable board allocations, executable, stack, renderer, and allocator. The mutable working set therefore remains a separate blocker; the ROM cache design alone does not make the full port fit in 32 MiB.


## 10. Mutable-memory accounting refinement

A second pass over the exact allocation expressions corrected the earlier culling guard arithmetic. `CULL_PAD` is `0x10000` 32-bit words, i.e. 256 KiB per culling RAM, not 64 KiB. The two culling allocations are therefore 4.25 MiB and 1.25 MiB. The combined baseline is 27.875 MiB, before the executable, stack, renderer, framebuffers, allocator metadata, and any extra texture copies. This makes the 32 MiB fit problem even tighter than the first estimate.

The save-state page-generation arrays are comparatively tiny: one 32-bit generation entry per 4 KiB page across tile, culling, polygon, and texture RAM (roughly tens of KiB, not MiB). Removing save-state dirty tracking would not solve the memory problem.

### What the source tells us about reduction candidates

- **Texture RAM (8 MiB):** this is a full 2048×2048 16-bit emulated texture store. It is the largest single candidate for aliasing or relocation, but only if the Naomi renderer can use the same physical storage without clobbering the PVR's own render targets, tile lists, and texture allocations. It cannot be assumed to be free PVR memory. A renderer-specific texture residency/cache design may be safer than a direct alias.
- **Polygon RAM (4 MiB):** the PPC-visible Real3D address space writes directly into this array, and the renderer consumes its contents at flush. It may be possible to allocate only the portions actually touched, but the current indexed vector and address masking assume a contiguous zero-filled 4 MiB region. A sparse-page backing would need to preserve zero reads, write ordering, and pointers passed into scene-building code.
- **Low/high culling RAM (5.5 MiB including guards):** these are also PPC-visible and are read by the scene builder. The guard words are intentional: upstream notes that game-generated node, matrix, LOD, and pointer-list reads may start in-range and extend beyond the nominal end. Simply deleting the guards risks out-of-bounds reads. Sparse backing or guard-page logic would need to preserve zero-filled overrun behaviour.
- **Tile RAM (1.125 MiB):** likely less attractive to optimize first because it is comparatively small and the renderer reads it as a contiguous device memory.
- **PPC RAM (8 MiB):** the recompiled PPC code attaches directly to `ram.data()`; its direct accesses and invalidation tracking make paging or sparse backing a major CPU-core change. It is not a quick win.
- **Texture FIFO (up to 1 MiB reserved):** this is transient staging memory, but it can contain a sequence of commands/data that `r3d_flush_textures()` later consumes. A bounded streaming/chunked consumer might reduce the peak, but only after confirming that texture upload and mipmap parsing never need to look backwards or retain the complete FIFO.

### Practical order for the next memory experiments

1. Inspect Naomi renderer allocations and PVR2 VRAM ownership before choosing any VRAM aliasing strategy.
2. Measure actual peak use with a minimal Naomi runtime and renderer, not just the emulated board arrays.
3. Prototype lazy/zero-backed pages for polygon RAM and culling RAM in a host-side test first; retain the existing guards and exact PPC-visible semantics.
4. Evaluate chunked texture FIFO processing only if its parser can be made streaming without changing texture upload ordering.
5. Treat PPC RAM and fixed device arrays as late-stage work, not the first memory optimization targets.


## 11. Upstream software-renderer memory is another major exclusion

The pinned upstream CPU rasterizer is not suitable to copy unchanged into a Naomi 32 MiB main-memory build. At the game's native 496×384 output (190,464 pixels), `video::Raster::alloc()` creates:

| Buffer | Bytes per pixel | Approx. size |
|---|---:|---:|
| `base`, `t1`, `t2` (each `Px` is four floats) | 48 total | 8.72 MiB |
| `depth`, `depth_saved` | 8 total | 1.45 MiB |
| `stencil`, `stencil_saved` | 2 total | 0.36 MiB |
| **Raster buffers** | **58 total** | **10.53 MiB** |

This excludes the transformed triangle lists, scene/model vectors, tile-generator outputs, texture copies, and display buffers. The tile generator alone keeps two 496×384 RGBA layers (about 1.46 MiB together), a 1008×384 wide layer (about 1.48 MiB), and optional plate/wide/tall intermediates. The desktop host also keeps multiple copies of frame layers and uploads the Model 3 texture RAM into renderer-owned texture resources.

Consequences:

- A Naomi port needs a purpose-built PVR2 renderer, not the upstream CPU rasterizer.
- Do not allocate the desktop renderer's multiple CPU frame layers on Naomi.
- The 8 MiB Model 3 texture RAM is conceptually separate from the PVR2's own VRAM allocations. A hardware renderer may upload or cache texture regions in PVR2 VRAM, but the Model 3 source remains authoritative and must preserve its write/update semantics.
- Naomi PVR2 VRAM should be treated as a separate, finite resource with explicit allocations for framebuffers, tile lists, textures, and command buffers. It is not additional SH-4 heap and cannot be double-counted as free capacity.
- The first renderer milestone should be a low-memory, native-resolution PVR2 path with no CPU raster fallback and no retained desktop frame-history buffers. Widescreen, interpolation history, panoramas, and optional visual enhancements should be left out initially.



## 12. Naomi cartridge-to-RAM DMA: the Holly G1 path is modelled

A further source trace resolved the earlier ambiguity. Flycast's Naomi DMA scheduler explicitly connects the Naomi cartridge source pointer to Holly's GD-ROM DMA transfer engine:

1. The cartridge DMA source offset is programmed through `0x005F700C` (high) and `0x005F7010` (low). Flycast stores this as `NaomiCartridge::DmaOffset`; `0x005F7014` is the modeled cartridge DMA count register.
2. The Holly G1 DMA destination/length/control registers are `SB_GDSTAR=0x005F7404`, `SB_GDLEN=0x005F7408`, `SB_GDDIR=0x005F740C`, `SB_GDEN=0x005F7414`, and `SB_GDST=0x005F7418`.
3. Flycast's `Naomi_DmaStart()` checks enable and direction, initializes the destination and transfer counters, then schedules `naomiDmaSched()`.
4. In `naomiDmaSched()`, Flycast calls `CurrentCartridge->GetDmaPtr(block_len)`, copies that source block to the programmed SH-4 destination with `WriteMemBlock_nommu_ptr()`, advances the cartridge source pointer, and repeats until the requested transfer length is complete. It then clears the busy state and raises the GD-ROM DMA interrupt.

This is stronger than merely observing two unrelated register groups: the emulator explicitly models the Naomi cartridge as the source of Holly G1 DMA transfers. It makes the G1 path a credible physical test candidate for NetDIMM reads.

Flycast comments that its modeled transfer is 20 MB/s, while citing a 14.4 MB/s GD-ROM-to-system-RAM figure from a Sega SH-4 access document. These are emulator timing assumptions/reference figures, not physical measurements of NetDIMM DMA throughput.

### What remains unproven on physical Naomi

The source trace is emulator evidence, not hardware validation. In particular, it does not establish whether a physical NetDIMM exposes the same DMA source behavior, the exact source-offset flag settings, maximum transfer size, alignment restrictions, or completion/error behavior.

The separate community reverse-engineering notes on Naomi protected ROM boards describe the `0x5F700C`/`0x5F7010` pair as the DMA-mode source address registers and document additional control bits in the high register. This reinforces that these are ROM-board DMA-mode registers, but the notes focus on protected ROM boards and must not be copied blindly as a universal NetDIMM programming recipe.

### Safe physical test plan

A dedicated read-only probe is justified, but it must be conservative:

1. First retain PIO as the known-good fallback and finish the pending v60 64 MiB PIO test.
2. Use a known marker in the probe image (not executable bytes) as the DMA source.
3. Reserve a small, 32-byte-aligned SH-4 main-RAM destination buffer and convert its KOS pointer to the correct root-bus address using verified KOS/SH-4 address rules.
4. Program only the documented DMA source/destination/length/direction/enable/start sequence. Use a short transfer (for example, 32 or 64 bytes) and a finite timeout; never wait forever for completion.
5. Display the destination words, marker comparison, final DMA status/counters, and timeout/error state on screen.
6. Only after the small transfer succeeds should the probe measure larger blocks or sequential throughput.

Do not infer the meaning of the cartridge high-register flags from Flycast alone. Do not run an unbounded DMA or direct it at executable, stack, PVR, or texture memory. A physical pass is required before the runtime ROM cache can use this path.
