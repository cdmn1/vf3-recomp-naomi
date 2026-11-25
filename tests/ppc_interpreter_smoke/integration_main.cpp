#include <kos.h>
#include <dc/minifont.h>
#include <dc/pvr.h>

#include <cstdint>
#include <stdio.h>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

#include "vf3_image.h"
#include "cart_vrom_bridge.h"
#include "cart_vrom.h"
#include "cart_vrom_bridge.h"
#include "r3d_scene.h"
#include "model3.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

#ifndef VF3_BUILD_VERSION
#define VF3_BUILD_VERSION "dev"
#endif

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

// This probe mounts either the synthetic fixture or real Revision D ROM image,
// initializes the Model3 board, and exercises the production storage/render paths.


// v228 isolates real VF3 geometry from all source material/texture state.
// v230: capture source-space, model-space and projected coordinates for each isolated real mesh.
// v231 compact diagnostics alternate transform and projection pages to avoid clipping.
static constexpr bool kForceSolidWhiteDiagnostic = true;

struct RenderTexture {
    const video::Mesh *mesh = nullptr;
    pvr_ptr_t texture = nullptr;
    uint16_t width = 0, height = 0;
};

static const uint32_t expected_vrom_edge[8] = {
    0xE0010001u, 0xE0020002u, 0xE0030003u, 0x00000040u,
    0xD0010001u, 0xD0020002u, 0xD0030003u, 0xD0040004u
};

static const uint32_t expected_model[38] = {
    // Polygon 1: white triangle, smooth normals and UVs; not terminal.
    0x00000000u, 0x0000000Au, 0x00000000u, 0x00000000u,
    0xFFFFFFFFu, 0x00000000u, 0x00800400u,
    0x0000007Fu, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00010000u, 0x0000007Fu, 0x00000000u, 0x01000000u,
    0x00000000u, 0x00010000u, 0x0000007Fu, 0x00000100u,
    // Polygon 2: red triangle with separate normals/UVs; terminal.
    0x00000000u, 0x0000000Eu, 0x00000000u, 0x00000000u,
    0xFF0000FFu, 0x00000000u, 0x00800400u,
    0x0000807Fu, 0x00000000u, 0x00000000u, 0x01000100u,
    0x00018000u, 0x0000007Fu, 0x00000000u, 0x02000100u,
    0x00008000u, 0x00010000u, 0x0000007Fu, 0x01000200u
};

struct Results {
    bool mount = false;
    bool board = false;
    bool board_load = false;
    bool power_ppc = false;
    bool crom_bus = false;
    bool tile_bus = false;
    bool real3d_bus = false;
    bool texture16 = false, texture8_low = false, texture8_high = false;
    bool crom = false;
    bool crom_cache = false;
    bool vrom = false;
    bool vrom_edges = false;
    bool vrom_port = false;
    bool scene = false;
    bool scene_geometry = false;
    bool scene_colour = false;
    bool scene_attributes = false;
    bool pvr_scene = false;
    bool full_scene = false;
    video::Meshes render_meshes;
    video::Scene render_scene;
    std::vector<RenderTexture> render_textures;
    uint32_t render_textures_ready = 0;
    uint32_t render_textures_failed = 0;
    uint32_t texture_alpha_zero = 0, texture_alpha_opaque = 0;
    uint32_t texture_rgb_unique = 0;
    alignas(32) uint16_t pvr_texture_pixels[32 * 32] = {};
    bool pvr_texture_source = false;
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
    bool synthetic_fixture = false;
    bool real_vrom = false;
    bool program_rom = false;
    size_t real_vrom_nonzero = 0;
    size_t program_rom_nonblank = 0;
    bool real_boot = false;
    bool real_program_copied = false;
    bool real_scene = false;
    uint32_t real_boot_frames = 0;
    uint32_t real_boot_pc = 0;
    uint32_t real_boot_pc_changes = 0;
    uint64_t real_boot_cycles = 0;
    uint32_t real_boot_r1 = 0, real_boot_r2 = 0;
    uint32_t real_boot_r3 = 0, real_boot_r4 = 0;
    uint32_t real_boot_lr = 0, real_boot_srr0 = 0, real_boot_srr1 = 0;
    uint32_t real_boot_msr = 0, real_boot_npc = 0, real_boot_dec = 0;
    uint32_t real_boot_instruction = 0, real_boot_cr0 = 0;
    int real_boot_interrupt_pending = 0;
    uint32_t real_boot_idle_samples = 0;
    uint32_t real_boot_poll_op = 0, real_boot_poll_addr = 0;
    uint32_t real_boot_poll_value = 0xFFFFFFFFu;
    uint32_t real_boot_irq_pending_or = 0, real_boot_irq_state_or = 0;
    uint32_t real_boot_srr0_changes = 0;
    size_t real_program_match_words = 0;
    size_t real_scene_viewports = 0;
    size_t real_scene_models = 0;
};

static void draw_boot_progress(uint32_t frame, uint32_t pc, uint64_t cycles);

static uint16_t decode_real3d_texel(uint16_t value, int format)
{
    unsigned r = 0, g = 0, b = 0, a = 15;
    auto grey = [&](unsigned n, unsigned alpha) { r = g = b = n; a = alpha; };
    switch (format) {
    case 0:
        r = (value >> 10) & 31; g = (value >> 5) & 31; b = value & 31;
        r = (r * 15 + 15) / 31; g = (g * 15 + 15) / 31; b = (b * 15 + 15) / 31;
        a = (value & 0x8000) ? 0 : 15; break;
    case 1: grey(value & 15, (value >> 4) & 15); break;
    case 2: grey((value >> 4) & 15, value & 15); break;
    case 3: grey((value >> 8) & 15, (value >> 12) & 15); break;
    case 4: grey((value >> 12) & 15, (value >> 8) & 15); break;
    case 5: { unsigned n = value & 255; grey((n * 15 + 127) / 255, n == 255 ? 0 : 15); break; }
    case 6: { unsigned n = (value >> 8) & 255; grey((n * 15 + 127) / 255, n == 255 ? 0 : 15); break; }
    case 7: r = (value >> 12) & 15; g = (value >> 8) & 15; b = (value >> 4) & 15; a = value & 15; break;
    case 8: case 9: case 10: case 11: {
        unsigned n = (value >> ((format - 8) * 4)) & 15;
        grey(n, n == 15 ? 0 : 15); break;
    }
    default: a = 0; break;
    }
    return static_cast<uint16_t>((a << 12) | (r << 8) | (g << 4) | b);
}

alignas(32) static uint16_t real3d_texture_upload[128 * 128];

static void prepare_real3d_textures(Results &r, const m3::Model3 &board)
{
    for (const video::Viewport &viewport : r.render_scene.viewports) {
        for (const video::Model &model : viewport.models) {
            if (!model.meshes) continue;
            for (const video::Mesh &mesh : *model.meshes) {
                if (!mesh.textured || mesh.width <= 0 || mesh.height <= 0) continue;
                const int out_w = std::min(mesh.width, 128);
                const int out_h = std::min(mesh.height, 128);
                const int page = (mesh.page ^ model.page) & 1;
                pvr_ptr_t texture = pvr_mem_malloc(out_w * out_h * 2);
                if (!texture) { ++r.render_textures_failed; continue; }
                for (int y = 0; y < out_h; ++y) {
                    const int sy = (mesh.y + (y * mesh.height) / out_h) & 1023;
                    for (int x = 0; x < out_w; ++x) {
                        const int sx = (mesh.x + (x * mesh.width) / out_w) & 2047;
                        const size_t at = size_t(page * 1024 + sy) * 2048 + sx;
                        const uint16_t raw = at < board.tex_ram.size() ? board.tex_ram[at] : 0;
                        real3d_texture_upload[y * out_w + x] = decode_real3d_texel(raw, mesh.format);
                    }
                }
                uint32_t alpha_zero = 0, alpha_opaque = 0;
                uint16_t first_rgb = real3d_texture_upload[0] & 0x0FFFu;
                bool rgb_varies = false;
                for (int i = 0; i < out_w * out_h; ++i) {
                    const uint16_t pixel = real3d_texture_upload[i];
                    const unsigned alpha = pixel >> 12;
                    if (alpha == 0) ++alpha_zero;
                    if (alpha == 15) ++alpha_opaque;
                    if ((pixel & 0x0FFFu) != first_rgb) rgb_varies = true;
                }
                r.texture_alpha_zero += alpha_zero;
                r.texture_alpha_opaque += alpha_opaque;
                r.texture_rgb_unique += rgb_varies ? 1u : 0u;
                pvr_txr_load(real3d_texture_upload, texture, out_w * out_h * 2);
                r.render_textures.push_back({&mesh, texture, static_cast<uint16_t>(out_w), static_cast<uint16_t>(out_h)});
                ++r.render_textures_ready;
            }
        }
    }
}

