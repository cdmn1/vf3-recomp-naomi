/*
 * SH-4 FPU rounding probe for Naomi.
 *
 * All results are shown on the Naomi video output. No serial/debug cable
 * is required. The probe compares nearest and toward-zero rounding for
 * exact and inexact positive/negative divisions and displays raw FPSCR.
 */
#include <kos.h>
#include <dc/minifont.h>
#include <dc/pvr.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define FONT_W 12
#define FONT_H 24

typedef struct {
    uint32_t result_bits;
    uint32_t fpscr;
} probe_result_t;

static pvr_ptr_t font_texture;

static __attribute__((noinline)) float divide_values(float a, float b) {
    return a / b;
}

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static probe_result_t run_case(float a, float b, unsigned mode) {
    unsigned fpscr = __builtin_sh_get_fpscr();
    fpscr = (fpscr & ~3u) | (mode & 1u);
    __builtin_sh_set_fpscr(fpscr);

    volatile float lhs = a;
    volatile float rhs = b;
    volatile float result = divide_values(lhs, rhs);

    probe_result_t out;
    out.result_bits = float_bits(result);
    out.fpscr = __builtin_sh_get_fpscr();
    return out;
}

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
        draw_char(x, y, (unsigned char)s[i]);
        x += FONT_W;
    }
}

static void draw_screen(const probe_result_t r[6], unsigned original) {
    char line[96];

    pvr_wait_ready();
    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_TR_POLY);

    draw_text(32.0f, 24.0f, "NAOMI SH-4 FPU ROUNDING PROBE");
    draw_text(32.0f, 64.0f, "NO SERIAL CONNECTION REQUIRED");
    snprintf(line, sizeof(line), "ORIGINAL FPSCR: %08lx", (unsigned long)original);
    draw_text(32.0f, 112.0f, line);

    draw_text(32.0f, 160.0f, "CASE             RM   RESULT BITS   FPSCR");
    static const char *names[6] = {
        "EXACT POSITIVE", "EXACT POSITIVE",
        "INEXACT POSITIVE", "INEXACT POSITIVE",
        "INEXACT NEGATIVE", "INEXACT NEGATIVE"
    };
    for(int i = 0; i < 6; ++i) {
        snprintf(line, sizeof(line), "%-16s %u    %08lx   %08lx",
                 names[i], (unsigned)(i & 1),
                 (unsigned long)r[i].result_bits,
                 (unsigned long)r[i].fpscr);
        draw_text(32.0f, 192.0f + i * 32.0f, line);
    }

    draw_text(32.0f, 416.0f, "RM 0=NEAREST  RM 1=TOWARD ZERO");
    draw_text(32.0f, 448.0f, "RESULTS ARE RAW IEEE-754 FLOAT BITS");
    pvr_list_finish();
    pvr_scene_finish();
}

int main(void) {
    const unsigned original = __builtin_sh_get_fpscr();
    probe_result_t results[6];

    results[0] = run_case(1.0f, 2.0f, 0);
    results[1] = run_case(1.0f, 2.0f, 1);
    results[2] = run_case(1.0f, 10.0f, 0);
    results[3] = run_case(1.0f, 10.0f, 1);
    results[4] = run_case(-1.0f, 10.0f, 0);
    results[5] = run_case(-1.0f, 10.0f, 1);

    __builtin_sh_set_fpscr(original);

    pvr_init_defaults();
    pvr_set_bg_color(0.02f, 0.06f, 0.12f);
    font_texture = pvr_mem_malloc(256 * 256 * 2);
    if(!font_texture) {
        for(;;) thd_sleep(1000);
    }
    font_init();

    for(;;) {
        draw_screen(results, original);
        thd_sleep(16);
    }
    return 0;
}
