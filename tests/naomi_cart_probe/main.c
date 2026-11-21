/*
 * Naomi cartridge/NetDIMM PIO read probe.
 *
 * Reads selected offsets through the Naomi cartridge ROM PIO registers.
 * This is deliberately read-only and reports raw words on screen.
 */
#include <kos.h>
#include <kos/cache.h>
#include <kos/timer.h>
#include <dc/minifont.h>
#include <dc/pvr.h>
#include <stdint.h>
#include <stdio.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define FONT_W 12
#define FONT_H 24
#define CART_ROM_OFFSETH (*(volatile uint16_t *)0xA05F7000u)
#define CART_ROM_OFFSETL (*(volatile uint16_t *)0xA05F7004u)
#define CART_ROM_DATA    (*(volatile uint16_t *)0xA05F7008u)
#define CART_DMA_OFFSETH (*(volatile uint16_t *)0xA05F700Cu)
#define CART_DMA_OFFSETL (*(volatile uint16_t *)0xA05F7010u)
#define G1_GDSTAR        (*(volatile uint32_t *)0xA05F7404u)
#define G1_GDLEN         (*(volatile uint32_t *)0xA05F7408u)
#define G1_GDDIR         (*(volatile uint32_t *)0xA05F740Cu)
#define G1_GDEN          (*(volatile uint32_t *)0xA05F7414u)
#define G1_GDST          (*(volatile uint32_t *)0xA05F7418u)
#define G1_GDSTARD       (*(volatile uint32_t *)0xA05F74F4u)
#define G1_GDLEND        (*(volatile uint32_t *)0xA05F74F8u)

static pvr_ptr_t font_texture;
static const uint32_t offsets[] = {
    0x00000000u, 0x00000100u, 0x00010000u,
    0x00100000u, 0x00200000u, 0x00300000u,
    0x00400000u, 0x00800000u, 0x01000000u,
    0x02000000u, 0x03F00000u, 0x04000000u
};
static const uint16_t expected_markers[8][4] = {
    { 0x4656u, 0x5033u, 0x4f49u, 0x2131u }, /* VF3PIO1! */
    { 0x4656u, 0x5033u, 0x4f49u, 0x2132u }, /* VF3PIO2! */
    { 0x4656u, 0x5033u, 0x4f49u, 0x2133u }, /* VF3PIO3! */
    { 0x4656u, 0x5033u, 0x4f49u, 0x2134u }, /* VF3PIO4! */
    { 0x4656u, 0x5033u, 0x4f49u, 0x2135u }, /* VF3PIO5! */
    { 0x4656u, 0x5033u, 0x4f49u, 0x2136u }, /* VF3PIO6! */
    { 0x4656u, 0x5033u, 0x4f49u, 0x2137u }, /* VF3PIO7! */
    { 0x4656u, 0x5033u, 0x4f49u, 0x2138u }  /* VF3PIO8! */
};
static uint16_t values[12][4];
static uint16_t repeat_values[12][4];
static int stable[12];
static int marker_ok[12];
#define DMA_MAX_BYTES 0x00100000u
#define DMA_TEST_COUNT 6
static uint16_t dma_buffer[DMA_MAX_BYTES / 2] __attribute__((aligned(32)));
static const uint32_t dma_sizes[DMA_TEST_COUNT] = {
    32u, 256u, 4096u, 65536u, 262144u, 1048576u
};
typedef struct {
    int attempted;
    int passed;
    int timed_out;
    uint32_t transferred;
    uint64_t elapsed_ms;
} dma_result_t;
static dma_result_t dma_results[DMA_TEST_COUNT];
static uint32_t dma_last_status;
static uint32_t dma_last_count;
static uint32_t dma_last_end;
static uint32_t dma_mismatch_index = 0xFFFFFFFFu;
static uint16_t dma_mismatch_actual;
static uint16_t dma_mismatch_expected;
static uint16_t dma_words[16];

static void font_init(void) {
    uint16_t *vram = (uint16_t *)font_texture;
    for(int y = 0; y < 8; ++y) {
        for(int x = 0; x < 16; ++x) {
            minifont_draw(vram, 256, (uint32_t)(y * 16 + x));
            vram += 16;
        }
        vram += 23 * 256;
    }
}

