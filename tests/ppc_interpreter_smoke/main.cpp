/*
 * Builds and executes the upstream Model 3 PowerPC interpreter on Naomi.
 * The only program under test is a synthetic three-instruction PPC sequence.
 */
#include <kos.h>
#include <dc/minifont.h>
#include <dc/pvr.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "ppc.h"
extern "C" {
#include <softfloat.h>
}

KOS_INIT_FLAGS(INIT_DEFAULT);

alignas(4) static uint8_t guest_ram[0x800000];
static const uint8_t dummy_crom[4] = {0, 0, 0, 0};

class DummyBus final : public ppc::Bus {
public:
    uint8_t read8(uint32_t) override { return 0; }
    uint16_t read16(uint32_t) override { return 0; }
    uint32_t read32(uint32_t) override { return 0; }
    uint64_t read64(uint32_t) override { return 0; }
    void write8(uint32_t, uint8_t) override {}
    void write16(uint32_t, uint16_t) override {}
    void write32(uint32_t, uint32_t) override {}
    void write64(uint32_t, uint64_t) override {}
};

static bool run_softfloat_rounding_test() {
    static const uint64_t expected_positive[4] = {
        0x3fb999999999999aull, 0x3fb9999999999999ull,
        0x3fb999999999999aull, 0x3fb9999999999999ull
    };
    static const uint64_t expected_negative[4] = {
        0xbfb999999999999aull, 0xbfb9999999999999ull,
        0xbfb9999999999999ull, 0xbfb999999999999aull
    };
    static const uint32_t expected_positive_f32[4] = {
        0x3dcccccd, 0x3dcccccc, 0x3dcccccd, 0x3dcccccc
    };
    static const uint32_t expected_negative_f32[4] = {
        0xbdcccccd, 0xbdcccccc, 0xbdcccccc, 0xbdcccccd
    };
    static const uint_fast8_t modes[4] = {
        softfloat_round_near_even, softfloat_round_minMag,
        softfloat_round_max, softfloat_round_min
    };

    const float64_t one = ui32_to_f64(1);
    const float64_t ten = ui32_to_f64(10);
    for (unsigned mode = 0; mode < 4; ++mode) {
        softfloat_roundingMode = modes[mode];
        softfloat_exceptionFlags = 0;
        const float64_t positive = f64_div(one, ten);
        float64_t negative_one = one;
        negative_one.v |= 0x8000000000000000ull;
        const float64_t negative = f64_div(negative_one, ten);
        const float32_t positive_single = f64_to_f32(positive);
        const float32_t negative_single = f64_to_f32(negative);
        if (positive.v != expected_positive[mode] ||
            negative.v != expected_negative[mode] ||
            uint32_t(positive_single.v) != expected_positive_f32[mode] ||
            uint32_t(negative_single.v) != expected_negative_f32[mode]) {
            softfloat_roundingMode = softfloat_round_near_even;
            return false;
        }
    }
    softfloat_roundingMode = softfloat_round_near_even;
    return true;
}

static bool run_store_single_rounding_test() {
    static const uint32_t expected[2][4] = {
        { 0x3dcccccd, 0x3dcccccc, 0x3dcccccd, 0x3dcccccc },
        { 0xbdcccccd, 0xbdcccccc, 0xbdcccccc, 0xbdcccccd }
    };
    std::memset(guest_ram, 0, sizeof(guest_ram));
    DummyBus bus;
    ppc::Cpu cpu;
    cpu.attach(&bus, guest_ram, dummy_crom);
    cpu.configure(0, 2, 66000000, 0);

    const uint32_t stfs = (52u << 26) | (1u << 21) | 0x100u;
    const uint32_t stfsx = (31u << 26) | (1u << 21) | (2u << 16) |
                           (3u << 11) | (663u << 1);
    for (unsigned sign = 0; sign < 2; ++sign) {
        for (unsigned mode = 0; mode < 4; ++mode) {
            cpu.reset();
            cpu.s.fpscr = mode;
            cpu.s.f[1].d = sign ? -0.1 : 0.1;
            cpu.s.pc = 0;
            cpu.s.npc = 0;
            reinterpret_cast<uint32_t *>(guest_ram)[0] = stfs;
            if (cpu.execute(1) != 1 ||
                *reinterpret_cast<uint32_t *>(guest_ram + 0x100) != expected[sign][mode] ||
                cpu.s.fatal)
                return false;

            cpu.reset();
            cpu.s.fpscr = mode;
            cpu.s.r[2] = 0x100;
            cpu.s.r[3] = 0;
            cpu.s.f[1].d = sign ? -0.1 : 0.1;
            cpu.s.pc = 0;
            cpu.s.npc = 0;
            reinterpret_cast<uint32_t *>(guest_ram)[0] = stfsx;
            if (cpu.execute(1) != 1 ||
                *reinterpret_cast<uint32_t *>(guest_ram + 0x100) != expected[sign][mode] ||
                cpu.s.fatal)
                return false;
        }
    }
    return true;
}

