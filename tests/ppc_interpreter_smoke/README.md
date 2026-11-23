# PPC interpreter SH-4 smoke test

This test compiles the actual upstream `src/ppc/ppc.cpp` and `ppc.h` from the pinned `mr-finster/vf3-recomp` checkout.

At runtime it attaches the interpreter to an 8 MiB RAM buffer and a dummy Model 3 bus, then executes three known PowerPC instructions:

1. `addi r3,0,7`
2. `addi r4,0,5`
3. `add r5,r3,r4`

It checks that `r5 == 12`, that exactly three instructions were counted, and that the interpreter did not enter its fatal state.

No game ROM, game executable or commercial asset is required.

A successful Actions build proves the upstream interpreter compiles and links for the Naomi SH-4 target. A successful run in Flycast or on real hardware is a separate validation step. It does not establish that a full VF3 frame can be emulated in real time.


## NetDIMM VROM and SceneBuilder integration ROM

The separate `naomi-model3-vrom-integration` artifact is a bootable Naomi test
image with a synthetic VF3NROM payload at the normal 16 MiB offset. It contains
zero-filled fixed CROM, banked CROM and VROM regions plus one synthetic
three-vertex polygon model; it contains no commercial game data.

At startup the ROM mounts the image through the cartridge G1 DMA reader and
checks all three region CRCs. It then calls the real patched `Model3::load()`
path, which initializes the board's RAM, culling RAM, polygon RAM, texture
FIFO, page tracking and PVR-VRAM-backed 8 MiB texture RAM. It checks the
resident fixed CROM and banked-CROM cache, streams VROM words, and passes the
synthetic model through the patched upstream `SceneBuilder::model_at()` and
polygon parser. It repeats the model request to check the parsed-ROM cache.
Results are shown on screen; no serial connection is used.

After the board-load checks, the probe calls the real `Model3::power_on()` path
and executes synthetic PPC programs through the CPU attached to the board.
The programs check arithmetic, load fixed CROM, write the active-low CROM bank
register, load banked CROM through the board bus, and round-trip a word through
the mapped Real3D tile RAM window. Distinct marker words in the synthetic
fixed CROM and selected bank 3 make the CROM bus checks observable.
These are not the commercial VF3 boot program or game assets.

This validates board storage initialization, PPC execution, mapped fixed/banked
CROM reads through the NetDIMM bridge, a mapped Real3D tile RAM read/write, and
NetDIMM/SceneBuilder integration. It
does not execute the VF3 PowerPC program or validate the Naomi PowerVR2 game
renderer.