static pvr_ptr_t real3d_texture_for(const Results &r, const video::Mesh &mesh,
                                    uint16_t &width, uint16_t &height)
{
    for (const RenderTexture &entry : r.render_textures) {
        if (entry.mesh == &mesh) {
            width = entry.width; height = entry.height; return entry.texture;
        }
    }
    width = height = 0;
    return nullptr;
}


static Results run_integration()
{
    Results r;
    r.mount_status = static_cast<int>(naomi_model3_mount_netdimm_image());
    r.mount = r.mount_status == NAOMI_VF3_IMAGE_OK;
    if (!r.mount)
        return r;

    // Read the same VROM model address used by the synthetic fixture. On a
    // real Revision D image this is ordinary game data, not the test model.
    alignas(32) uint32_t actual_model[38] = {};
    r.vrom_status = naomi_model3_vrom_read_words(0x00100000u, actual_model, 38);
    r.synthetic_fixture = r.vrom_status == NAOMI_CART_VROM_OK &&
        std::memcmp(actual_model, expected_model, sizeof(expected_model)) == 0;
    if (r.vrom_status == NAOMI_CART_VROM_OK) {
        for (uint32_t word : actual_model)
            if (word != 0u && word != 0xFFFFFFFFu) ++r.real_vrom_nonzero;
    }
    r.real_vrom = r.vrom_status == NAOMI_CART_VROM_OK &&
        !r.synthetic_fixture && r.real_vrom_nonzero != 0;
    r.vrom = r.synthetic_fixture;

    m3::Model3 board;
    std::string error;
    r.hw_mode = static_cast<unsigned>(hardware_sys_mode(nullptr));
    r.pvr_total = static_cast<unsigned>(PVR_RAM_SIZE);
    r.pvr_before = pvr_mem_available();
    r.board_load = board.load("", error);
    const uint8_t *fixed_program = naomi_model3_crom_fixed();
    if (fixed_program && !r.synthetic_fixture) {
        for (uint32_t i = 0; i < 0x0A0000u; ++i) {
            const uint8_t byte = fixed_program[0x710000u + i];
            if (byte != 0u && byte != 0xFFu) ++r.program_rom_nonblank;
        }
        r.program_rom = r.program_rom_nonblank > 0x1000u;
    }
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

    if (r.board_load && r.synthetic_fixture) {
        // Synthetic-only PPC, Real3D bus, texture and scene fixtures.
        // Real Revision D must execute its own reset ROM instead.
        // Capture actual ROM words before exercising the PPC bus. The real-ROM
        // image uses these as reference values; the synthetic fixture uses its
        // explicit bus markers below.
        const uint8_t *fixed_rom = naomi_model3_crom_fixed();
        const uint32_t fixed_bus_expected = fixed_rom ?
            *reinterpret_cast<const uint32_t *>(fixed_rom) : 0xFFFFFFFFu;
        naomi_model3_crom_select_bank(0xFCu);
        const uint32_t banked_bus_expected = naomi_model3_crom_read32(0);

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
                     (r.synthetic_fixture ?
                        (board.cpu.s.r[7] == 0x13579BDFu &&
                         board.cpu.s.r[11] == 0x2468ACE0u) :
                        (board.cpu.s.r[7] == fixed_bus_expected &&
                         board.cpu.s.r[11] == banked_bus_expected)) &&
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
        // copy PPC RAM words into tile RAM, low/high culling RAM and polygon RAM.
        // Enable completion IRQ/status reporting and acknowledge it after the transfers.
        const uint32_t dma_source_word = 0xA1B2C3D4u;
        const uint32_t cull_source_word = 0x11223344u;
        const uint32_t poly_source_word = 0x55667788u;
        const uint32_t cull_hi_source_word = 0x99AABBCCu;
        *reinterpret_cast<uint32_t *>(board.ram.data() + 0x100) = dma_source_word;
        *reinterpret_cast<uint32_t *>(board.ram.data() + 0x104) = cull_source_word;
        *reinterpret_cast<uint32_t *>(board.ram.data() + 0x108) = poly_source_word;
        *reinterpret_cast<uint32_t *>(board.ram.data() + 0x10C) = cull_hi_source_word;

        // A single 32x32 16-bit texture record: length, header, then 512 data words.
        // Use a distinct value for every source texel so the test observes the
        // upstream 8x8 tile-order decoder instead of passing on a solid fill.
        // Place it at (32, 1056): non-zero X/Y plus texture page 1 (Y + 1024).
        // The DMA/Real3D bus byte swaps are accounted for when packing each word.
        uint32_t *texture_record = reinterpret_cast<uint32_t *>(board.ram.data() + 0x200);
        uint16_t texture_pattern[1024];
        for (unsigned i = 0; i < 1024; ++i)
            texture_pattern[i] = static_cast<uint16_t>(0x1000u + i);
        texture_record[0] = 0xFC0F0000u; // bswap(4092): 2046 16-bit units after the two-word prefix
        texture_record[1] = 0x81009001u; // bswap(0x01900081): X=32, Y=32, page 1, 32x32, no mipmaps
        for (unsigned i = 0; i < 512; ++i) {
            const uint32_t fifo_word =
                (uint32_t(texture_pattern[i * 2 + 1]) << 16) | texture_pattern[i * 2];
            // write32(0x94) byte-swaps each DMA word before storing it in
            // tex_fifo. Pre-swap the source so the FIFO's uint16_t stream is
            // exactly texture_pattern[0..1023] on the little-endian target.
            texture_record[i + 2] = __builtin_bswap32(fifo_word);
        }

        // Two more independent texture records exercise the 8-bit low-byte
        // and high-byte upload modes. Each is flushed separately because the
        // upstream FIFO parser's record-length stepping is validated one
        // record at a time here.
        uint32_t *texture8_low = reinterpret_cast<uint32_t *>(board.ram.data() + 0xA20);
        uint32_t *texture8_high = reinterpret_cast<uint32_t *>(board.ram.data() + 0xE40);
        texture8_low[0] = 0xFC070000u;  // bswap(0x000007FC): 32x32 8-bit payload
        texture8_low[1] = 0x02012001u;  // bswap(0x01200102): type 1, LSB/low byte, X=64 Y=64
        texture8_high[0] = 0xFC070000u;
        texture8_high[1] = 0x02014001u; // bswap(0x01400102): type 1, MSB/high byte, X=96 Y=64
        // Give every 8-bit source texel a deterministic value. This checks
        // the 8x8 tile permutation and the alternating source-byte selection,
        // not merely that a solid fill reaches the destination.
        uint16_t texture8_pattern[512];
        for (unsigned i = 0; i < 512; ++i) {
            const uint16_t hi = static_cast<uint16_t>((i * 13u + 0x40u) & 0xFFu);
            const uint16_t lo = static_cast<uint16_t>((i * 29u + 0x10u) & 0xFFu);
            texture8_pattern[i] = static_cast<uint16_t>((hi << 8) | lo);
        }
        for (unsigned i = 0; i < 256; ++i) {
            const uint32_t fifo_word =
                (uint32_t(texture8_pattern[i * 2 + 1]) << 16) |
                texture8_pattern[i * 2];
            texture8_low[i + 2] = __builtin_bswap32(fifo_word);
            texture8_high[i + 2] = __builtin_bswap32(fifo_word);
        }
        for (unsigned y = 0; y < 32; ++y) {
            for (unsigned x = 0; x < 32; ++x) {
                board.tex_ram[(64u + y) * 2048u + 64u + x] = 0xA500u;
                board.tex_ram[(64u + y) * 2048u + 96u + x] = 0x00A5u;
            }
        }
        program[0] = 0x3DE08400u; // lis r15, 0x8400 (Real3D register window)
        program[1] = 0x820F0000u; // lwz r16, 0(r15)
        program[2] = 0x3E20C200u; // lis r17, 0xC200 (Real3D DMA registers)
        program[3] = 0x3AE00001u; // li r23, 1 (enable DMA completion status/IRQ)
        program[4] = 0x9AF1000Eu; // stb r23, 14(r17) (DMA config)
        program[5] = 0x3E400001u; // lis r18, 1 (bswap(0x100), DMA source)
        program[6] = 0x92510000u; // stw r18, 0(r17)
        program[7] = 0x3E600400u; // lis r19, 0x0400
        program[8] = 0x627300F1u; // ori r19, r19, 0x00F1 (bswap(0xF1000004))
        program[9] = 0x92710004u; // stw r19, 4(r17)
        program[10] = 0x3E800100u; // lis r20, 0x0100 (bswap(1), one DMA word)
        program[11] = 0x92910008u; // stw r20, 8(r17) (start tile RAM DMA)
        program[12] = 0x3EA0F100u; // lis r21, 0xF100
        program[13] = 0x82D50004u; // lwz r22, 4(r21)
        program[14] = 0x3E400401u; // lis r18, 0x0401 (bswap(0x104), low culling source)
        program[15] = 0x92510000u; // stw r18, 0(r17)
        program[16] = 0x3A60008Cu; // addi r19, r0, 0x008C (bswap(0x8C000000))
        program[17] = 0x92710004u; // stw r19, 4(r17) (low culling RAM)
        program[18] = 0x92910008u; // stw r20, 8(r17) (start low culling DMA)
        program[19] = 0x3E400801u; // lis r18, 0x0801 (bswap(0x108), polygon source)
        program[20] = 0x92510000u; // stw r18, 0(r17)
        program[21] = 0x3A600098u; // addi r19, r0, 0x0098 (bswap(0x98000000))
        program[22] = 0x92710004u; // stw r19, 4(r17) (polygon RAM)
        program[23] = 0x92910008u; // stw r20, 8(r17) (start polygon DMA)
        program[24] = 0x3E400C01u; // lis r18, 0x0C01 (bswap(0x10C), high culling source)
        program[25] = 0x92510000u; // stw r18, 0(r17)
        program[26] = 0x3A60008Eu; // addi r19, r0, 0x008E (bswap(0x8E000000))
        program[27] = 0x92710004u; // stw r19, 4(r17) (high culling RAM)
        program[28] = 0x92910008u; // stw r20, 8(r17) (start high culling DMA)
        program[29] = 0x8B11000Cu; // lbz r24, 12(r17) (read DMA completion status)
        program[30] = 0x3B200001u; // li r25, 1 (DMA status acknowledge)
        program[31] = 0x9B31000Du; // stb r25, 13(r17) (clear status and completion IRQ)
        // DMA the synthetic texture record into the FIFO, acknowledge its completion,
        // then issue the Real3D flush register to decode it into texture RAM.
        program[32] = 0x3E400002u; // lis r18, 2 (bswap(0x200), texture-record source)
        program[33] = 0x92510000u; // stw r18, 0(r17)
        program[34] = 0x3A600094u; // addi r19, r0, 0x0094 (texture FIFO)
        program[35] = 0x92710004u; // stw r19, 4(r17)
        program[36] = 0x3E800202u; // lis r20, 0x0202 (bswap(514), transfer 514 words)
        program[37] = 0x92910008u; // stw r20, 8(r17) (start texture FIFO DMA)
        program[38] = 0x8B11000Cu; // lbz r24, 12(r17) (read DMA completion status)
        program[39] = 0x9B31000Du; // stb r25, 13(r17) (acknowledge completion IRQ)
        program[40] = 0x3F408800u; // lis r26, 0x8800 (Real3D flush register)
        program[41] = 0x937A0000u; // stw r27, 0(r26) (flush texture FIFO)
        board.cpu.s.pc = 0;
        board.cpu.s.npc = 0;
        const int real3d_executed = board.cpu.execute(42);
        r.real3d_bus = real3d_executed == 42 &&
                       board.cpu.s.r[16] == 0xFFFFFFFDu &&
                       board.cpu.s.r[22] == dma_source_word &&
                       board.cpu.s.r[24] == 1u &&
                       board.cull_lo[0] == 0x44332211u &&
                       board.cull_hi[0] == 0xCCBBAA99u &&
                       board.poly_ram[0] == 0x88776655u &&
                       board.dma_status == 0 &&
                       (board.irq.state & 0x100u) == 0 &&
                       !board.cpu.s.fatal;
        r.texture16 = board.tex_fifo.empty() &&
                      // 8x8 tiled input order: decode8x8[0]=1, [1]=0,
                      // [4]=9, [8]=3, [63]=62. Check adjacent texels,
                      // tile boundaries, and the final texel of the 32x32 image.
                      board.tex_ram[1056u * 2048u + 32u] == texture_pattern[1] &&
                      board.tex_ram[1056u * 2048u + 33u] == texture_pattern[0] &&
                      board.tex_ram[1056u * 2048u + 36u] == texture_pattern[9] &&
                      board.tex_ram[1057u * 2048u + 32u] == texture_pattern[3] &&
                      board.tex_ram[1056u * 2048u + 40u] == texture_pattern[65] &&
                      board.tex_ram[1064u * 2048u + 32u] == texture_pattern[257] &&
                      board.tex_ram[1087u * 2048u + 63u] == texture_pattern[1022] &&
                      board.tex_ram[32u * 2048u + 32u] == 0 &&
                      board.tex_ram[0] == 0;

        // Preserve a real decoded 32x32 Model3 texture page for the PVR2 path.
        // The texture remains in Model3's decoded 16-bit format; this first
        // upload probe deliberately uses RGB565 to isolate transfer/UV/raster.
        if (r.texture16) {
            for (unsigned y = 0; y < 32; ++y)
                for (unsigned x = 0; x < 32; ++x)
                    r.pvr_texture_pixels[y * 32u + x] =
                        board.tex_ram[(1056u + y) * 2048u + 32u + x];
            r.pvr_texture_source = true;
        }

        // Exercise each 8-bit lane independently so the on-screen result
        // identifies which upload mode or byte-preservation check fails.
        program[0] = 0x3E40200Au; // lis r18, 0x200A (bswap(0xA20), low-byte record)
        program[1] = 0x92510000u; // stw r18, 0(r17)
        program[2] = 0x3A600094u; // addi r19, r0, 0x0094 (texture FIFO)
        program[3] = 0x92710004u; // stw r19, 4(r17)
        program[4] = 0x3E800201u; // lis r20, 0x0201 (bswap(258), 258 DMA words)
        program[5] = 0x92910008u; // stw r20, 8(r17)
        program[6] = 0x8B11000Cu; // lbz r24, 12(r17)
        program[7] = 0x9B31000Du; // stb r25, 13(r17)
        program[8] = 0x3F408800u; // lis r26, 0x8800 (Real3D flush register)
        program[9] = 0x937A0000u; // stw r27, 0(r26) (flush low-byte upload)
        board.cpu.s.pc = 0;
        board.cpu.s.npc = 0;
        const int texture8_low_executed = board.cpu.execute(10);
        r.texture8_low = texture8_low_executed == 10 &&
                         board.cpu.s.r[24] == 1u &&
                         board.tex_fifo.empty() &&
                         board.tex_ram[64u * 2048u + 64u] == 0xA54Du &&
                         board.tex_ram[64u * 2048u + 65u] == 0xA52Du &&
                         board.tex_ram[65u * 2048u + 64u] == 0xA540u &&
                         board.tex_ram[71u * 2048u + 71u] == 0xA576u &&
                         board.tex_ram[64u * 2048u + 72u] == 0xA5EDu &&
                         board.tex_ram[64u * 2048u + 95u] == 0xA5BBu &&
                         board.tex_ram[72u * 2048u + 64u] == 0xA5CDu &&
                         board.tex_ram[95u * 2048u + 95u] == 0xA5D6u &&
                         board.tex_ram[80u * 2048u + 79u] == 0xA57Bu &&
                         board.tex_ram[79u * 2048u + 80u] == 0xA538u &&
                         board.tex_ram[88u * 2048u + 88u] == 0xA5ADu &&
                         board.tex_ram[64u * 2048u + 96u] == 0x00A5u &&
                         !board.cpu.s.fatal;

        // X=3 (96 pixels), Y=2 (64 pixels), MSB/high-byte lane.
        texture8_high[1] = 0x03014001u; // bswap(0x01400103)
        program[0] = 0x3E40400Eu; // lis r18, 0x400E (bswap(0xE40), high-byte record)
        program[1] = 0x92510000u; // stw r18, 0(r17)
        program[2] = 0x3A600094u; // addi r19, r0, 0x0094
        program[3] = 0x92710004u; // stw r19, 4(r17)
        program[4] = 0x92910008u; // stw r20, 8(r17)
        program[5] = 0x8B11000Cu; // lbz r24, 12(r17)
        program[6] = 0x9B31000Du; // stb r25, 13(r17)
        program[7] = 0x937A0000u; // stw r27, 0(r26) (flush high-byte upload)
        board.cpu.s.pc = 0;
        board.cpu.s.npc = 0;
        const int texture8_high_executed = board.cpu.execute(8);
        r.texture8_high = texture8_high_executed == 8 &&
                          board.cpu.s.r[24] == 1u &&
                          board.tex_fifo.empty() &&
                          board.tex_ram[64u * 2048u + 96u] == 0x4DA5u &&
                          board.tex_ram[64u * 2048u + 97u] == 0x2DA5u &&
                          board.tex_ram[65u * 2048u + 96u] == 0x40A5u &&
                          board.tex_ram[71u * 2048u + 103u] == 0x76A5u &&
                          board.tex_ram[64u * 2048u + 104u] == 0xEDA5u &&
                          board.tex_ram[64u * 2048u + 127u] == 0xBBA5u &&
                          board.tex_ram[72u * 2048u + 96u] == 0xCDA5u &&
                          board.tex_ram[95u * 2048u + 127u] == 0xD6A5u &&
                          board.tex_ram[80u * 2048u + 111u] == 0x7BA5u &&
                          board.tex_ram[79u * 2048u + 112u] == 0x38A5u &&
                          board.tex_ram[88u * 2048u + 120u] == 0xADA5u &&
                          board.tex_ram[64u * 2048u + 95u] == 0xA5BBu &&
                          !board.cpu.s.fatal;
    }

    if (r.board_load && !r.synthetic_fixture) {
        // Start the real Model 3 PPC at its hardware reset vector. The boot ROM
        // is expected to copy the Revision D game program (CROM 0x710000,
        // 0xA0000 bytes) into PPC RAM before transferring control to it.
        board.power_on();
        draw_boot_progress(0, board.cpu.s.pc, 0);
        uint32_t previous_pc = board.cpu.s.pc;
        uint32_t previous_srr0 = board.cpu.s.srr0;
        // Match the upstream Revision D profile's 960-frame boot window.
        // Sample the documented interrupt-driven idle poll and interrupt state.
        for (uint32_t frame = 0; frame < 960 && !board.cpu.s.fatal; ++frame) {
            board.run_frame();
            ++r.real_boot_frames;
            r.real_boot_irq_pending_or |= static_cast<uint32_t>(board.cpu.s.interrupt_pending);
            r.real_boot_irq_state_or |= board.irq.state;
            if (board.cpu.s.srr0 != previous_srr0) {
                ++r.real_boot_srr0_changes;
                previous_srr0 = board.cpu.s.srr0;
            }
            const uint32_t sampled_pc = board.cpu.s.pc;
            if (sampled_pc >= 0x1E9FCu && sampled_pc <= 0x1EA04u)
                ++r.real_boot_idle_samples;
            const uint32_t poll_pc = sampled_pc == 0x1E9FCu ? sampled_pc :
                (board.cpu.s.npc == 0x1E9FCu ? board.cpu.s.npc : 0u);
            if (poll_pc) {
                const uint32_t op = reinterpret_cast<const uint32_t *>(board.ram.data())[poll_pc / 4u];
                const uint32_t ra = (op >> 16) & 31u;
                const int32_t displacement = static_cast<int16_t>(op & 0xFFFFu);
                const uint32_t base = ra ? board.cpu.s.r[ra] : 0u;
                r.real_boot_poll_op = op;
                r.real_boot_poll_addr = base + static_cast<uint32_t>(displacement);
                r.real_boot_poll_value = board.read8(r.real_boot_poll_addr);
            }
            if ((frame + 1u) % 25u == 0u)
                draw_boot_progress(frame + 1u, board.cpu.s.pc, board.cpu.s.total_cycles);
            if (board.cpu.s.pc != previous_pc) {
                ++r.real_boot_pc_changes;
                previous_pc = board.cpu.s.pc;
            }
        }
        r.real_boot_pc = board.cpu.s.pc;
        r.real_boot_cycles = board.cpu.s.total_cycles;
        r.real_boot_r1 = board.cpu.s.r[1];
        r.real_boot_r2 = board.cpu.s.r[2];
        r.real_boot_r3 = board.cpu.s.r[3];
        r.real_boot_r4 = board.cpu.s.r[4];
        r.real_boot_lr = board.cpu.s.lr;
        r.real_boot_srr0 = board.cpu.s.srr0;
        r.real_boot_srr1 = board.cpu.s.srr1;
        r.real_boot_msr = board.cpu.s.msr;
        r.real_boot_npc = board.cpu.s.npc;
        r.real_boot_dec = board.cpu.s.dec;
        r.real_boot_interrupt_pending = board.cpu.s.interrupt_pending;
        r.real_boot_cr0 = board.cpu.s.cr[0];
        if (r.real_boot_pc < board.ram.size())
            r.real_boot_instruction = reinterpret_cast<const uint32_t *>(board.ram.data())[r.real_boot_pc / 4];
        r.real_boot = r.real_boot_cycles != 0 &&
            r.real_boot_pc != 0xFFF00100u && !board.cpu.s.fatal;
        r.crom_bus = r.real_boot;

        const std::vector<uint32_t> program_image = board.program_image();
        const size_t program_words = program_image.size();
        const uint32_t *ram_words = reinterpret_cast<const uint32_t *>(board.ram.data());
        while (r.real_program_match_words < program_words &&
               ram_words[r.real_program_match_words] == program_image[r.real_program_match_words])
            ++r.real_program_match_words;
        r.real_program_copied = r.real_program_match_words == program_words;

        // Ask the production SceneBuilder to parse whatever the real game has
        // produced so far in culling/polygon RAM. Empty output is a valid
        // early-boot result; the counters show how far the game has progressed.
        video::SceneBuilder builder;
        builder.build(board, r.render_scene);
        r.real_scene_viewports = r.render_scene.viewports.size();
        for (const video::Viewport &viewport : r.render_scene.viewports) {
            r.real_scene_models += viewport.models.size();
            for (const video::Model &model : viewport.models) {
                if (!model.meshes) continue;
                for (const video::Mesh &mesh : *model.meshes)
                    r.vertices += mesh.verts.size();
            }
        }
        r.meshes = r.real_scene_models;
        r.real_scene = r.real_scene_viewports != 0 &&
            r.real_scene_models != 0 && r.vertices != 0;
        r.pvr_scene = r.real_scene;
        if (r.real_scene) prepare_real3d_textures(r, board);
    }

    const uint8_t *fixed = naomi_model3_crom_fixed();
    const int bank_selected = naomi_model3_crom_select_bank(0xFCu); // bank 3
    if (r.synthetic_fixture) {
        r.crom = fixed &&
                 fixed[0] == 0xDFu && fixed[1] == 0x9Bu &&
                 fixed[2] == 0x57u && fixed[3] == 0x13u &&
                 bank_selected &&
                 naomi_model3_crom_read8(0) == 0xE0u &&
                 naomi_model3_crom_read8(1) == 0xACu &&
                 naomi_model3_crom_read8(2) == 0x68u &&
                 naomi_model3_crom_read8(3) == 0x24u &&
                 naomi_model3_crom_last_status() == NAOMI_CART_IMAGE_OK;
    } else {
        const uint8_t first = naomi_model3_crom_read8(0);
        r.crom = fixed && bank_selected &&
                 naomi_model3_crom_last_status() == NAOMI_CART_IMAGE_OK &&
                 first == naomi_model3_crom_read8(0);
    }

    // Exercise all four bank-page cache slots, force page-zero eviction,
    // then verify the page is fetched again with the original marker intact.
    // Also switch banks and verify the active-low register's mirrored bank
    // 11 aliases physical bank 3 after the bank cache is invalidated.
    if (r.crom) {
        if (r.synthetic_fixture) {
            bool pages_ok = true;
            for (uint32_t page = 1; page <= 4; ++page)
                pages_ok = pages_ok &&
                    naomi_model3_crom_read8(page * NAOMI_CART_CROM_PAGE_SIZE) == 0u;
            const bool page_zero_refetched =
                naomi_model3_crom_read8(0) == 0xE0u;
            const bool bank_four_selected =
                naomi_model3_crom_select_bank(0xFBu) &&
                naomi_model3_crom_read8(0) == 0u;
            const bool mirrored_bank_selected =
                naomi_model3_crom_select_bank(0xF4u) &&
                naomi_model3_crom_read8(0) == 0xE0u;
            r.crom_cache = pages_ok && page_zero_refetched &&
                bank_four_selected && mirrored_bank_selected &&
                naomi_model3_crom_last_status() == NAOMI_CART_IMAGE_OK;
        } else {
            const uint8_t page_zero = naomi_model3_crom_read8(0);
            bool pages_ok = naomi_model3_crom_last_status() == NAOMI_CART_IMAGE_OK;
            for (uint32_t page = 1; page <= 4; ++page) {
                (void)naomi_model3_crom_read8(page * NAOMI_CART_CROM_PAGE_SIZE);
                pages_ok = pages_ok &&
                    naomi_model3_crom_last_status() == NAOMI_CART_IMAGE_OK;
            }
            const bool page_zero_refetched = naomi_model3_crom_read8(0) == page_zero;
            const bool bank_four_selected = naomi_model3_crom_select_bank(0xFBu);
            (void)naomi_model3_crom_read8(0);
            const bool bank_four_read = naomi_model3_crom_last_status() == NAOMI_CART_IMAGE_OK;
            const bool mirrored_bank_selected = naomi_model3_crom_select_bank(0xF4u) &&
                naomi_model3_crom_read8(0) == page_zero;
            r.crom_cache = pages_ok && page_zero_refetched && bank_four_selected &&
                bank_four_read && mirrored_bank_selected &&
                naomi_model3_crom_last_status() == NAOMI_CART_IMAGE_OK;
        }
    }

    // Synthetic-only VROM boundary and texture-port assertions are skipped for
    // real VF3 data; the mount already CRC-verifies the complete 32 MiB VROM.

    // Cross the 64 KiB scratch-page boundary with non-zero markers, then
    // verify the 32 MiB mirror and 24-bit word-address wrap against the model.
    alignas(32) uint32_t edge_words[8] = {};
    alignas(32) uint32_t mirror_words[38] = {};
    alignas(32) uint32_t wrapped_words[38] = {};
    if (r.synthetic_fixture) {
        const int edge_status = naomi_model3_vrom_read_words(0x00003FFCu, edge_words, 8);
        const int mirror_status = naomi_model3_vrom_read_words(0x00900000u, mirror_words, 38);
        const int wrap_status = naomi_model3_vrom_read_words(0x01100000u, wrapped_words, 38);
        r.vrom_edges = edge_status == NAOMI_CART_VROM_OK &&
            std::memcmp(edge_words, expected_vrom_edge, sizeof(expected_vrom_edge)) == 0 &&
            mirror_status == NAOMI_CART_VROM_OK &&
            std::memcmp(mirror_words, expected_model, sizeof(expected_model)) == 0 &&
            wrap_status == NAOMI_CART_VROM_OK &&
            std::memcmp(wrapped_words, expected_model, sizeof(expected_model)) == 0;
    }

    // Exercise the production texture-port bridge: the length header is the
    // final word of one 64 KiB scratch page and its payload crosses into the next.
    std::vector<uint32_t> texture_port_fifo;
    if (r.synthetic_fixture) {
        const int texture_port_called = naomi_model3_vrom_port(0x00003FFFu, &texture_port_fifo);
        const uint32_t expected_port[8] = {
            0x00000040u, 0xD0010001u, 0xD0020002u, 0xD0030003u,
            0xD0040004u, 0x00000000u, 0x00000000u, 0x00000000u
        };
        r.vrom_port = texture_port_called &&
            naomi_model3_vrom_last_status() == NAOMI_CART_VROM_OK &&
            texture_port_fifo.size() == 8u &&
            std::memcmp(texture_port_fifo.data(), expected_port, sizeof(expected_port)) == 0;
    }

    if (r.vrom && board.poly_ram.size() == 0x400000u / 4u) {
        video::SceneBuilder builder;
        const bool first = builder.naomi_test_vrom_model(
            0x00100000u, board.poly_ram.data(), &r.render_meshes, &r.meshes, &r.vertices,
            &r.scene_geometry, &r.scene_colour, &r.scene_attributes);
        const size_t cache_after_first = builder.cached();
        size_t second_meshes = 0, second_vertices = 0;
        const bool second = builder.naomi_test_vrom_model(
            0x00100000u, board.poly_ram.data(), nullptr, &second_meshes, &second_vertices,
            nullptr, nullptr, nullptr);
        r.cached = builder.cached();
        r.scene = first && second && r.meshes == 2 && r.vertices == 6 &&
                  second_meshes == r.meshes && second_vertices == r.vertices &&
                  cache_after_first == 1 && r.cached == 1 &&
                  r.scene_geometry && r.scene_colour && r.scene_attributes;
        r.pvr_scene = r.render_meshes && r.render_meshes->size() == 2 &&
            (*r.render_meshes)[0].verts.size() == 3 &&
            (*r.render_meshes)[1].verts.size() == 3;

        // Build a complete synthetic Real3D viewport/culling tree in the same
        // board memories consumed by SceneBuilder::build(). The base matrix
        // cancels the Real3D-to-view axis swap and places the model 8 units
        // in front of the camera. This exercises viewport, matrix, node,
        // model lookup, mesh parsing and the production projection data path.
        uint32_t *cull = board.cull_hi.data();
        uint32_t *viewport = cull;
        viewport[0] = 0;
        viewport[1] = 0x01000000u; // no next viewport
        viewport[2] = 0x00800300u; // first culling node
        viewport[3] = 0x3F800000u; // texture LOD scale
        viewport[8] = 0x3F000000u; // cv = 0.5
        viewport[9] = 0x3F000000u; // cw = 0.5
        viewport[10] = 0x3F000000u; // io = 0.5
        viewport[11] = 0x3F000000u; // jo = 0.5
        viewport[0x14] = (1536u << 16) | 1984u; // 496x384 viewport, 14.2
        viewport[0x16] = 0x00800100u; // matrix table
        viewport[0x17] = 0x00800200u; // LOD table
        viewport[0x1A] = 0; // origin
        float *matrix = reinterpret_cast<float *>(cull + 0x100);
        matrix[0] = 8.0f; matrix[1] = 0.0f; matrix[2] = 0.0f;
        matrix[3] = 0.0f; matrix[4] = 0.0f; matrix[5] = -1.0f;
        matrix[6] = 1.0f; matrix[7] = 0.0f; matrix[8] = 0.0f;
        matrix[9] = 0.0f; matrix[10] = -1.0f; matrix[11] = 0.0f;
        float *lod = reinterpret_cast<float *>(cull + 0x200);
        lod[6] = 0.0f;
        lod[7] = 8.0f;
        uint32_t *node = cull + 0x300;
        node[0] = 1u;
        node[1] = 0u;
        node[5] = 0x01100000u; // VROM model at word address 0x00100000
        node[6] = 0u;          // no sibling
        node[7] = 0x3C003C00u; // culling and blend radii = 1.0

        video::SceneBuilder full_builder;
        full_builder.build(board, r.render_scene);
        r.full_scene = r.render_scene.viewports.size() == 1 &&
            r.render_scene.viewports[0].models.size() == 1 &&
            r.render_scene.viewports[0].models[0].meshes &&
            r.render_scene.viewports[0].models[0].meshes->size() == 2;
        if (r.full_scene) {
            const video::Model &model = r.render_scene.viewports[0].models[0];
            r.pvr_scene = model.meshes &&
                (*model.meshes)[0].verts.size() == 3 &&
                (*model.meshes)[1].verts.size() == 3 &&
                r.render_scene.viewports[0].vp_w == 496.0f &&
                r.render_scene.viewports[0].vp_h == 384.0f &&
                model.matrix.m[14] < 0.0f;
        }
    }
    return r;
}