static void draw_char(float x, float y, unsigned char c) {
    if(c <= ' ' || c >= 127) return;
    pvr_vertex_t v;
    const int ix = (c % 16) * 16, iy = (c / 16) * 24;
    const float u1 = ix / 256.0f, v1 = iy / 256.0f;
    const float u2 = (ix + FONT_W) / 256.0f, v2 = (iy + FONT_H) / 256.0f;
    v.flags = PVR_CMD_VERTEX; v.x = x; v.y = y + FONT_H; v.z = 1.0f;
    v.u = u1; v.v = v2; v.argb = PVR_PACK_COLOR(1, 1, 1, 1); v.oargb = 0;
    pvr_prim(&v, sizeof(v));
    v.x = x; v.y = y; v.u = u1; v.v = v1; pvr_prim(&v, sizeof(v));
    v.x = x + FONT_W; v.y = y + FONT_H; v.u = u2; v.v = v2; pvr_prim(&v, sizeof(v));
    v.flags = PVR_CMD_VERTEX_EOL; v.x = x + FONT_W; v.y = y; v.u = u2; v.v = v1;
    pvr_prim(&v, sizeof(v));
}

static void draw_text(float x, float y, const char *s) {
    pvr_poly_cxt_t cxt; pvr_poly_hdr_t poly;
    pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY,
        PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_NONTWIDDLED,
        256, 256, font_texture, PVR_FILTER_NONE);
    pvr_poly_compile(&poly, &cxt); pvr_prim(&poly, sizeof(poly));
    for(size_t i = 0; s[i]; ++i) { draw_char(x, y, (unsigned char)s[i]); x += FONT_W; }
}

static void read_words(uint32_t offset, uint16_t out[4]) {
    CART_ROM_OFFSETH = (uint16_t)(((offset >> 16) & 0x7FFFu) | 0x8000u);
    CART_ROM_OFFSETL = (uint16_t)(offset & 0xFFFFu);
    for(int i = 0; i < 4; ++i) out[i] = CART_ROM_DATA;
}


/*
 * Characterize cartridge-to-RAM G1 DMA using a deterministic 1 MiB payload
 * at source offset 1 MiB. Every transfer is 32-byte aligned and checked
 * word-for-word. The NetDIMM image is only read.
 */
static uint16_t expected_dma_word(uint32_t index) {
    if(index == 0) return 0x4656u;
    if(index == 1) return 0x5033u;
    if(index == 2) return 0x4f49u;
    if(index == 3) return 0x2131u;
    /* The image stores generated words big-endian; SH-4 uint16_t loads
       interpret the same two bytes little-endian after DMA. */
    uint16_t word = (uint16_t)(0xA500u ^ ((index * 73u + (index >> 5)) & 0xFFFFu));
    return (uint16_t)((word << 8) | (word >> 8));
}

