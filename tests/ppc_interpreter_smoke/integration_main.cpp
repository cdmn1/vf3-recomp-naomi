#include <kos.h>
#include <dc/minifont.h>
#include <dc/pvr.h>

#include <cstdint>
#include <stdio.h>
#include <cstring>
#include <string>

#include "vf3_image.h"
#include "cart_vrom_bridge.h"
#include "cart_vrom.h"
#include "r3d_scene.h"
#include "model3.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

// The storage/board probe does not need the production recompiler entry map.
// Supply only the VF3 profile fields used by Model3::load() in this test link.
namespace m3 {
const Profile VF3_REV_D = [] {
    Profile p{};
    p.set = "vf3";
    p.title = "Virtua Fighter 3 Naomi integration";
    p.name = "Virtua Fighter 3";
    p.online = true;
    p.program_at = 0x710000u;
    p.program_size = 0x0A0000u;
    return p;
}();
}

// This probe mounts the synthetic NetDIMM image, initializes the real Model3
// board storage, and invokes the production SceneBuilder parser/cache.

static const uint32_t expected_model[19] = {
    0x00000000u, 0x00000006u, 0x00000000u, 0x00000000u,
    0xFFFFFFFFu, 0x00000000u, 0x00800000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00010000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00010000u, 0x00000000u, 0x00000000u
};

struct Results {
    bool mount = false;
    bool board = false;
    bool board_load = false;
    bool power_ppc = false;
    bool crom_bus = false;
    bool tile_bus = false;
    bool real3d_bus = false;
    bool crom = false;
    bool vrom = false;
    bool scene = false;
    int mount_status = 0;
    int vrom_status = 0;
    size_t meshes = 0;
    size_t vertices = 0;
    size_t cached = 0;
    size_t ram_bytes = 0, tile_bytes = 0, cull_lo_words = 0;
    size_t cull_hi_words = 0, poly_words = 0, fifo_capacity = 0;
    size_t tex_words = 0;
    size_t pvr_before = 0, pvr_after = 0;
    unsigned hw_mode = 0, pvr_total = 0;
    bool pvr_error = false;
};