#define FONT_W 12
#define FONT_H 18
#define FONT_SOURCE_H 24
static pvr_ptr_t font_texture;
static pvr_ptr_t scene_texture;
static bool scene_texture_ready;

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
    const float u2 = (ix + FONT_W) / 256.0f, v2 = (iy + FONT_SOURCE_H) / 256.0f;
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

static void draw_boot_progress(uint32_t frame, uint32_t pc, uint64_t cycles)
{
    pvr_wait_ready();
    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_OP_POLY);
    pvr_list_begin(PVR_LIST_TR_POLY);
    char line[64];
    draw_text(40, 48, "VF3 REV D - PPC BOOT IN PROGRESS");
    snprintf(line, sizeof(line), "FRAME: %u / 960", frame);
    draw_text(40, 84, line);
    snprintf(line, sizeof(line), "PPC PC: %08X", pc);
    draw_text(40, 108, line);
    snprintf(line, sizeof(line), "PPC CYC: %08X", static_cast<unsigned>(cycles));
    draw_text(40, 132, line);
    draw_text(40, 180, "PLEASE WAIT - RUNNING REAL GAME CODE");
    pvr_list_finish();
    pvr_scene_finish();
}

static void draw_scene_texture()
{
    if (!scene_texture_ready) return;
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t header;
    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY,
        PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED,
        32, 32, scene_texture, PVR_FILTER_NONE);
    cxt.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&header, &cxt);
    pvr_prim(&header, sizeof(header));
    pvr_vertex_t v{};
    v.flags = PVR_CMD_VERTEX; v.x = 500.0f; v.y = 250.0f;
    v.z = 1.0f; v.u = 0.0f; v.v = 0.0f; v.argb = 0xFFFFFFFFu;
    pvr_prim(&v, sizeof(v));
    v.flags = PVR_CMD_VERTEX; v.x = 500.0f; v.y = 378.0f;
    v.u = 0.0f; v.v = 1.0f;
    pvr_prim(&v, sizeof(v));
    v.flags = PVR_CMD_VERTEX; v.x = 628.0f; v.y = 250.0f;
    v.u = 1.0f; v.v = 0.0f;
    pvr_prim(&v, sizeof(v));
    v.flags = PVR_CMD_VERTEX_EOL; v.x = 628.0f; v.y = 378.0f;
    v.u = 1.0f; v.v = 1.0f;
    pvr_prim(&v, sizeof(v));
}

