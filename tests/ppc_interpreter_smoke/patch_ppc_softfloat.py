#!/usr/bin/env python3
"""Apply the Naomi SoftFloat compatibility layer to the pinned upstream PPC core.

The upstream source is checked out at a fixed commit by the Naomi workflow.  Keep
this patch script strict: a changed upstream source must fail loudly rather than
silently producing a partially patched interpreter.
"""
from pathlib import Path
import sys

path = Path(sys.argv[1])
source = path.read_text()

def replace_once(old: str, new: str, label: str) -> None:
    global source
    count = source.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, found {count}")
    source = source.replace(old, new, 1)

replace_once('#include <cfenv>\n', 'extern "C" {\n#include <softfloat.h>\n}\n', 'SoftFloat include')
replace_once('#pragma STDC FENV_ACCESS ON\n', '', 'remove FENV pragma')
replace_once('#pragma GCC optimize("trapping-math,rounding-math,fp-contract=off")',
             '#pragma GCC optimize("fp-contract=off")', 'GCC floating-point pragma')
replace_once('const int ROUNDING[4] = { FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD };',
             '''// PPC FPSCR RN encoding: nearest, toward zero, toward +infinity, toward -infinity.
const uint_fast8_t PPC_SOFTFLOAT_ROUNDING[4] = {
    softfloat_round_near_even, softfloat_round_minMag,
    softfloat_round_max, softfloat_round_min
};''', 'rounding-mode table')

replace_once('''// The arithmetic in the FPSCR's rounding mode (only switched when it isn't round-to-nearest)
struct Rounding
{
\tuint32_t mode;
\texplicit Rounding(uint32_t fpscr) : mode(fpscr & 3) { if (mode) std::fesetround(ROUNDING[mode]); }
\t~Rounding() { if (mode) std::fesetround(FE_TONEAREST); }
};''',
'''// SoftFloat supplies all four IEEE-754 modes on SH-4, independent of the
// hardware FPU's more limited rounding-mode support.
struct Rounding
{
    uint_fast8_t previous;
    explicit Rounding(uint32_t fpscr) : previous(softfloat_roundingMode)
    {
        softfloat_roundingMode = PPC_SOFTFLOAT_ROUNDING[fpscr & 3];
    }
    ~Rounding() { softfloat_roundingMode = previous; }
};

// Negating a rounded result reverses directed-rounding direction. Keep the
// upstream NaN-selection path, but invert only the final add/sub rounding.
struct NegatedRounding
{
    uint_fast8_t previous;
    NegatedRounding() : previous(softfloat_roundingMode)
    {
        if (previous == softfloat_round_max) softfloat_roundingMode = softfloat_round_min;
        else if (previous == softfloat_round_min) softfloat_roundingMode = softfloat_round_max;
    }
    ~NegatedRounding() { softfloat_roundingMode = previous; }
};''', 'Rounding guard')

replace_once('''static inline double bits(uint64_t i) { Fpr f; f.i = i; return f.d; }
static inline uint64_t ubits(double d) { Fpr f; f.d = d; return f.i; }

// The result r of "x op y", with x the operand SSE takes first''',
'''static inline double bits(uint64_t i) { Fpr f; f.i = i; return f.d; }
static inline uint64_t ubits(double d) { Fpr f; f.d = d; return f.i; }

static inline float64_t sf64(double d)
{
    float64_t v;
    v.v = ubits(d);
    return v;
}

static inline double sf_add(double a, double b) { return bits(f64_add(sf64(a), sf64(b)).v); }
static inline double sf_sub(double a, double b) { return bits(f64_sub(sf64(a), sf64(b)).v); }
static inline double sf_mul(double a, double b) { return bits(f64_mul(sf64(a), sf64(b)).v); }
static inline double sf_div(double a, double b) { return bits(f64_div(sf64(a), sf64(b)).v); }
static inline double sf_sqrt(double a) { return bits(f64_sqrt(sf64(a)).v); }

// The result r of "x op y", with x the operand SSE takes first''', 'SoftFloat arithmetic helpers')

replace_once('''static inline double sse_sqrt(double x)
{
\tdouble r = std::sqrt(x);
\tif (!std::isnan(r))
\t\treturn r;
\treturn std::isnan(x) ? bits(ubits(x) | 0x0008000000000000ull) : bits(SSE_DEFAULT_NAN);
}''',
'''static inline double sse_sqrt(double x)
{
    double r = sf_sqrt(x);
    if (!std::isnan(r))
        return r;
    return std::isnan(x) ? bits(ubits(x) | 0x0008000000000000ull) : bits(SSE_DEFAULT_NAN);
}''', 'SoftFloat square root')

replace_once('''static inline int64_t to_i64(double d)
{
\tif (!(d >= -9223372036854775808.0 && d < 9223372036854775808.0))
\t\treturn INT64_MIN;
\treturn int64_t(d);
}''',
'''static inline float64_t sf64(double d);\n\nstatic inline int64_t to_i64(double d, uint32_t mode)\n{
    return int64_t(f64_to_i64(sf64(d), PPC_SOFTFLOAT_ROUNDING[mode & 3], false));
}''', 'SoftFloat integer conversion')