static Results run_integration()
{
    Results r;
    r.mount_status = static_cast<int>(naomi_model3_mount_netdimm_image());
    r.mount = r.mount_status == NAOMI_VF3_IMAGE_OK;
    if (!r.mount)
        return r;

    m3::Model3 board;
    std::string error;
    r.hw_mode = static_cast<unsigned>(hardware_sys_mode(nullptr));
    r.pvr_total = static_cast<unsigned>(PVR_RAM_SIZE);
    r.pvr_before = pvr_mem_available();
    r.board_load = board.load("", error);
    r.pvr_after = pvr_mem_available();
    r.pvr_error = error.find("PVR VRAM") != std::string::npos;
    r.ram_bytes = board.ram.size();
    r.tile_bytes = board.tile_ram.size();
    r.cull_lo_words = board.cull_lo.size();
    r.cull_hi_words = board.cull_hi.size();
    r.poly_words = board.poly_ram.size();
    r.fifo_capacity = board.tex_fifo.capacity();
    r.tex_words = board.tex_ram.size();
    if (r.board_load) {
        r.board = board.ram.size() == 0x800000u &&
                  board.backup.size() == 0x20000u &&
                  board.security.size() == 0x20000u &&
                  board.tile_ram.size() == 0x120000u &&
                  board.cull_lo.size() == 0x400000u / 4u + m3::Model3::CULL_PAD &&
                  board.cull_hi.size() == 0x100000u / 4u + m3::Model3::CULL_PAD &&
                  board.poly_ram.size() == 0x400000u / 4u &&
                  board.tex_fifo.capacity() >= 0x100000u / 4u &&
                  board.tex_ram.size() == 2048u * 2048u;
    }

    if (r.board_load) {
        // Exercise the real board power-on/reset path, then execute a tiny
        // synthetic PPC program through the CPU attached to Model3's bus/RAM.
        board.power_on();
        uint32_t *program = reinterpret_cast<uint32_t *>(board.ram.data());
        program[0] = 0x38600007u;
        program[1] = 0x38800005u;
        program[2] = 0x7CA32214u;
        board.cpu.s.pc = 0;
        board.cpu.s.npc = 0;
        const int executed = board.cpu.execute(3);
        r.power_ppc = executed == 3 &&
                      board.cpu.s.total_cycles == 3 &&
                      board.cpu.s.r[3] == 7 &&
                      board.cpu.s.r[4] == 5 &&
                      board.cpu.s.r[5] == 12 &&
                      !board.cpu.s.fatal;

        // Exercise PPC load/store instructions through the attached Model3 bus:
        // read fixed CROM, write the active-low CROM bank register, then read banked CROM.
        program[0] = 0x3CC0FF80u; // lis r6, 0xFF80
        program[1] = 0x80E60000u; // lwz r7, 0(r6)
        program[2] = 0x3D00F010u; // lis r8, 0xF010
        program[3] = 0x392000FCu; // li r9, 0xFC (select bank 3)
        program[4] = 0x99280008u; // stb r9, 8(r8) (CROM bank register)
        program[5] = 0x3D40FF00u; // lis r10, 0xFF00
        program[6] = 0x816A0000u; // lwz r11, 0(r10)
        board.cpu.s.pc = 0;
        board.cpu.s.npc = 0;
        const int bus_executed = board.cpu.execute(7);
        r.crom_bus = bus_executed == 7 &&
                     board.cpu.s.r[7] == 0x13579BDFu &&
                     board.cpu.s.r[11] == 0x2468ACE0u &&
                     !board.cpu.s.fatal;

        // Exercise PPC stw/lwz through the mapped Real3D tile RAM window.
        program[0] = 0x3D80F100u; // lis r12, 0xF100
        program[1] = 0x3DA0A55Au; // lis r13, 0xA55A
        program[2] = 0x61ADC33Cu; // ori r13, r13, 0xC33C
        program[3] = 0x91AC0000u; // stw r13, 0(r12)
        program[4] = 0x81CC0000u; // lwz r14, 0(r12)
        board.cpu.s.pc = 0;
        board.cpu.s.npc = 0;
        const int tile_executed = board.cpu.execute(5);
        r.tile_bus = tile_executed == 5 &&
                     board.cpu.s.r[14] == 0xA55AC33Cu &&
                     !board.cpu.s.fatal;

        // Read the Real3D status register, then exercise its DMA engine:
        // copy one word from PPC RAM into the mapped tile RAM window.
        const uint32_t dma_source_word = 0xA1B2C3D4u;
        *reinterpret_cast<uint32_t *>(board.ram.data() + 0x100) = dma_source_word;
        program[0] = 0x3DE08400u; // lis r15, 0x8400 (Real3D register window)
        program[1] = 0x820F0000u; // lwz r16, 0(r15)
        program[2] = 0x3E20C200u; // lis r17, 0xC200 (Real3D DMA registers)
        program[3] = 0x3E400001u; // lis r18, 1 (bswap(0x100), DMA source)
        program[4] = 0x92510000u; // stw r18, 0(r17)
        program[5] = 0x3E600400u; // lis r19, 0x0400
        program[6] = 0x627300F1u; // ori r19, r19, 0x00F1 (bswap(0xF1000004))
        program[7] = 0x92710004u; // stw r19, 4(r17)
        program[8] = 0x3E800100u; // lis r20, 0x0100 (bswap(1), one DMA word)
        program[9] = 0x92910008u; // stw r20, 8(r17) (start DMA)
        program[10] = 0x3EA0F100u; // lis r21, 0xF100
        program[11] = 0x82D50004u; // lwz r22, 4(r21)
        board.cpu.s.pc = 0;
        board.cpu.s.npc = 0;
        const int real3d_executed = board.cpu.execute(12);
        r.real3d_bus = real3d_executed == 12 &&
                       board.cpu.s.r[16] == 0xFFFFFFFDu &&
                       board.cpu.s.r[22] == dma_source_word &&
                       !board.cpu.s.fatal;
    }

    const uint8_t *fixed = naomi_model3_crom_fixed();
    const int bank_selected = naomi_model3_crom_select_bank(0xFCu); // bank 3
    // The synthetic image deliberately places non-zero bus markers in both
    // CROM regions. Check the exact little-endian bytes rather than expecting
    // zero-filled ROM; the PPC bus probe independently checks SH-4/PPC words.
    r.crom = fixed &&
             fixed[0] == 0xDFu && fixed[1] == 0x9Bu &&
             fixed[2] == 0x57u && fixed[3] == 0x13u &&
             bank_selected &&
             naomi_model3_crom_read8(0) == 0xE0u &&
             naomi_model3_crom_read8(1) == 0xACu &&
             naomi_model3_crom_read8(2) == 0x68u &&
             naomi_model3_crom_read8(3) == 0x24u &&
             naomi_model3_crom_last_status() == NAOMI_CART_IMAGE_OK;

    alignas(32) uint32_t actual_model[19] = {};
    r.vrom_status = naomi_model3_vrom_read_words(0x00100000u, actual_model, 19);
    r.vrom = r.vrom_status == NAOMI_CART_VROM_OK &&
             std::memcmp(actual_model, expected_model, sizeof(expected_model)) == 0;

    if (r.vrom && board.poly_ram.size() == 0x400000u / 4u) {
        video::SceneBuilder builder;
        const bool first = builder.naomi_test_vrom_model(
            0x00100000u, board.poly_ram.data(), &r.meshes, &r.vertices);
        const size_t cache_after_first = builder.cached();
        size_t second_meshes = 0, second_vertices = 0;
        const bool second = builder.naomi_test_vrom_model(
            0x00100000u, board.poly_ram.data(), &second_meshes, &second_vertices);
        r.cached = builder.cached();
        r.scene = first && second && r.meshes == 1 && r.vertices == 3 &&
                  second_meshes == r.meshes && second_vertices == r.vertices &&
                  cache_after_first == 1 && r.cached == 1;
    }
    return r;
}