struct PvrRenderStats {
    uint32_t mesh_vertices = 0;
    uint32_t mesh_triangles = 0;
    uint32_t triangles_submitted = 0;
    uint32_t triangles_rejected = 0;
    uint32_t vertices_submitted = 0;
    float min_x = 1.0e9f, min_y = 1.0e9f;
    float max_x = -1.0e9f, max_y = -1.0e9f;
    uint32_t vertices_offscreen = 0;
    float min_u = 1.0e9f, min_v = 1.0e9f;
    float max_u = -1.0e9f, max_v = -1.0e9f;
    float min_depth = 1.0e9f, max_depth = -1.0e9f;
    float source_min[3] = {1.0e9f, 1.0e9f, 1.0e9f};
    float source_max[3] = {-1.0e9f, -1.0e9f, -1.0e9f};
    float world_min[3] = {1.0e9f, 1.0e9f, 1.0e9f};
    float world_max[3] = {-1.0e9f, -1.0e9f, -1.0e9f};
    float first_screen_x[3] = {};
    float first_screen_y[3] = {};
    uint32_t first_screen_count = 0;
    float vp_x = 0.0f, vp_y = 0.0f, vp_w = 0.0f, vp_h = 0.0f;
    float fr_left = 0.0f, fr_right = 0.0f, fr_top = 0.0f, fr_bottom = 0.0f;
    float matrix_rows[12] = {};
    // v236: upstream Model 3 near/guard-band clipping diagnostics for the first triangle.
    float first_camera[9] = {};
    float clip_min[5] = {1.0e9f, 1.0e9f, 1.0e9f, 1.0e9f, 1.0e9f};
    uint32_t clip_inside[5] = {};
    float upstream_screen_x[3] = {}, upstream_screen_y[3] = {};
    uint32_t upstream_screen_count = 0;
    // v236: true sequential upstream near/guard-band clipping of the first triangle.
    uint32_t clipped_vertex_count = 0;
    float clipped_min_x = 1.0e9f, clipped_min_y = 1.0e9f;
    float clipped_max_x = -1.0e9f, clipped_max_y = -1.0e9f;
    float max_projection_delta = 0.0f;
};

