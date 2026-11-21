# Naomi cartridge / NetDIMM DMA reader

This branch has a synchronous, allocation-free G1 DMA transport primitive in platform/naomi/cart_dma.c.

## Contract

- The caller supplies the destination; the transport allocates no memory.
- Source offset, destination address and length must be 32-byte aligned.
- Requests larger than 4 MiB are split into independently completed G1 transactions.
- Timeout applies to each transaction.
- Destination cache lines are invalidated before and after DMA.
- Reads are synchronous. The caller can reuse the buffer after return.
- Unaligned byte ranges should be handled by reading an aligned containing block and slicing it in software.
- Invalid arguments, an already-active G1 engine, timeout and short transfer are explicit errors.

## Evidence and limits

The v74 Flycast probe demonstrated byte-for-byte correctness through a single 4 MiB transaction. This does not establish a physical-hardware maximum or physical throughput. Register behavior and the observed transfer-count register are based on the probe and Flycast model. The 4 MiB ceiling is a conservative software chunk size, not a claimed hardware limit.

## Not yet implemented

This is only the transport primitive. It does not parse ZIP/ROM images, cache CROM banks, stream VROM into the texture FIFO, retry failed reads, or arbitrate concurrent cartridge access. VF3 integration still needs a stable aligned page/window buffer and serialized bus access.
