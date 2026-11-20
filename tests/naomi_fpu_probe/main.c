/*
 * SH-4 FPU rounding probe for Naomi.
 *
 * Reports actual result bit patterns and FPSCR snapshots for nearest and
 * toward-zero modes. It deliberately does not assume undocumented flag
 * encodings: the raw FPSCR is printed for inspection.
 */
#include <kos.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

static volatile float lhs;
static volatile float rhs;
static volatile float result;

static __attribute__((noinline)) float divide_values(float a, float b) {
    return a / b;
}

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void run_case(const char *name, float a, float b, unsigned mode) {
    unsigned fpscr = __builtin_sh_get_fpscr();
    fpscr = (fpscr & ~3u) | (mode & 1u);
    __builtin_sh_set_fpscr(fpscr);

    lhs = a;
    rhs = b;
    result = divide_values(lhs, rhs);

    /* Read immediately after the FPU operation, before formatting output. */
    unsigned after = __builtin_sh_get_fpscr();
    printf("%s rm=%u result=%08lx fpscr=%08lx\n",
           name, mode, (unsigned long)float_bits(result),
           (unsigned long)after);
}

int main(void) {
    unsigned original = __builtin_sh_get_fpscr();

    printf("NAOMI SH-4 FPU ROUNDING PROBE\n");
    printf("original FPSCR=%08lx\n", (unsigned long)original);

    /* Exact result: should not raise inexact. */
    run_case("exact-positive", 1.0f, 2.0f, 0);
    run_case("exact-positive", 1.0f, 2.0f, 1);

    /* Inexact positive and negative results distinguish directed behaviour. */
    run_case("inexact-positive", 1.0f, 10.0f, 0);
    run_case("inexact-positive", 1.0f, 10.0f, 1);
    run_case("inexact-negative", -1.0f, 10.0f, 0);
    run_case("inexact-negative", -1.0f, 10.0f, 1);

    __builtin_sh_set_fpscr(original);
    printf("FPSCR restored=%08lx\n", (unsigned long)__builtin_sh_get_fpscr());
    printf("PROBE COMPLETE - capture serial output\n");

    for(;;)
        thd_sleep(1000);
    return 0;
}