static PvrRenderStats draw_scene_builder_output(const Results &r, size_t selected_mesh)
{
    PvrRenderStats stats{};
    size_t mesh_index = 0;
    for (const video::Viewport &viewport : r.render_scene.viewports) {
        for (const video::Model &model : viewport.models) {
            if (!model.meshes) continue;
            for (const video::Mesh &mesh : *model.meshes) {
                const size_t this_mesh = mesh_index++;
                if (this_mesh != selected_mesh) continue;
                pvr_poly_cxt_t cxt;
                pvr_poly_hdr_t header;
                uint16_t texture_width = 0, texture_height = 0;
                const pvr_ptr_t real_texture = mesh.textured ?
                    real3d_texture_for(r, mesh, texture_width, texture_height) : nullptr;
                stats.mesh_vertices = static_cast<uint32_t>(mesh.verts.size());
                stats.mesh_triangles = static_cast<uint32_t>(mesh.verts.size() / 3u);
                stats.vp_x = viewport.vp_x; stats.vp_y = viewport.vp_y;
                stats.vp_w = viewport.vp_w; stats.vp_h = viewport.vp_h;
                stats.fr_left = viewport.left; stats.fr_right = viewport.right;
                stats.fr_top = viewport.top; stats.fr_bottom = viewport.bottom;
                for (size_t row = 0; row < 3; ++row) {
                    stats.matrix_rows[row * 4 + 0] = model.matrix.m[row];
                    stats.matrix_rows[row * 4 + 1] = model.matrix.m[4 + row];
                    stats.matrix_rows[row * 4 + 2] = model.matrix.m[8 + row];
                    stats.matrix_rows[row * 4 + 3] = model.matrix.m[12 + row];
                }
                const bool textured = !kForceSolidWhiteDiagnostic && real_texture != nullptr;
                if (textured) {
                    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY,
                        PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED,
                        texture_width, texture_height, real_texture, PVR_FILTER_BILINEAR);
                } else {
                    pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);
                }
                cxt.gen.culling = PVR_CULLING_NONE;
                pvr_poly_compile(&header, &cxt);
                pvr_prim(&header, sizeof(header));

                // Clip each camera-space triangle against the same five planes as
                // upstream Raster::prepare_mesh(), then triangulate the resulting polygon.
                struct ClipVertex {
                    float p[3];
                    float u, v;
                    float a, r, g, b;
                };
                constexpr float near_plane = 1.0e-3f;
                const float gl = viewport.left - 0.5f * (viewport.right - viewport.left);
                const float gr = viewport.right + 0.5f * (viewport.right - viewport.left);
                const float gb = viewport.bottom - 0.5f * (viewport.top - viewport.bottom);
                const float gt = viewport.top + 0.5f * (viewport.top - viewport.bottom);
                auto plane_distance = [&](int plane, const ClipVertex &q) {
                    switch (plane) {
                    case 0: return -q.p[2] - near_plane;
                    case 1: return q.p[0] + gl * q.p[2];
                    case 2: return -gr * q.p[2] - q.p[0];
                    case 3: return q.p[1] + gb * q.p[2];
                    default: return -gt * q.p[2] - q.p[1];
                    }
                };
                auto interpolate = [](const ClipVertex &a, const ClipVertex &b, float t) {
                    ClipVertex v{};
                    for (size_t axis = 0; axis < 3; ++axis)
                        v.p[axis] = a.p[axis] + (b.p[axis] - a.p[axis]) * t;
                    v.u = a.u + (b.u - a.u) * t;
                    v.v = a.v + (b.v - a.v) * t;
                    v.a = a.a + (b.a - a.a) * t;
                    v.r = a.r + (b.r - a.r) * t;
                    v.g = a.g + (b.g - a.g) * t;
                    v.b = a.b + (b.b - a.b) * t;
                    return v;
                };
                for (size_t first = 0; first + 2 < mesh.verts.size(); first += 3) {
                    ClipVertex buf_a[8]{}, buf_b[8]{};
                    bool valid = true;
                    for (size_t j = 0; j < 3; ++j) {
                        const video::Vertex &source = mesh.verts[first + j];
                        const float *m = model.matrix.m;
                        ClipVertex &v = buf_a[j];
                        v.p[0] = m[0] * source.pos[0] + m[4] * source.pos[1] +
                            m[8] * source.pos[2] + m[12];
                        v.p[1] = m[1] * source.pos[0] + m[5] * source.pos[1] +
                            m[9] * source.pos[2] + m[13];
                        v.p[2] = m[2] * source.pos[0] + m[6] * source.pos[1] +
                            m[10] * source.pos[2] + m[14];
                        const float source_values[3] = {
                            source.pos[0], source.pos[1], source.pos[2]
                        };
                        for (size_t axis = 0; axis < 3; ++axis) {
                            if (!std::isfinite(source_values[axis]) || !std::isfinite(v.p[axis]))
                                valid = false;
                            else {
                                stats.source_min[axis] = std::min(stats.source_min[axis], source_values[axis]);
                                stats.source_max[axis] = std::max(stats.source_max[axis], source_values[axis]);
                                stats.world_min[axis] = std::min(stats.world_min[axis], v.p[axis]);
                                stats.world_max[axis] = std::max(stats.world_max[axis], v.p[axis]);
                            }
                        }
                        // SceneBuilder UVs are in texels; PVR2 expects normalized coordinates.
                        v.u = textured ? source.uv[0] / float(mesh.width) : 0.0f;
                        v.v = textured ? source.uv[1] / float(mesh.height) : 0.0f;
                        v.a = model.alpha * source.colour[3] / 255.0f;
                        v.r = source.colour[0] / 255.0f;
                        v.g = source.colour[1] / 255.0f;
                        v.b = source.colour[2] / 255.0f;
                        if (first == 0u) {
                            stats.first_camera[j * 3u + 0u] = v.p[0];
                            stats.first_camera[j * 3u + 1u] = v.p[1];
                            stats.first_camera[j * 3u + 2u] = v.p[2];
                            const float distances[5] = {
                                -v.p[2] - near_plane,
                                v.p[0] + gl * v.p[2],
                                -gr * v.p[2] - v.p[0],
                                v.p[1] + gb * v.p[2],
                                -gt * v.p[2] - v.p[1]
                            };
                            for (size_t plane = 0; plane < 5u; ++plane) {
                                stats.clip_min[plane] = std::min(stats.clip_min[plane], distances[plane]);
                                if (distances[plane] >= 0.0f) ++stats.clip_inside[plane];
                            }
                            const float depth = -v.p[2];
                            if (depth > near_plane && std::isfinite(depth)) {
                                const float nx = (2.0f * (v.p[0] / depth) -
                                    (viewport.right + viewport.left)) /
                                    (viewport.right - viewport.left);
                                const float ny = (2.0f * (v.p[1] / depth) -
                                    (viewport.top + viewport.bottom)) /
                                    (viewport.top - viewport.bottom);
                                stats.upstream_screen_x[j] = viewport.vp_x +
                                    (nx + 1.0f) * 0.5f * viewport.vp_w;
                                stats.upstream_screen_y[j] = viewport.vp_y +
                                    (1.0f - ny) * 0.5f * viewport.vp_h;
                                ++stats.upstream_screen_count;
                            }
                        }
                    }
                    if (!valid) {
                        ++stats.triangles_rejected;
                        continue;
                    }
                    int polygon_count = 3;
                    ClipVertex *in = buf_a;
                    ClipVertex *out = buf_b;
                    for (int plane = 0; plane < 5 && polygon_count >= 3; ++plane) {
                        int next_count = 0;
                        for (int k = 0; k < polygon_count; ++k) {
                            const ClipVertex &pa = in[k];
                            const ClipVertex &pb = in[(k + 1) % polygon_count];
                            const float da = plane_distance(plane, pa);
                            const float db = plane_distance(plane, pb);
                            if (da >= 0.0f && next_count < 8)
                                out[next_count++] = pa;
                            if ((da >= 0.0f) != (db >= 0.0f) && next_count < 8) {
                                const float t = da / (da - db);
                                out[next_count++] = interpolate(pa, pb, t);
                            }
                        }
                        polygon_count = next_count;
                        ClipVertex *swap = in; in = out; out = swap;
                    }
                    if (first == 0u) {
                        stats.clipped_vertex_count = static_cast<uint32_t>(polygon_count);
                        for (int k = 0; k < polygon_count; ++k) {
                            const float depth = -in[k].p[2];
                            if (!(depth > near_plane) || !std::isfinite(depth)) continue;
                            const float nx = (2.0f * (in[k].p[0] / depth) -
                                (viewport.right + viewport.left)) /
                                (viewport.right - viewport.left);
                            const float ny = (2.0f * (in[k].p[1] / depth) -
                                (viewport.top + viewport.bottom)) /
                                (viewport.top - viewport.bottom);
                            const float sx = viewport.vp_x + (nx + 1.0f) * 0.5f * viewport.vp_w;
                            const float sy = viewport.vp_y + (1.0f - ny) * 0.5f * viewport.vp_h;
                            stats.clipped_min_x = std::min(stats.clipped_min_x, sx);
                            stats.clipped_min_y = std::min(stats.clipped_min_y, sy);
                            stats.clipped_max_x = std::max(stats.clipped_max_x, sx);
                            stats.clipped_max_y = std::max(stats.clipped_max_y, sy);
                        }
                    }
                    if (polygon_count < 3) {
                        ++stats.triangles_rejected;
                        continue;
                    }
                    // Fan triangulation preserves the clipped convex polygon and emits
                    // complete PVR triangles only; interpolate all per-vertex attributes.
                    for (int tri = 1; tri + 1 < polygon_count; ++tri) {
                        const ClipVertex *fan[3] = {&in[0], &in[tri], &in[tri + 1]};
                        for (size_t j = 0; j < 3; ++j) {
                            const ClipVertex &source = *fan[j];
                            const float depth = -source.p[2];
                            if (!(depth > near_plane) || !std::isfinite(depth)) {
                                valid = false;
                                break;
                            }
                            pvr_vertex_t vertex{};
                            vertex.flags = j == 2 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
                            vertex.x = viewport.vp_x + viewport.vp_w *
                                (source.p[0] / depth - viewport.left) /
                                (viewport.right - viewport.left);
                            vertex.y = viewport.vp_y + viewport.vp_h *
                                (viewport.top - source.p[1] / depth) /
                                (viewport.top - viewport.bottom);
                            if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y)) {
                                valid = false;
                                break;
                            }
                            vertex.z = 1.0f;
                            vertex.u = source.u;
                            vertex.v = source.v;
                            vertex.argb = kForceSolidWhiteDiagnostic ? 0xFFFFFFFFu :
                                PVR_PACK_COLOR(source.a, source.r, source.g, source.b);
                            vertex.oargb = 0;
                            stats.min_depth = std::min(stats.min_depth, depth);
                            stats.max_depth = std::max(stats.max_depth, depth);
                            stats.min_x = std::min(stats.min_x, vertex.x);
                            stats.min_y = std::min(stats.min_y, vertex.y);
                            stats.max_x = std::max(stats.max_x, vertex.x);
                            stats.max_y = std::max(stats.max_y, vertex.y);
                            if (vertex.x < 0.0f || vertex.x >= 640.0f ||
                                vertex.y < 0.0f || vertex.y >= 480.0f)
                                ++stats.vertices_offscreen;
                            if (textured) {
                                stats.min_u = std::min(stats.min_u, vertex.u);
                                stats.max_u = std::max(stats.max_u, vertex.u);
                                stats.min_v = std::min(stats.min_v, vertex.v);
                                stats.max_v = std::max(stats.max_v, vertex.v);
                            }
                            pvr_prim(&vertex, sizeof(vertex));
                        }
                        if (!valid) break;
                        ++stats.triangles_submitted;
                        stats.vertices_submitted += 3;
                    }
                    if (!valid) ++stats.triangles_rejected;
                }
            }
        }
    }
    return stats;
}