/* Per-size elapsed times are rendered with each DMA result for easy capture. */
static void run_dma_probe(void) {
    const uint32_t source_offset = 0x00100000u;
    const uint64_t timeout_ms = 5000u;

    for(int test = 0; test < DMA_TEST_COUNT; ++test) {
        dma_result_t *result = &dma_results[test];
        const uint32_t transfer_size = dma_sizes[test];
        uint64_t start_ms;

        result->attempted = 1;
        if((G1_GDST & 1u) || (G1_GDEN & 1u)) {
            result->attempted = 0;
            break;
        }

        for(uint32_t i = 0; i < transfer_size / 2u; ++i)
            dma_buffer[i] = 0xA5A5u;
        dcache_inval_range((uintptr_t)dma_buffer, transfer_size);
        CART_DMA_OFFSETL = (uint16_t)(source_offset & 0xFFFFu);
        CART_DMA_OFFSETH = (uint16_t)((source_offset >> 16) & 0x7FFFu);

        G1_GDSTAR = (uint32_t)((uintptr_t)dma_buffer & 0x1FFFFFE0u);
        G1_GDLEN = transfer_size;
        G1_GDDIR = 1u;
        G1_GDEN = 1u;
        start_ms = timer_ms_gettime64();
        G1_GDST = 1u;

        while(G1_GDST & 1u) {
            if(timer_ms_gettime64() - start_ms >= timeout_ms) {
                result->timed_out = 1;
                G1_GDEN = 0u;
                break;
            }
            thd_pass();
        }

        result->elapsed_ms = timer_ms_gettime64() - start_ms;
        dma_last_status = G1_GDST;
        result->transferred = G1_GDLEND;
        dma_last_count = G1_GDLEND;
        dma_last_end = G1_GDSTARD;
        dcache_inval_range((uintptr_t)dma_buffer, transfer_size);

        result->passed = !result->timed_out &&
                         ((G1_GDST & 1u) == 0u) &&
                         (result->transferred >= transfer_size);
        for(uint32_t i = 0; i < 16u; ++i)
            dma_words[i] = dma_buffer[i];
        if(result->passed) {
            for(uint32_t i = 0; i < transfer_size / 2u; ++i) {
                if(dma_buffer[i] != expected_dma_word(i)) {
                    result->passed = 0;
                    dma_mismatch_index = i;
                    dma_mismatch_actual = dma_buffer[i];
                    dma_mismatch_expected = expected_dma_word(i);
                    break;
                }
            }
        }

        G1_GDEN = 0u;
        if(!result->passed)
            break;
    }
}

static const char *dma_status_label(int index) {
    if(!dma_results[index].attempted) return "SKIP";
    if(dma_results[index].timed_out) return "TIMEOUT";
    return dma_results[index].passed ? "OK" : "FAIL";
}

static void draw_dma_summary_screen(void) {
    char line[96];
    pvr_wait_ready(); pvr_scene_begin(); pvr_list_begin(PVR_LIST_TR_POLY);
    draw_text(24, 20, "NAOMI CARTRIDGE DMA SIZE TESTS");
    draw_text(24, 52, "SOURCE OFFSET 00100000 - EACH SIZE CHECKED");
    for(int i = 0; i < DMA_TEST_COUNT; ++i) {
        const uint32_t size = dma_sizes[i];
        if(size < 1024u)
            snprintf(line, sizeof(line), "%4luB  %-6s COUNT=%lu  %lums",
                (unsigned long)size, dma_status_label(i),
                (unsigned long)dma_results[i].transferred,
                (unsigned long)dma_results[i].elapsed_ms);
        else
            snprintf(line, sizeof(line), "%4luK  %-6s COUNT=%lu  %lums",
                (unsigned long)(size / 1024u), dma_status_label(i),
                (unsigned long)dma_results[i].transferred,
                (unsigned long)dma_results[i].elapsed_ms);
        draw_text(24, 92 + i * 28, line);
    }
    snprintf(line, sizeof(line), "FINAL STATUS=%08lx END=%08lx",
        (unsigned long)dma_last_status, (unsigned long)dma_last_end);
    draw_text(24, 284, line);
    snprintf(line, sizeof(line), "FINAL COUNT=%lu FIRST MISMATCH=%s",
        (unsigned long)dma_last_count,
        dma_mismatch_index == 0xFFFFFFFFu ? "NONE" : "FOUND");
    draw_text(24, 316, line);
    if(dma_mismatch_index != 0xFFFFFFFFu) {
        snprintf(line, sizeof(line), "INDEX=%lu GOT=%04x EXPECTED=%04x",
            (unsigned long)dma_mismatch_index, dma_mismatch_actual,
            dma_mismatch_expected);
        draw_text(24, 348, line);
    } else {
        draw_text(24, 348, "NO WORD MISMATCH IN ATTEMPTED TRANSFERS");
    }
    draw_text(24, 420, "DMA DATA WORDS ON NEXT SCREEN");
    pvr_list_finish(); pvr_scene_finish();
}

