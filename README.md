# Virtua Fighter 3 Recomp → Sega Naomi

An experimental porting track for the **Model 3 arcade Virtua Fighter 3 recompilation** to Sega Naomi.

This is intentionally separate from [vf3-naomi](https://github.com/cdmn1/vf3-naomi), which investigates the Dreamcast VF3tb executable. It also remains separate from [daytona-arcade-recomp-naomi](https://github.com/cdmn1/daytona-arcade-recomp-naomi).

## Starting point

- Upstream source: [mr-finster/vf3-recomp](https://github.com/mr-finster/vf3-recomp)
- Initial pinned source revision: `8cb7cc5f5919c9cc33076b5c7bad3ab515654907`
- Naomi target: KallistiOS, SH-4, Naomi subarchitecture
- Deployment target: NetDIMM, treated as RAM-backed GD-ROM media; game data is transferred to the DIMM before the Naomi title reads it.

The Naomi platform bootstrap and build approach are being adapted from the separate VF3 Dreamcast-to-Naomi research repository. Dreamcast-specific game code is not being imported.

## First milestone: compile and execute the PPC interpreter

The first proof of concept builds the upstream `src/ppc/ppc.cpp` PowerPC 603e interpreter as SH-4 code and executes a small synthetic PowerPC instruction sequence on Naomi. It checks register results and instruction-cycle accounting.

This isolates the interpreter from the Model 3 board, graphics, sound and commercial game data. A successful build proves only that this interpreter source can be compiled and its small test can run; it does not prove that full VF3 gameplay can run at an acceptable speed.

GitHub Actions builds a versioned Naomi ROM. The ROM contains no commercial game data.

## Research order

1. **CPU feasibility** — compile the real interpreter for SH-4, execute known instructions, then measure actual VF3 PPC work per frame.
2. **Board and memory model** — identify the minimum Model 3 devices and memory regions required by VF3; do not attempt to allocate the entire Model 3 memory map in Naomi RAM.
3. **Graphics** — reuse the upstream scene-building logic where practical, then replace its desktop GPU renderer with Naomi PowerVR2 polygon submission.
4. **Sound and input** — investigate Model 3 sound-board behavior and map VF3 controls to Naomi JVS; neither is assumed to be a drop-in port.
5. **NetDIMM storage** — treat the DIMM as a RAM-backed optical-media device, not as extra general-purpose Naomi RAM. Design explicit reads/caching around the game’s storage needs.

## Working rules

- Keep this repository independent of the Dreamcast VF3 and Daytona projects.
- Use GitHub Actions for builds; no local toolchain installation is required from the user.
- Make small, testable changes and preserve each known-good artifact.
- Keep verified facts separate from hypotheses.
- Never commit commercial ROMs, game executables or extracted commercial assets.
- A successful compile is not a successful emulator or hardware test. Record Flycast and real Naomi validation separately.
- Do not claim playable compatibility until it has actually been demonstrated.


## Model 3 / NetDIMM integration probe

The synthetic Naomi integration ROM now exercises the production NetDIMM image
mount, fixed and banked CROM access, VROM streaming, Model 3 board initialization,
PPC bus reads/writes, and the upstream SceneBuilder parser/cache.

Its PPC-programmed Real3D DMA probe transfers marker words from PPC RAM into
tile RAM, low culling RAM, high culling RAM, and polygon RAM. It also enables DMA completion reporting, reads the completion status, and
acknowledges the completion interrupt. The probe then DMA-writes a synthetic
32x32 16-bit texture record into the Real3D texture FIFO, issues the flush
register, and checks decoded texels in texture RAM. These checks validate the
mapped bus, DMA and texture-upload memory paths; they do not yet demonstrate a
complete VF3 boot or rendered game frame. The v154 integration ROM passed the
DMA and completion checks in Flycast; the expanded texture test is being built.

The image contains synthetic markers and a tiny synthetic triangle only. No
commercial VF3 ROMs, executables, or extracted game assets are included.

## PPC floating-point rounding

The Naomi SH-4 build uses Berkeley SoftFloat Release 3e for the PPC interpreter's
IEEE-754 arithmetic and conversions. This is necessary because the SH-4 hardware
FPU does not provide the two directed rounding modes required by the PPC FPSCR.
The Naomi smoke ROM checks all four modes in the interpreter, including double
and single-precision division and single-precision stores. These tests are a
targeted validation of the rounding integration, not proof that every PPC
floating-point edge case or full VF3 execution is correct.

Berkeley SoftFloat is pinned to revision
`07ebd29db0cfb55723a79fbbffe99103ef38e305`. Its BSD-style licence is included
in [licenses/berkeley-softfloat-3-COPYING.txt](licenses/berkeley-softfloat-3-COPYING.txt).