static void draw_pvr_control_triangle()
{
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t header;
    pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);
    cxt.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&header, &cxt);
    pvr_prim(&header, sizeof(header));
    pvr_vertex_t v{};
    v.flags = PVR_CMD_VERTEX; v.x = 570.0f; v.y = 112.0f; v.z = 1.0f;
    v.argb = 0xFF20FF20u; pvr_prim(&v, sizeof(v));
    v.flags = PVR_CMD_VERTEX; v.x = 630.0f; v.y = 112.0f; v.z = 1.0f;
    v.argb = 0xFF20FF20u; pvr_prim(&v, sizeof(v));
    v.flags = PVR_CMD_VERTEX_EOL; v.x = 600.0f; v.y = 172.0f; v.z = 1.0f;
    v.argb = 0xFF20FF20u; pvr_prim(&v, sizeof(v));
}

static void draw_pvr_translucent_control_triangle()
{
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t header;
    pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
    cxt.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&header, &cxt);
    pvr_prim(&header, sizeof(header));
    pvr_vertex_t v{};
    v.flags = PVR_CMD_VERTEX; v.x = 570.0f; v.y = 292.0f; v.z = 1.0f;
    v.argb = 0xFFFF2020u; pvr_prim(&v, sizeof(v));
    v.flags = PVR_CMD_VERTEX; v.x = 630.0f; v.y = 292.0f; v.z = 1.0f;
    v.argb = 0xFFFF2020u; pvr_prim(&v, sizeof(v));
    v.flags = PVR_CMD_VERTEX_EOL; v.x = 600.0f; v.y = 352.0f; v.z = 1.0f;
    v.argb = 0xFFFF2020u; pvr_prim(&v, sizeof(v));
}