#define FONT_W 12
#define FONT_H 24
static pvr_ptr_t font_texture;

static void font_init()
{
    uint16_t *vram = static_cast<uint16_t *>(font_texture);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 16; ++x) {
            minifont_draw(vram, 256, static_cast<uint32_t>(y * 16 + x));
            vram += 16;
        }
        vram += 23 * 256;
    }
}

static void draw_char(float x, float y, unsigned char c)
{
    if (c <= ' ' || c >= 127) return;
    pvr_vertex_t v;
    const int ix = (c % 16) * 16, iy = (c / 16) * 24;
    const float u1 = ix / 256.0f, v1 = iy / 256.0f;
    const float u2 = (ix + FONT_W) / 256.0f, v2 = (iy + FONT_H) / 256.0f;
    v.flags = PVR_CMD_VERTEX;
    v.x = x; v.y = y + FONT_H; v.z = 1.0f; v.u = u1; v.v = v2;
    v.argb = PVR_PACK_COLOR(1, 1, 1, 1); v.oargb = 0;
    pvr_prim(&v, sizeof(v));
    v.x = x; v.y = y; v.u = u1; v.v = v1; pvr_prim(&v, sizeof(v));
    v.x = x + FONT_W; v.y = y + FONT_H; v.u = u2; v.v = v2; pvr_prim(&v, sizeof(v));
    v.flags = PVR_CMD_VERTEX_EOL;
    v.x = x + FONT_W; v.y = y; v.u = u2; v.v = v1; pvr_prim(&v, sizeof(v));
}

static void draw_text(float x, float y, const char *s)
{
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t poly;
    pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY,
        PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_NONTWIDDLED,
        256, 256, font_texture, PVR_FILTER_NONE);
    pvr_poly_compile(&poly, &cxt);
    pvr_prim(&poly, sizeof(poly));
    for (size_t i = 0; s && s[i]; ++i) {
        draw_char(x, y, static_cast<unsigned char>(s[i]));
        x += FONT_W;
    }
}