static bool run_basic_fpu_rounding_test();

static bool run_interpreter_rounding_test() {
    static const uint64_t expected_double[2][4] = {
        { 0x3fb999999999999aull, 0x3fb9999999999999ull,
          0x3fb999999999999aull, 0x3fb9999999999999ull },
        { 0xbfb999999999999aull, 0xbfb9999999999999ull,
          0xbfb9999999999999ull, 0xbfb999999999999aull }
    };
    static const uint32_t expected_single[2][4] = {
        { 0x3dcccccd, 0x3dcccccc, 0x3dcccccd, 0x3dcccccc },
        { 0xbdcccccd, 0xbdcccccc, 0xbdcccccc, 0xbdcccccd }
    };

    std::memset(guest_ram, 0, sizeof(guest_ram));
    DummyBus bus;
    ppc::Cpu cpu;
    cpu.attach(&bus, guest_ram, dummy_crom);
    cpu.configure(0, 2, 66000000, 0);

    // fdiv f1,f2,f3 (primary opcode 63, XO 18).
    // fdivs uses the same fields with primary opcode 59.
    uint32_t *program = reinterpret_cast<uint32_t *>(guest_ram);
    const uint32_t fdiv = (63u << 26) | (1u << 21) | (2u << 16) | (3u << 11) | (18u << 1);
    const uint32_t fdivs = (59u << 26) | (1u << 21) | (2u << 16) | (3u << 11) | (18u << 1);

    for (unsigned sign = 0; sign < 2; ++sign) {
        for (unsigned mode = 0; mode < 4; ++mode) {
            cpu.reset();
            cpu.s.fpscr = mode;
            cpu.s.f[2].d = sign ? -1.0 : 1.0;
            cpu.s.f[3].d = 10.0;
            cpu.s.pc = 0;
            cpu.s.npc = 0;
            program[0] = fdiv;
            if (cpu.execute(1) != 1 || cpu.s.f[1].i != expected_double[sign][mode] || cpu.s.fatal)
                return false;

            cpu.reset();
            cpu.s.fpscr = mode;
            cpu.s.f[2].d = sign ? -1.0 : 1.0;
            cpu.s.f[3].d = 10.0;
            cpu.s.pc = 0;
            cpu.s.npc = 0;
            program[0] = fdivs;
            if (cpu.execute(1) != 1 || cpu.s.fatal)
                return false;

            union FloatBits { float f; uint32_t i; } expected;
            expected.i = expected_single[sign][mode];
            ppc::Fpr expected_fpr;
            expected_fpr.d = double(expected.f);
            if (cpu.s.f[1].i != expected_fpr.i)
                return false;
        }
    }
    return run_store_single_rounding_test() && run_basic_fpu_rounding_test();
}

static uint64_t widened_float_bits(uint32_t bits) {
    union FloatBits { float f; uint32_t i; } value;
    value.i = bits;
    ppc::Fpr widened;
    widened.d = double(value.f);
    return widened.i;
}