static void draw_screen(const Results &r, size_t selected_mesh, unsigned diagnostic_page)
{
    pvr_wait_ready();
    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_OP_POLY);
    // v227 renders exactly one real SceneBuilder mesh per capture interval.
    // The green control triangle follows that mesh, making per-mesh list-state
    // interference visible without changing the real geometry or texture path.
    const PvrRenderStats pvr_stats = draw_scene_builder_output(r, selected_mesh);
    draw_pvr_control_triangle();
    pvr_list_finish();
    pvr_list_begin(PVR_LIST_TR_POLY);
    draw_pvr_translucent_control_triangle();
    char title[64];
    char line[64];
    snprintf(title, sizeof(title), "VF3 NAOMI - MODEL3 INTEGRATION %s", VF3_BUILD_VERSION);
    draw_text(40, 48, title);
    draw_text(40, 72, r.synthetic_fixture ?
        "SYNTHETIC ROM DATA - TEST FIXTURE" : "REAL VF3 REV D - WHITE GEOMETRY TEST");
    draw_text(40, 96, r.mount ? "NETDIMM IMAGE MOUNT: PASS" : "NETDIMM IMAGE MOUNT: FAIL");
    draw_text(40, 120, r.board ? "MODEL3 BOARD INIT: PASS" : "MODEL3 BOARD INIT: FAIL");
    draw_text(40, 138, r.synthetic_fixture ?
        (r.board_load ? "MODEL3 LOAD: PASS" : "MODEL3 LOAD: FAIL") :
        (r.board_load && r.program_rom ? "MODEL3 LOAD / VF3 PPC ROM: PASS" :
                                        "MODEL3 LOAD / VF3 PPC ROM: FAIL"));
    draw_text(40, 156, r.crom_cache ? "FIXED/BANKED CROM + PAGE CACHE: PASS" : "FIXED/BANKED CROM + PAGE CACHE: FAIL");
    draw_text(40, 174, r.synthetic_fixture ?
        (r.power_ppc ? "SYNTHETIC PPC CORE SMOKE: PASS" : "SYNTHETIC PPC CORE SMOKE: FAIL") :
        (r.real_boot ? "REAL VF3 PPC BOOT ROM: PASS" : "REAL VF3 PPC BOOT ROM: FAIL"));
    draw_text(40, 192, r.synthetic_fixture ?
        (r.vrom ? "VROM STREAM READ: PASS" : "VROM STREAM READ: FAIL") :
        (r.real_vrom ? "REAL VF3 VROM READ: PASS" : "REAL VF3 VROM READ: FAIL"));
    draw_text(40, 210, r.synthetic_fixture ?
        (r.vrom_edges ? "VROM PAGE/MIRROR/WRAP: PASS" : "VROM PAGE/MIRROR/WRAP: FAIL") :
        "VROM SYNTHETIC EDGE TESTS: N/A");
    draw_text(40, 228, r.synthetic_fixture ?
        (r.vrom_port ? "VROM TEXTURE PORT: PASS" : "VROM TEXTURE PORT: FAIL") :
        "VROM SYNTHETIC TEXTURE TEST: N/A");
    draw_text(40, 246, r.synthetic_fixture ?
        (r.crom_bus ? "PPC FIXED/BANKED CROM BUS: PASS" : "PPC FIXED/BANKED CROM BUS: FAIL") :
        (r.crom_bus ? "REAL PPC CROM INSTRUCTION FETCH: PASS" : "REAL PPC CROM INSTRUCTION FETCH: FAIL"));
    if (r.synthetic_fixture)
        draw_text(40, 264, r.tile_bus ? "SYNTHETIC PPC TILE BUS: PASS" : "SYNTHETIC PPC TILE BUS: FAIL");
    else {
        snprintf(line, sizeof(line), "PPC R1:%08X R2:%08X", r.real_boot_r1, r.real_boot_r2);
        draw_text(40, 264, line);
    }
    if (r.synthetic_fixture)
        draw_text(40, 282, r.scene ? "SCENEBUILDER GEOMETRY/ATTR/CACHE: PASS" : "SCENEBUILDER GEOMETRY/ATTR/CACHE: FAIL");
    else {
        snprintf(line, sizeof(line), "PPC LR:%08X SRR0:%08X", r.real_boot_lr, r.real_boot_srr0);
        draw_text(40, 282, line);
    }
    if (r.synthetic_fixture)
        draw_text(40, 300, r.real3d_bus ? "SYNTHETIC REAL3D REG/DMA: PASS" : "SYNTHETIC REAL3D REG/DMA: FAIL");
    else {
        snprintf(line, sizeof(line), "PPC OP:%08X MSR:%08X", r.real_boot_instruction, r.real_boot_msr);
        draw_text(40, 300, line);
    }
    if (r.synthetic_fixture) {
        draw_text(40, 318, r.texture16 ? "SYNTHETIC REAL3D TEX 16-BIT: PASS" : "SYNTHETIC REAL3D TEX 16-BIT: FAIL");
        draw_text(40, 336, r.texture8_low ? "SYNTHETIC REAL3D TEX 8-LOW: PASS" : "SYNTHETIC REAL3D TEX 8-LOW: FAIL");
        draw_text(40, 354, r.texture8_high ? "SYNTHETIC REAL3D TEX 8-HIGH: PASS" : "SYNTHETIC REAL3D TEX 8-HIGH: FAIL");
    } else {
        snprintf(line, sizeof(line), "PPC R3:%08X R4:%08X", r.real_boot_r3, r.real_boot_r4);
        draw_text(40, 318, line);
        snprintf(line, sizeof(line), "PPC NPC:%08X SRR1:%08X", r.real_boot_npc, r.real_boot_srr1);
        draw_text(40, 336, line);
        snprintf(line, sizeof(line), "IRQ:%X/%X DEC:%08X", r.real_boot_irq_pending_or, r.real_boot_irq_state_or, r.real_boot_dec);
        draw_text(40, 354, line);
    }
    snprintf(line, sizeof(line), "MOUNT:%d VROM:%d NZ:%u MESH:%u VERT:%u",
        r.mount_status, r.vrom_status, static_cast<unsigned>(r.real_vrom_nonzero),
        static_cast<unsigned>(r.meshes), static_cast<unsigned>(r.vertices));
    draw_text(40, 372, line);
    if (r.synthetic_fixture) {
        snprintf(line, sizeof(line), "RAM:%u TILE:%u CULL:%u/%u",
            static_cast<unsigned>(r.ram_bytes), static_cast<unsigned>(r.tile_bytes),
            static_cast<unsigned>(r.cull_lo_words), static_cast<unsigned>(r.cull_hi_words));
        draw_text(40, 390, line);
        snprintf(line, sizeof(line), "POLY:%u FIFO:%u TEX:%u",
            static_cast<unsigned>(r.poly_words), static_cast<unsigned>(r.fifo_capacity),
            static_cast<unsigned>(r.tex_words));
        draw_text(40, 408, line);
        snprintf(line, sizeof(line), "SCENE G:%u C:%u A:%u",
            r.scene_geometry ? 1u : 0u, r.scene_colour ? 1u : 0u,
            r.scene_attributes ? 1u : 0u);
        draw_text(40, 426, line);
        draw_text(40, 444, r.pvr_scene ? "SCENEBUILDER -> PVR2 TRIANGLES: PASS" : "SCENEBUILDER -> PVR2 TRIANGLES: FAIL");
        draw_text(40, 462, scene_texture_ready ? "MODEL3 TEXTURE -> PVR2: PASS" : "MODEL3 TEXTURE -> PVR2: FAIL");
    } else {
        snprintf(line, sizeof(line), "M%u V%u T%u OK%u BAD%u P%u",
            static_cast<unsigned>(selected_mesh + 1),
            static_cast<unsigned>(pvr_stats.mesh_vertices),
            static_cast<unsigned>(pvr_stats.mesh_triangles),
            static_cast<unsigned>(pvr_stats.triangles_submitted),
            static_cast<unsigned>(pvr_stats.triangles_rejected),
            diagnostic_page);
        draw_text(40, 390, line);
        // v236 keeps one stable diagnostic page per mesh so a single capture
        // records the full comparison, rather than cycling three pages.
        snprintf(line, sizeof(line), "CAM P0 %.2g %.2g %.2g",
            pvr_stats.first_camera[0], pvr_stats.first_camera[1], pvr_stats.first_camera[2]);
        draw_text(40, 404, line);
        snprintf(line, sizeof(line), "CAM P1 %.2g %.2g %.2g",
            pvr_stats.first_camera[3], pvr_stats.first_camera[4], pvr_stats.first_camera[5]);
        draw_text(40, 418, line);
        snprintf(line, sizeof(line), "CAM P2 %.2g %.2g %.2g",
            pvr_stats.first_camera[6], pvr_stats.first_camera[7], pvr_stats.first_camera[8]);
        draw_text(40, 432, line);
        snprintf(line, sizeof(line), "CLIP POLYGON VERTICES: %u",
            static_cast<unsigned>(pvr_stats.clipped_vertex_count));
        draw_text(40, 446, line);
        snprintf(line, sizeof(line), "CLIP XY %.3g..%.3g %.3g..%.3g",
            pvr_stats.clipped_min_x, pvr_stats.clipped_max_x,
            pvr_stats.clipped_min_y, pvr_stats.clipped_max_y);
        draw_text(40, 460, line);
        snprintf(line, sizeof(line), "UPSTREAM vs PVR MAX XY DELTA: %.3g",
            pvr_stats.max_projection_delta);
        draw_text(40, 474, line);
    }
    pvr_list_finish();
    pvr_scene_finish();
}

int main()
{
    pvr_init_defaults();
    pvr_set_bg_color(0.02f, 0.06f, 0.12f);
    font_texture = pvr_mem_malloc(256 * 256 * 2);
    if (!font_texture) {
        printf("PVR font texture allocation failed\n");
        for (;;) thd_sleep(1000);
    }
    font_init();
    const Results results = run_integration();
    if (results.pvr_texture_source) {
        scene_texture = pvr_mem_malloc(sizeof(results.pvr_texture_pixels));
        if (scene_texture) {
            pvr_txr_load(results.pvr_texture_pixels, scene_texture,
                         sizeof(results.pvr_texture_pixels));
            scene_texture_ready = true;
        }
    }
    unsigned frame = 0;
    for (;;) {
        const size_t selected_mesh = (frame / 180u) % 9u;
        draw_screen(results, selected_mesh, 0u);
        ++frame;
        thd_sleep(16);
    }
    return 0;
}