static void draw_screen(const Results &r)
{
    pvr_wait_ready();
    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_TR_POLY);
    draw_text(40, 48, "VF3 NAOMI - MODEL3 INTEGRATION");
    draw_text(40, 96, r.mount ? "NETDIMM IMAGE MOUNT: PASS" : "NETDIMM IMAGE MOUNT: FAIL");
    draw_text(40, 136, r.board ? "MODEL3 BOARD INIT: PASS" : "MODEL3 BOARD INIT: FAIL");
    draw_text(40, 156, r.board_load ? "MODEL3 LOAD: PASS" : "MODEL3 LOAD: FAIL");
    draw_text(40, 176, r.crom ? "FIXED/BANKED CROM: PASS" : "FIXED/BANKED CROM: FAIL");
    draw_text(40, 196, r.power_ppc ? "MODEL3 POWER-ON/PPC BUS: PASS" : "MODEL3 POWER-ON/PPC BUS: FAIL");
    draw_text(40, 216, r.vrom ? "VROM STREAM READ: PASS" : "VROM STREAM READ: FAIL");
    draw_text(40, 236, r.crom_bus ? "PPC FIXED/BANKED CROM BUS: PASS" : "PPC FIXED/BANKED CROM BUS: FAIL");
    draw_text(40, 256, r.tile_bus ? "PPC TILE RAM BUS: PASS" : "PPC TILE RAM BUS: FAIL");
    draw_text(40, 276, r.scene ? "SCENEBUILDER PARSE/CACHE: PASS" : "SCENEBUILDER PARSE/CACHE: FAIL");
    draw_text(40, 296, r.real3d_bus ? "PPC REAL3D REG/DMA BUS: PASS" : "PPC REAL3D REG/DMA BUS: FAIL");
    char line[64];
    snprintf(line, sizeof(line), "MOUNT STATUS: %d", r.mount_status);
    draw_text(40, 316, line);
    snprintf(line, sizeof(line), "VROM STATUS: %d", r.vrom_status);
    draw_text(40, 336, line);
    snprintf(line, sizeof(line), "MESHES: %u VERTICES: %u CACHE: %u",
        static_cast<unsigned>(r.meshes), static_cast<unsigned>(r.vertices),
        static_cast<unsigned>(r.cached));
    draw_text(40, 356, line);
    snprintf(line, sizeof(line), "RAM:%u TILE:%u", static_cast<unsigned>(r.ram_bytes), static_cast<unsigned>(r.tile_bytes));
    draw_text(40, 376, line);
    snprintf(line, sizeof(line), "CULL:%u/%u POLY:%u", static_cast<unsigned>(r.cull_lo_words), static_cast<unsigned>(r.cull_hi_words), static_cast<unsigned>(r.poly_words));
    draw_text(40, 396, line);
    snprintf(line, sizeof(line), "FIFO:%u TEX:%u", static_cast<unsigned>(r.fifo_capacity), static_cast<unsigned>(r.tex_words));
    draw_text(40, 416, line);
    snprintf(line, sizeof(line), "PVR:%u>%u HW:%u KOS:%u NAOMI:16", static_cast<unsigned>(r.pvr_before), static_cast<unsigned>(r.pvr_after), r.hw_mode, r.pvr_total / (1024u * 1024u));
    draw_text(40, 436, line);
    draw_text(40, 480, r.pvr_error ? "PVR VRAM ALLOCATION FAILED" : "SYNTHETIC ROM DATA - NO GAME ASSETS");
    pvr_list_finish();
    pvr_scene_finish();
}

int main()
{
    pvr_init_defaults();
    pvr_set_bg_color(0.02f, 0.06f, 0.12f);
    const Results results = run_integration();
    font_texture = pvr_mem_malloc(256 * 256 * 2);
    if (!font_texture) {
        printf("PVR font texture allocation failed\n");
        for (;;) thd_sleep(1000);
    }
    font_init();
    for (;;) {
        draw_screen(results);
        thd_sleep(16);
    }
    return 0;
}