static bool run_basic_fpu_rounding_test() {
    static const uint64_t add_positive[4] = {
        0x3ff0000000000000ull, 0x3ff0000000000000ull,
        0x3ff0000000000001ull, 0x3ff0000000000000ull
    };
    static const uint64_t add_negative[4] = {
        0xbff0000000000000ull, 0xbff0000000000000ull,
        0xbff0000000000000ull, 0xbff0000000000001ull
    };
    static const uint64_t sqrt_two[4] = {
        0x3ff6a09e667f3bcdull, 0x3ff6a09e667f3bccull,
        0x3ff6a09e667f3bcdull, 0x3ff6a09e667f3bccull
    };
    static const uint32_t integer_nearest_modes[4] = { 2, 1, 2, 1 };

    std::memset(guest_ram, 0, sizeof(guest_ram));
    DummyBus bus;
    ppc::Cpu cpu;
    cpu.attach(&bus, guest_ram, dummy_crom);
    cpu.configure(0, 2, 66000000, 0);
    uint32_t *program = reinterpret_cast<uint32_t *>(guest_ram);

    // A-form/X-form floating-point instruction encodings.
    const uint32_t fadd  = (63u << 26) | (1u << 21) | (2u << 16) | (3u << 11) | (21u << 1);
    const uint32_t fsub  = (63u << 26) | (1u << 21) | (2u << 16) | (3u << 11) | (20u << 1);
    // fmul is an A-form instruction: its third source is FRC (bits 6..10), not FRB.
    const uint32_t fmul  = (63u << 26) | (1u << 21) | (2u << 16) | (3u << 6) | (25u << 1);
    const uint32_t fsqrt = (63u << 26) | (1u << 21) | (2u << 11) | (22u << 1);
    const uint32_t fctiw = (63u << 26) | (1u << 21) | (2u << 11) | (14u << 1);
    const uint32_t fctiwz= (63u << 26) | (1u << 21) | (2u << 11) | (15u << 1);

    for (unsigned mode = 0; mode < 4; ++mode) {
        // 1 + 2^-53 is exactly halfway; directed rounding exposes the mode.
        cpu.reset();
        cpu.s.fpscr = mode;
        cpu.s.f[2].d = 1.0;
        cpu.s.f[3].d = 0x1p-53;
        cpu.s.pc = cpu.s.npc = 0;
        program[0] = fadd;
        if (cpu.execute(1) != 1 || cpu.s.fatal || cpu.s.f[1].i != add_positive[mode])
            return false;

        // -1 + -2^-53 exercises the opposite sign/direction.
        cpu.reset();
        cpu.s.fpscr = mode;
        cpu.s.f[2].d = -1.0;
        cpu.s.f[3].d = -0x1p-53;
        cpu.s.pc = cpu.s.npc = 0;
        program[0] = fadd;
        if (cpu.execute(1) != 1 || cpu.s.fatal || cpu.s.f[1].i != add_negative[mode])
            return false;

        // sqrt(2) has distinct directed-rounding results.
        cpu.reset();
        cpu.s.fpscr = mode;
        cpu.s.f[2].d = 2.0;
        cpu.s.pc = cpu.s.npc = 0;
        program[0] = fsqrt;
        if (cpu.execute(1) != 1 || cpu.s.fatal || cpu.s.f[1].i != sqrt_two[mode])
            return false;

        // fctiw follows FPSCR; fctiwz always truncates toward zero.
        cpu.reset();
        cpu.s.fpscr = mode;
        cpu.s.f[2].d = 1.5;
        cpu.s.pc = cpu.s.npc = 0;
        program[0] = fctiw;
        if (cpu.execute(1) != 1 || cpu.s.fatal ||
            uint32_t(cpu.s.f[1].i) != integer_nearest_modes[mode])
            return false;

        cpu.reset();
        cpu.s.fpscr = mode;
        cpu.s.f[2].d = 1.5;
        cpu.s.pc = cpu.s.npc = 0;
        program[0] = fctiwz;
        if (cpu.execute(1) != 1 || cpu.s.fatal || uint32_t(cpu.s.f[1].i) != 1)
            return false;
    }

    // fsubs and fmuls are also exercised under all four modes with exact inputs.
    for (unsigned mode = 0; mode < 4; ++mode) {
        cpu.reset();
        cpu.s.fpscr = mode;
        cpu.s.f[2].d = 3.0;
        cpu.s.f[3].d = 1.0;
        cpu.s.pc = cpu.s.npc = 0;
        program[0] = fsub;
        if (cpu.execute(1) != 1 || cpu.s.fatal || cpu.s.f[1].d != 2.0)
            return false;

        cpu.reset();
        cpu.s.fpscr = mode;
        cpu.s.f[2].d = 1.5;
        cpu.s.f[3].d = 2.0;
        cpu.s.pc = cpu.s.npc = 0;
        program[0] = fmul;
        if (cpu.execute(1) != 1 || cpu.s.fatal || cpu.s.f[1].d != 3.0)
            return false;
    }
    // Multiply-add family: the reference model deliberately rounds the product
    // and sum separately (not as a fused operation). Choose an exact product and
    // a halfway addend so the final result distinguishes all FPSCR modes.
    const uint32_t fmadd  = (63u << 26) | (1u << 21) | (2u << 16) | (3u << 11) | (4u << 6) | (29u << 1);
    const uint32_t fmsub  = (63u << 26) | (1u << 21) | (2u << 16) | (3u << 11) | (4u << 6) | (28u << 1);
    const uint32_t fnmadd = (63u << 26) | (1u << 21) | (2u << 16) | (3u << 11) | (4u << 6) | (31u << 1);
    const uint32_t fnmsub = (63u << 26) | (1u << 21) | (2u << 16) | (3u << 11) | (4u << 6) | (30u << 1);
    const uint32_t fmadds  = (59u << 26) | (1u << 21) | (2u << 16) | (3u << 11) | (4u << 6) | (29u << 1);
    const uint32_t fmsubs  = (59u << 26) | (1u << 21) | (2u << 16) | (3u << 11) | (4u << 6) | (28u << 1);
    const uint32_t fnmadds = (59u << 26) | (1u << 21) | (2u << 16) | (3u << 11) | (4u << 6) | (31u << 1);
    const uint32_t fnmsubs = (59u << 26) | (1u << 21) | (2u << 16) | (3u << 11) | (4u << 6) | (30u << 1);
    const uint32_t double_ops[4] = { fmadd, fmsub, fnmadd, fnmsub };
    const uint32_t single_ops[4] = { fmadds, fmsubs, fnmadds, fnmsubs };
    static const uint32_t fma_positive_halfway[4] = {
        0x3f800000, 0x3f800000, 0x3f800001, 0x3f800000
    };
    static const uint32_t fma_negative_halfway[4] = {
        0xbf800000, 0xbf800000, 0xbf800000, 0xbf800001
    };
    for (unsigned mode = 0; mode < 4; ++mode) {
        for (unsigned op = 0; op < 4; ++op) {
            // Set FRA*FRC to +1 or -1; FRB supplies +/- one half-ULP.
            const bool negate_result = op >= 2;
            const bool subtract_form = (op == 1 || op == 3);
            for (unsigned sign = 0; sign < 2; ++sign) {
                const double product = sign ? -1.0 : 1.0;
                const double addend = (sign ^ subtract_form) ? -0x1p-53 : 0x1p-53;
                const double expected_value_sign = (sign ^ negate_result) ? -1.0 : 1.0;
                const uint64_t expected_bits = expected_value_sign < 0 ? add_negative[mode] : add_positive[mode];
                cpu.reset();
                cpu.s.fpscr = mode;
                cpu.s.f[2].d = product;
                cpu.s.f[3].d = addend;
                cpu.s.f[4].d = 1.0;
                cpu.s.pc = cpu.s.npc = 0;
                program[0] = double_ops[op];
                if (cpu.execute(1) != 1 || cpu.s.fatal || cpu.s.f[1].i != expected_bits) return false;

                const double single_addend = (sign ^ subtract_form) ? -0x1p-24 : 0x1p-24;
                cpu.reset();
                cpu.s.fpscr = mode;
                cpu.s.f[2].d = product;
                cpu.s.f[3].d = single_addend;
                cpu.s.f[4].d = 1.0;
                cpu.s.pc = cpu.s.npc = 0;
                program[0] = single_ops[op];
                const uint32_t expected_single_bits = expected_value_sign < 0 ? fma_negative_halfway[mode] : fma_positive_halfway[mode];
                if (cpu.execute(1) != 1 || cpu.s.fatal || cpu.s.f[1].i != widened_float_bits(expected_single_bits)) return false;
            }
        }
    }

    // Single-precision arithmetic rounds the operation result to binary32.
    // 1 + 2^-24 is exactly halfway between adjacent binary32 values.
    static const uint32_t positive_halfway[4] = {
        0x3f800000, 0x3f800000, 0x3f800001, 0x3f800000
    };
    static const uint32_t negative_halfway[4] = {
        0xbf800000, 0xbf800000, 0xbf800000, 0xbf800001
    };
    const uint32_t fadds = (59u << 26) | (1u << 21) | (2u << 16) | (3u << 11) | (21u << 1);
    const uint32_t fsubs = (59u << 26) | (1u << 21) | (2u << 16) | (3u << 11) | (20u << 1);
    const uint32_t fmuls = (59u << 26) | (1u << 21) | (2u << 16) | (3u << 6) | (25u << 1);
    for (unsigned mode = 0; mode < 4; ++mode) {
        // fadds: positive and negative halfway cases.
        cpu.reset();
        cpu.s.fpscr = mode;
        cpu.s.f[2].d = 1.0;
        cpu.s.f[3].d = 0x1p-24;
        cpu.s.pc = cpu.s.npc = 0;
        program[0] = fadds;
        if (cpu.execute(1) != 1 || cpu.s.fatal || cpu.s.f[1].i != widened_float_bits(positive_halfway[mode])) return false;

        cpu.reset();
        cpu.s.fpscr = mode;
        cpu.s.f[2].d = -1.0;
        cpu.s.f[3].d = -0x1p-24;
        cpu.s.pc = cpu.s.npc = 0;
        program[0] = fadds;
        if (cpu.execute(1) != 1 || cpu.s.fatal || cpu.s.f[1].i != widened_float_bits(negative_halfway[mode])) return false;

        // fsubs: 1 - (-2^-24) and -1 - 2^-24 exercise both directions.
        cpu.reset();
        cpu.s.fpscr = mode;
        cpu.s.f[2].d = 1.0;
        cpu.s.f[3].d = -0x1p-24;
        cpu.s.pc = cpu.s.npc = 0;
        program[0] = fsubs;
        if (cpu.execute(1) != 1 || cpu.s.fatal || cpu.s.f[1].i != widened_float_bits(positive_halfway[mode])) return false;

        cpu.reset();
        cpu.s.fpscr = mode;
        cpu.s.f[2].d = -1.0;
        cpu.s.f[3].d = 0x1p-24;
        cpu.s.pc = cpu.s.npc = 0;
        program[0] = fsubs;
        if (cpu.execute(1) != 1 || cpu.s.fatal || cpu.s.f[1].i != widened_float_bits(negative_halfway[mode])) return false;

        // fmuls: exact binary64 product is a binary32 halfway value.
        cpu.reset();
        cpu.s.fpscr = mode;
        cpu.s.f[2].d = 1.0 + 0x1p-24;
        cpu.s.f[3].d = 1.0;
        cpu.s.pc = cpu.s.npc = 0;
        program[0] = fmuls;
        if (cpu.execute(1) != 1 || cpu.s.fatal || cpu.s.f[1].i != widened_float_bits(positive_halfway[mode])) return false;

        // Negative fmuls checks directed rounding in the opposite direction.
        cpu.reset();
        cpu.s.fpscr = mode;
        cpu.s.f[2].d = -(1.0 + 0x1p-24);
        cpu.s.f[3].d = 1.0;
        cpu.s.pc = cpu.s.npc = 0;
        program[0] = fmuls;
        if (cpu.execute(1) != 1 || cpu.s.fatal || cpu.s.f[1].i != widened_float_bits(negative_halfway[mode])) return false;

        // Reciprocal estimate opcode is exact in this upstream reference model;
        // it returns a single-precision 1/x, so 1/3 exposes all four modes.
        static const uint32_t reciprocal_positive[4] = {
            0x3eaaaaab, 0x3eaaaaaa, 0x3eaaaaab, 0x3eaaaaaa
        };
        static const uint32_t reciprocal_negative[4] = {
            0xbeaaaaab, 0xbeaaaaaa, 0xbeaaaaaa, 0xbeaaaaab
        };
        const uint32_t fres = (59u << 26) | (1u << 21) | (2u << 11) | (24u << 1);
        cpu.reset();
        cpu.s.fpscr = mode;
        cpu.s.f[2].d = 3.0;
        cpu.s.pc = cpu.s.npc = 0;
        program[0] = fres;
        if (cpu.execute(1) != 1 || cpu.s.fatal || cpu.s.f[1].i != widened_float_bits(reciprocal_positive[mode])) return false;

        cpu.reset();
        cpu.s.fpscr = mode;
        cpu.s.f[2].d = -3.0;
        cpu.s.pc = cpu.s.npc = 0;
        program[0] = fres;
        if (cpu.execute(1) != 1 || cpu.s.fatal || cpu.s.f[1].i != widened_float_bits(reciprocal_negative[mode])) return false;

        // frsp rounds a binary64 FPR value directly to binary32 under FPSCR.RN.
        const uint32_t frsp = (63u << 26) | (1u << 21) | (2u << 11) | (12u << 1);
        cpu.reset();
        cpu.s.fpscr = mode;
        cpu.s.f[2].d = 1.0 + 0x1p-24;
        cpu.s.pc = cpu.s.npc = 0;
        program[0] = frsp;
        if (cpu.execute(1) != 1 || cpu.s.fatal || cpu.s.f[1].i != widened_float_bits(positive_halfway[mode])) return false;

        cpu.reset();
        cpu.s.fpscr = mode;
        cpu.s.f[2].d = -(1.0 + 0x1p-24);
        cpu.s.pc = cpu.s.npc = 0;
        program[0] = frsp;
        if (cpu.execute(1) != 1 || cpu.s.fatal || cpu.s.f[1].i != widened_float_bits(negative_halfway[mode])) return false;

        // fsqrts checks a non-exact square root rounded to binary32.
        static const uint32_t sqrt_two_single[4] = {
            0x3fb504f3, 0x3fb504f3, 0x3fb504f4, 0x3fb504f3
        };
        const uint32_t fsqrts = (59u << 26) | (1u << 21) | (2u << 11) | (22u << 1);
        cpu.reset();
        cpu.s.fpscr = mode;
        cpu.s.f[2].d = 2.0;
        cpu.s.pc = cpu.s.npc = 0;
        program[0] = fsqrts;
        if (cpu.execute(1) != 1 || cpu.s.fatal || cpu.s.f[1].i != widened_float_bits(sqrt_two_single[mode])) return false;
    }
    return true;
}

