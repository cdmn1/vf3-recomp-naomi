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
    r.board_load = board.load("", error);
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

    const uint8_t *fixed = naomi_model3_crom_fixed();
    const int bank_selected = naomi_model3_crom_select_bank(0xFCu); // bank 3
    const uint32_t bank_word = naomi_model3_crom_read32(0);
    r.crom = fixed && fixed[0] == 0 && fixed[1] == 0 &&
             bank_selected && bank_word == 0 &&
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
    draw_text(40, 216, r.vrom ? "VROM STREAM READ: PASS" : "VROM STREAM READ: FAIL");
    draw_text(40, 256, r.scene ? "SCENEBUILDER PARSE/CACHE: PASS" : "SCENEBUILDER PARSE/CACHE: FAIL");
    char line[64];
    snprintf(line, sizeof(line), "MOUNT STATUS: %d", r.mount_status);
    draw_text(40, 320, line);
    snprintf(line, sizeof(line), "VROM STATUS: %d", r.vrom_status);
    draw_text(40, 352, line);
    snprintf(line, sizeof(line), "MESHES: %u VERTICES: %u CACHE: %u",
        static_cast<unsigned>(r.meshes), static_cast<unsigned>(r.vertices),
        static_cast<unsigned>(r.cached));
    draw_text(40, 384, line);
    snprintf(line, sizeof(line), "RAM:%u TILE:%u", static_cast<unsigned>(r.ram_bytes), static_cast<unsigned>(r.tile_bytes));
    draw_text(40, 408, line);
    snprintf(line, sizeof(line), "CULL:%u/%u POLY:%u", static_cast<unsigned>(r.cull_lo_words), static_cast<unsigned>(r.cull_hi_words), static_cast<unsigned>(r.poly_words));
    draw_text(40, 432, line);
    snprintf(line, sizeof(line), "FIFO:%u TEX:%u", static_cast<unsigned>(r.fifo_capacity), static_cast<unsigned>(r.tex_words));
    draw_text(40, 456, line);
    draw_text(40, 480, "SYNTHETIC ROM DATA - NO GAME ASSETS");
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
