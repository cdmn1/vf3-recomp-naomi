# PPC interpreter SH-4 smoke test

This test compiles the actual upstream `src/ppc/ppc.cpp` and `ppc.h` from the pinned `mr-finster/vf3-recomp` checkout.

At runtime it attaches the interpreter to an 8 MiB RAM buffer and a dummy Model 3 bus, then executes three known PowerPC instructions:

1. `addi r3,0,7`
2. `addi r4,0,5`
3. `add r5,r3,r4`

It checks that `r5 == 12`, that exactly three instructions were counted, and that the interpreter did not enter its fatal state.

No game ROM, game executable or commercial asset is required.

A successful Actions build proves the upstream interpreter compiles and links for the Naomi SH-4 target. A successful run in Flycast or on real hardware is a separate validation step. It does not establish that a full VF3 frame can be emulated in real time.