static bool run_interpreter_test() {
    std::memset(guest_ram, 0, sizeof(guest_ram));

    DummyBus bus;
    ppc::Cpu cpu;
    cpu.attach(&bus, guest_ram, dummy_crom);
    cpu.configure(0, 2, 66000000, 0);
    cpu.reset();

    // PowerPC: addi r3,0,7; addi r4,0,5; add r5,r3,r4.
    uint32_t *program = reinterpret_cast<uint32_t *>(guest_ram);
    program[0] = 0x38600007u;
    program[1] = 0x38800005u;
    program[2] = 0x7CA32214u;

    // Start at guest RAM address zero instead of the Model 3 reset vector.
    cpu.s.pc = 0;
    cpu.s.npc = 0;

    const int executed = cpu.execute(3);
    return executed == 3 &&
           cpu.s.total_cycles == 3 &&
           cpu.s.r[3] == 7 &&
           cpu.s.r[4] == 5 &&
           cpu.s.r[5] == 12 &&
           !cpu.s.fatal;
}

#define FONT_W 12
#define FONT_H 24

static pvr_ptr_t font_texture;

static void font_init() {
    uint16_t *vram = static_cast<uint16_t *>(font_texture);
    for(int y = 0; y < 8; ++y) {
        for(int x = 0; x < 16; ++x) {
            minifont_draw(vram, 256, static_cast<uint32_t>(y * 16 + x));
            vram += 16;
        }
        vram += 23 * 256;
    }
}