static void draw_dma_screen(void) {
    char line[96];
    pvr_wait_ready(); pvr_scene_begin(); pvr_list_begin(PVR_LIST_TR_POLY);
    draw_text(24, 20, "NAOMI CARTRIDGE DMA DATA DETAIL");
    draw_text(24, 52, "RECEIVED WORDS (SH-4 LITTLE-ENDIAN)");
    draw_text(24, 92, "GOT  00-03");
    snprintf(line, sizeof(line), "%04x %04x %04x %04x",
        dma_words[0], dma_words[1], dma_words[2], dma_words[3]);
    draw_text(180, 92, line);
    draw_text(24, 120, "GOT  04-07");
    snprintf(line, sizeof(line), "%04x %04x %04x %04x",
        dma_words[4], dma_words[5], dma_words[6], dma_words[7]);
    draw_text(180, 120, line);
    draw_text(24, 148, "GOT  08-11");
    snprintf(line, sizeof(line), "%04x %04x %04x %04x",
        dma_words[8], dma_words[9], dma_words[10], dma_words[11]);
    draw_text(180, 148, line);
    draw_text(24, 176, "GOT  12-15");
    snprintf(line, sizeof(line), "%04x %04x %04x %04x",
        dma_words[12], dma_words[13], dma_words[14], dma_words[15]);
    draw_text(180, 176, line);
    draw_text(24, 236, "EXPECTED WORDS 00-03");
    snprintf(line, sizeof(line), "%04x %04x %04x %04x",
        expected_dma_word(0), expected_dma_word(1),
        expected_dma_word(2), expected_dma_word(3));
    draw_text(24, 260, line);
    draw_text(24, 292, "EXPECTED WORDS 04-07");
    snprintf(line, sizeof(line), "%04x %04x %04x %04x",
        expected_dma_word(4), expected_dma_word(5),
        expected_dma_word(6), expected_dma_word(7));
    draw_text(24, 316, line);
    draw_text(24, 348, "EXPECTED WORDS 08-11");
    snprintf(line, sizeof(line), "%04x %04x %04x %04x",
        expected_dma_word(8), expected_dma_word(9),
        expected_dma_word(10), expected_dma_word(11));
    draw_text(24, 372, line);
    draw_text(24, 404, "EXPECTED WORDS 12-15");
    snprintf(line, sizeof(line), "%04x %04x %04x %04x",
        expected_dma_word(12), expected_dma_word(13),
        expected_dma_word(14), expected_dma_word(15));
    draw_text(24, 428, line);
    pvr_list_finish(); pvr_scene_finish();
}

static void draw_pio_screen(void) {
    char line[96];
    pvr_wait_ready(); pvr_scene_begin(); pvr_list_begin(PVR_LIST_TR_POLY);
    draw_text(24, 20, "NAOMI CARTRIDGE PIO READ PROBE");
    draw_text(24, 52, "READ ONLY - NETDIMM/CART ACCESS");
    draw_text(24, 92, "OFFSET       WORD0 WORD1 WORD2 WORD3");
    for(int i = 0; i < 12; ++i) {
        const char *check;
        snprintf(line, sizeof(line), "%08lx %04x  %04x  %04x  %04x",
            (unsigned long)offsets[i], values[i][0], values[i][1],
            values[i][2], values[i][3]);
        draw_text(24, 116 + i * 24, line);
        if(i >= 3 && i <= 10)
            check = marker_ok[i] ? "MARKER OK" : "BAD MARKER";
        else if(i == 11)
            check = marker_ok[i] ? "EOF FFFF" : "NOT FFFF";
        else
            check = "DATA SAMPLE";
        snprintf(line, sizeof(line), "REPEAT: %s  %s",
            stable[i] ? "MATCH" : "MISMATCH", check);
        draw_text(24, 130 + i * 24, line);
    }
    draw_text(24, 430, "DMA DETAILS ON NEXT SCREEN");
    pvr_list_finish(); pvr_scene_finish();
}

