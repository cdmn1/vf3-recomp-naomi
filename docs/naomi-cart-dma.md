# Naomi cartridge / NetDIMM DMA reader

This branch has a synchronous, allocation-free G1 DMA transport primitive in `platform/naomi/cart_dma.c`, plus a VF3NROM image reader in `platform/naomi/cart_image.c`.

## Transport contract

- The caller supplies the destination; the transport allocates no memory.
- Source offset, destination address and length must be 32-byte aligned.
- Requests larger than 4 MiB are split into independently completed G1 transactions.
- Timeout applies to each transaction.
- Destination cache lines are invalidated before and after DMA.
- Reads are synchronous. The caller can reuse the buffer after return.
- Unaligned byte ranges should be handled by reading an aligned containing block and slicing it in software.
- Invalid arguments, an already-active G1 engine, timeout and short transfer are explicit errors.

## Flycast evidence

The v74 Flycast probe demonstrated repeatable PIO reads at offsets from 0 through 8 MiB, expected markers at each 1 MiB point from 1 through 8 MiB, and repeatable 0xFFFF reads at the 64 MiB test-image EOF boundary.

The same v74 run verified byte-for-byte G1 DMA transfers from source offset 36 MiB at sizes 32 B, 256 B, 4 KiB, 64 KiB, 256 KiB, 1 MiB, 2 MiB and 4 MiB. The 4 MiB transfer completed in about 212 ms, reported the exact 4,194,304-byte count, and had no word mismatch. The displayed first 16 words matched the expected values.

These are authoritative results for the Flycast environment used in the project. They do not establish physical hardware throughput or the maximum cartridge address. The 4 MiB software chunk size remains conservative, not a claimed hardware limit.

## VF3NROM reader

The image-format parser checks the VF3NROM v1 magic, version, fixed VF3 Revision D profile ID, header CRC, exact region sizes, 32-byte alignment, 31-bit cartridge address bounds and region overlap. The runtime reader mounts the 4 KiB header through G1 DMA, exposes bounded synchronous reads for fixed CROM, banked CROM and VROM, and verifies a region CRC using a caller-owned 64 KiB scratch buffer.

Host-side tests cover valid and malformed headers, a mock cartridge DMA path, bounded reads, successful region CRC verification and detection of a deliberately incorrect region checksum. The Naomi cartridge probe also compiles and links the reader.

## Remaining integration

This is not yet connected to the Model 3 board core. It does not yet:
- load and patch the 8 MiB fixed CROM at startup;
- refill an 8 MiB banked-CROM window when the PPC bank register changes;
- stream VROM words into the Real3D texture FIFO;
- recover a cartridge DMA engine after a timeout;
- arbitrate cartridge reads between concurrent clients.

The next porting step is to integrate the reader with the upstream Model 3 memory paths while solving the Naomi 32 MiB main-RAM budget. No physical-hardware success is claimed for the new shared reader.

## Fixed CROM and bounded bank-page cache

`platform/naomi/cart_rom.c` keeps the patched 8 MiB fixed-CROM region resident, but avoids an 8 MiB bank allocation:

- Banked CROM uses four caller-owned 64 KiB pages (256 KiB total).
- Selecting logical bank 8–15 mirrors banks 0–7, matching upstream's duplicated 128 MiB vector.
- The page lookup fills a missing page synchronously through the image reader and uses an LRU replacement policy.
- Repeated reads within resident pages avoid further DMA. Changing banks invalidates the cache.
- A returned page pointer is valid only until its cache slot is replaced or the bank changes. The Model 3 bus-handler adapter must not retain it across calls.

The host test writes distinct signatures for each bank, checks all eight logical banks, confirms bank-15 mirroring, exercises multiple page reads and rejects an out-of-range page. Naomi CI compiles and links this layer into the cartridge probe.

## Bounded VROM texture-port streamer

The pinned upstream `Model3::r3d_texture_port()` reads a 24-bit word address, obtains its length from the first word using `(2 + v[addr] / 2) / 4`, then appends sequential words until the 1 MiB texture FIFO is full. Its VROM vector is 64 MiB: two identical 32 MiB copies, with the 24-bit word address wrapping at 0xFFFFFF.

`platform/naomi/cart_vrom.c` implements that operation against the packed single-copy 32 MiB VROM region. It uses a caller-owned 64 KiB scratch buffer, aligned synchronous DMA reads, preserves word order, collapses the duplicated upper half onto the same 32 MiB data, and respects the remaining FIFO capacity. The sink callback receives words synchronously; it must append or consume each word before returning and must not retain the scratch pointer.

Host tests cover the 24-bit wrap from word 0xFFFFFF to word zero, exact word sequence, the upstream length calculation, and FIFO-full truncation. The pinned upstream Model 3 texture-port handler is patched in the Naomi build to call a C++ bridge, which streams into the existing 1 MiB texture FIFO. The image binding must still be supplied by the Naomi startup/image-mount path; that loader is not yet implemented. This has not been measured on physical hardware.

## VF3NROM mount and bridge binding

`platform/naomi/vf3_image.c` adds an allocation-free mount sequence for the packed VF3 Revision D image:

1. Read and parse the 4 KiB VF3NROM v1 header at the packer's fixed 16 MiB image offset.
2. Load the 8 MiB fixed CROM into a caller-owned, 32-byte-aligned buffer.
3. Check the fixed CROM CRC from that resident buffer.
4. Stream-verify the full 64 MiB banked-CROM and 32 MiB VROM regions using a caller-owned 64 KiB scratch buffer.
5. Bind the persistent image layout to the Model 3 VROM bridge and the initialized CROM page cache to the Model 3 banked-CROM bridge.

The mount object retains the parsed layout and cache metadata and must remain alive for the entire board session. Fixed CROM, the 256 KiB bank-page cache and the 64 KiB verification scratch area are caller-owned; the mount code allocates no memory. The Model 3 patch now obtains the fixed-CROM pointer from the bridge for PPC instruction fetch, fixed-CROM bus reads, program-image copying, and the existing free-play/Dural patch routines, so those writes remain in the same resident buffer rather than requiring a second 8 MiB copy. The host integration test constructs a synthetic full-size image, verifies all region CRCs, mounts it, and exercises the actual CROM/VROM bridge calls.

This is now a usable NetDIMM image-mount component, but it is not yet invoked by a Naomi game startup entrypoint. The normal upstream `Model3::load()` path still creates the desktop ZIP-backed resident ROM vectors; replacing that load path and reconciling the remaining board/renderer memory allocations is the next integration stage.