static void draw_char(float x, float y, unsigned char c) {
    if(c <= ' ' || c >= 127) return;

    pvr_vertex_t v;
    const int ix = (c % 16) * 16;
    const int iy = (c / 16) * 24;
    const float u1 = ix / 256.0f;
    const float v1 = iy / 256.0f;
    const float u2 = (ix + FONT_W) / 256.0f;
    const float v2 = (iy + FONT_H) / 256.0f;

    v.flags = PVR_CMD_VERTEX;
    v.x = x; v.y = y + FONT_H; v.z = 1.0f;
    v.u = u1; v.v = v2;
    v.argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
    v.oargb = 0;
    pvr_prim(&v, sizeof(v));

    v.x = x; v.y = y; v.u = u1; v.v = v1;
    pvr_prim(&v, sizeof(v));

    v.x = x + FONT_W; v.y = y + FONT_H; v.u = u2; v.v = v2;
    pvr_prim(&v, sizeof(v));

    v.flags = PVR_CMD_VERTEX_EOL;
    v.x = x + FONT_W; v.y = y; v.u = u2; v.v = v1;
    pvr_prim(&v, sizeof(v));
}

static void draw_text(float x, float y, const char *s) {
    if(!s || !s[0]) return;

    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t poly;
    pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY,
                     PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_NONTWIDDLED,
                     256, 256, font_texture, PVR_FILTER_NONE);
    pvr_poly_compile(&poly, &cxt);
    pvr_prim(&poly, sizeof(poly));

    for(size_t i = 0; s[i]; ++i) {
        draw_char(x, y, static_cast<unsigned char>(s[i]));
        x += FONT_W;
    }
}