static void draw_dma_screen(void) {
    char line[96];
    pvr_wait_ready(); pvr_scene_begin(); pvr_list_begin(PVR_LIST_TR_POLY);
    draw_text(24, 20, "NAOMI CARTRIDGE DMA DIAGNOSTIC");
    snprintf(line, sizeof(line), "32B %s  STATUS=%08lx COUNT=%lu",
        dma_status_label(0), (unsigned long)dma_last_status,
        (unsigned long)dma_last_count);
    draw_text(24, 52, line);
    snprintf(line, sizeof(line), "END=%08lx  FIRST MISMATCH=%s",
        (unsigned long)dma_last_end,
        dma_mismatch_index == 0xFFFFFFFFu ? "NONE" : "SEE BELOW");
    draw_text(24, 84, line);
    if(dma_mismatch_index != 0xFFFFFFFFu) {
        snprintf(line, sizeof(line), "INDEX=%lu GOT=%04x EXPECTED=%04x",
            (unsigned long)dma_mismatch_index, dma_mismatch_actual,
            dma_mismatch_expected);
        draw_text(24, 116, line);
    } else {
        draw_text(24, 116, "NO WORD MISMATCH IN ATTEMPTED TRANSFERS");
    }
    draw_text(24, 160, "GOT  00-03");
    snprintf(line, sizeof(line), "%04x %04x %04x %04x",
        dma_words[0], dma_words[1], dma_words[2], dma_words[3]);
    draw_text(180, 160, line);
    draw_text(24, 184, "GOT  04-07");
    snprintf(line, sizeof(line), "%04x %04x %04x %04x",
        dma_words[4], dma_words[5], dma_words[6], dma_words[7]);
    draw_text(180, 184, line);
    draw_text(24, 208, "GOT  08-11");
    snprintf(line, sizeof(line), "%04x %04x %04x %04x",
        dma_words[8], dma_words[9], dma_words[10], dma_words[11]);
    draw_text(180, 208, line);
    draw_text(24, 232, "GOT  12-15");
    snprintf(line, sizeof(line), "%04x %04x %04x %04x",
        dma_words[12], dma_words[13], dma_words[14], dma_words[15]);
    draw_text(180, 232, line);
    draw_text(24, 280, "EXPECTED WORDS 00-03");
    snprintf(line, sizeof(line), "%04x %04x %04x %04x",
        expected_dma_word(0), expected_dma_word(1),
        expected_dma_word(2), expected_dma_word(3));
    draw_text(24, 304, line);
    draw_text(24, 328, "EXPECTED WORDS 04-07");
    snprintf(line, sizeof(line), "%04x %04x %04x %04x",
        expected_dma_word(4), expected_dma_word(5),
        expected_dma_word(6), expected_dma_word(7));
    draw_text(24, 352, line);
    draw_text(24, 376, "EXPECTED WORDS 08-11");
    snprintf(line, sizeof(line), "%04x %04x %04x %04x",
        expected_dma_word(8), expected_dma_word(9),
        expected_dma_word(10), expected_dma_word(11));
    draw_text(24, 400, line);
    draw_text(24, 424, "EXPECTED WORDS 12-15");
    snprintf(line, sizeof(line), "%04x %04x %04x %04x",
        expected_dma_word(12), expected_dma_word(13),
        expected_dma_word(14), expected_dma_word(15));
    draw_text(24, 448, line);
    pvr_list_finish(); pvr_scene_finish();
}

int main(void) {
    for(int i = 0; i < 12; ++i) {
        read_words(offsets[i], values[i]);
        read_words(offsets[i], repeat_values[i]);
        stable[i] = 1;
        for(int j = 0; j < 4; ++j)
            if(values[i][j] != repeat_values[i][j]) stable[i] = 0;
        if(i >= 3 && i <= 10) {
            marker_ok[i] = 1;
            for(int j = 0; j < 4; ++j)
                if(values[i][j] != expected_markers[i - 3][j]) marker_ok[i] = 0;
        }
        else if(i == 11) {
            marker_ok[i] = 1;
            for(int j = 0; j < 4; ++j)
                if(values[i][j] != 0xFFFFu) marker_ok[i] = 0;
        }
    }
    run_dma_probe();
    pvr_init_defaults(); pvr_set_bg_color(0.02f, 0.06f, 0.12f);
    font_texture = pvr_mem_malloc(256 * 256 * 2);
    if(!font_texture) for(;;) thd_sleep(1000);
    font_init();
    for(;;) {
        uint32_t page = (uint32_t)((timer_ms_gettime64() / 3000u) % 3u);
        if(page == 0u) draw_pio_screen();
        else if(page == 1u) draw_dma_summary_screen();
        else draw_dma_screen();
        thd_sleep(16);
    }
    return 0;
}
