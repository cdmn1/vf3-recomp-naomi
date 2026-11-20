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

KOS_INIT_FLAGS(INIT_DEFAULT);

static alignas(4) uint8_t guest_ram[0x800000];
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

static void draw_screen(bool passed) {
    pvr_wait_ready();
    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_TR_POLY);
    draw_text(40.0f, 64.0f, "VF3 RECOMP - NAOMI");
    draw_text(40.0f, 112.0f, "UPSTREAM PPC INTERPRETER");
    draw_text(40.0f, 160.0f, passed ? "SH-4 EXECUTION: PASS" : "SH-4 EXECUTION: FAIL");
    draw_text(40.0f, 224.0f, "SYNTHETIC 3-INSTRUCTION TEST");
    pvr_list_finish();
    pvr_scene_finish();
}

int main() {
    const bool passed = run_interpreter_test();
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
        draw_screen(passed);
        thd_sleep(16);
    }
    return 0;
}