static void draw_screen(bool passed, bool softfloat_passed, bool ppc_rounding_passed) {
    pvr_wait_ready();
    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_TR_POLY);
    draw_text(40.0f, 64.0f, "VF3 RECOMP - NAOMI");
    draw_text(40.0f, 112.0f, "UPSTREAM PPC INTERPRETER");
    draw_text(40.0f, 160.0f, passed ? "SH-4 EXECUTION: PASS" : "SH-4 EXECUTION: FAIL");
    draw_text(40.0f, 224.0f, "SYNTHETIC 3-INSTRUCTION TEST");
    draw_text(40.0f, 272.0f, softfloat_passed ? "SOFTFLOAT 4-MODE TEST: PASS" : "SOFTFLOAT 4-MODE TEST: FAIL");
    draw_text(40.0f, 320.0f, ppc_rounding_passed ? "PPC FPU 4-MODE TEST: PASS" : "PPC FPU 4-MODE TEST: FAIL");
    pvr_list_finish();
    pvr_scene_finish();
}

int main() {
    const bool passed = run_interpreter_test();
    const bool softfloat_passed = run_softfloat_rounding_test();
    const bool ppc_rounding_passed = run_interpreter_rounding_test();
    printf("VF3 Recomp PPC interpreter smoke test: %s\n", passed ? "PASS" : "FAIL");

    pvr_init_defaults();
    pvr_set_bg_color(0.02f, 0.06f, 0.12f);

    font_texture = pvr_mem_malloc(256 * 256 * 2);
    if(!font_texture) {
        printf("PVR font texture allocation failed\n");
        for(;;) thd_sleep(1000);
    }
    font_init();

    for(;;) {
        draw_screen(passed, softfloat_passed, ppc_rounding_passed);
        thd_sleep(16);
    }
    return 0;
}
