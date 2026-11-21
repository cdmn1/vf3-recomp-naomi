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


## Fixed and banked CROM window

`platform/naomi/cart_rom.c` now adds a caller-buffer-backed CROM layer above the image reader:

- Startup reads the complete 8 MiB fixed-CROM region into a caller-owned, 32-byte-aligned buffer. This is the resident region that upstream patches and the PPC fetch path expect.
- Bank selection reads one 8 MiB window from the 64 MiB logical banked-CROM region into a second caller-owned aligned buffer.
- Logical bank numbers 8–15 mirror 0–7, matching upstream's duplicated 128 MiB vector.
- Selecting the already-resident bank avoids a redundant DMA. A failed refill invalidates the window so callers cannot mistake stale bytes for the requested bank.
- The API allocates no memory and does not yet modify upstream Model 3 bus handlers. The caller must ensure the bank window is not observed while a synchronous refill is in progress.

The host mock test writes a distinct signature at the start of each of the eight logical banks, exercises all banks, and confirms bank 15 mirrors bank 7. Naomi CI compiles and links the new object into the cartridge probe.

This is a storage primitive, not yet proof that an 8 MiB resident bank window fits alongside the complete Model 3 working set in Naomi's 32 MiB RAM. The memory-budget investigation remains a separate gate before connecting this to the board core.