replace_once('''static inline double to_single(double d)
{
\tif (std::isnan(d))
\t\treturn bits((ubits(d) & 0xFFFFFFFFE0000000ull) | 0x0008000000000000ull);
\treturn double(float(d));
}''',
'''static inline double to_single(double d)
{
    if (std::isnan(d))
        return bits((ubits(d) & 0xFFFFFFFFE0000000ull) | 0x0008000000000000ull);
    float32_t rounded = f64_to_f32(sf64(d));
    F32 f;
    f.i = uint32_t(rounded.v);
    return double(f.f);
}''', 'SoftFloat single conversion')

# Replace every native arithmetic operation in the PPC floating-point instruction handlers.
replacements = [
    ('sse(C * A, C, A)', 'sse(sf_mul(C, A), C, A)'),
    ('sse(A * C, A, C)', 'sse(sf_mul(A, C), A, C)'),
    ('sse(p - B, p, B)', 'sse(sf_sub(p, B), p, B)'),
    ('sse(p + B, p, B)', 'sse(sf_add(p, B), p, B)'),
    ('sse(A / B, A, B)', 'sse(sf_div(A, B), A, B)'),
    ('sse(A - B, A, B)', 'sse(sf_sub(A, B), A, B)'),
    ('sse(B + A, B, A)', 'sse(sf_add(B, A), B, A)'),
    ('sse(A + B, A, B)', 'sse(sf_add(A, B), A, B)'),
    ('sse(1.0 / B, 1.0, B)', 'sse(sf_div(1.0, B), 1.0, B)'),
    ('sse(1.0 / r, 1.0, r)', 'sse(sf_div(1.0, r), 1.0, r)'),
]
for old, new in replacements:
    count = source.count(old)
    if count == 0:
        raise SystemExit(f"floating-point operation not found: {old}")
    source = source.replace(old, new)

# fnmadd/fnmsub negate the final arithmetic result. For directed rounding,
# the inner operation must use the opposite direction before the exact sign flip.
for old, new in [
    ("F(t).d = neg(sse(sf_sub(p, B), p, B));",
     "{ NegatedRounding n; F(t).d = neg(sse(sf_sub(p, B), p, B)); }"),
    ("F(t).d = neg(sse(sf_add(p, B), p, B));",
     "{ NegatedRounding n; F(t).d = neg(sse(sf_add(p, B), p, B)); }"),
    ("F(t).d = to_single(neg(sse(sf_sub(p, B), p, B)));",
     "{ NegatedRounding n; F(t).d = to_single(neg(sse(sf_sub(p, B), p, B))); }"),
    ("F(t).d = to_single(neg(sse(sf_add(p, B), p, B)));",
     "{ NegatedRounding n; F(t).d = to_single(neg(sse(sf_add(p, B), p, B))); }"),
]:
    count = source.count(old)
    if count != 1:
        raise SystemExit(f"negated multiply-add rounding: expected 1 match for {old}, found {count}")
    source = source.replace(old, new, 1)

# Replace the conversion block without relying on whitespace-sensitive sub-blocks.
old = '''\t\tuint32_t mode = x == 15 ? 1 : s.fpscr & 3;
\t\tswitch (mode) {
\t\tcase 0: { Rounding m(s.fpscr); r = to_i64(std::nearbyint(F(b).d)); } break;
\t\tcase 1: r = to_i64(std::trunc(F(b).d)); break;
\t\tcase 2: r = to_i64(std::ceil(F(b).d)); break;
\t\tcase 3: r = to_i64(std::floor(F(b).d)); break;
\t\t}'''
new = '''\t\tuint32_t mode = x == 15 ? 1 : s.fpscr & 3;
\t\tr = to_i64(F(b).d, mode);'''
replace_once(old, new, 'fctiw conversion')

replace_once('''case 12: { vxsnan(s, F(b)); F(t).d = to_single(B); } set_fprf(s, F(t)); if (rcbit(op)) set_cr1(s); return;  // frsp''',
'''case 12: { vxsnan(s, F(b)); Rounding m(s.fpscr); F(t).d = to_single(B); } set_fprf(s, F(t)); if (rcbit(op)) set_cr1(s); return;  // frsp''',
'frsp rounding mode')

replace_once('''\t\tF32 f;
\t\tf.f = float(F(rd(op)).d);''',
'''\t\tF32 f;
\t\t{ Rounding m(s.fpscr); f.f = float(to_single(F(rd(op)).d)); }''',
'stfs rounding')

replace_once('''\t\tF32 f;
\t\tf.f = float(F(t).d);''',
'''\t\tF32 f;
\t\t{ Rounding m(s.fpscr); f.f = float(to_single(F(t).d)); }''',
'stfsx rounding')

path.write_text(source)
print(f"Patched {path}")
