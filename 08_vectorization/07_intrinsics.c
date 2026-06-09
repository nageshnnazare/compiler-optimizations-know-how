/* 07_intrinsics.c — hand-written vectors
 *
 * Two portable-ish ways to write vectors in pure C without target-specific
 * intrinsics:
 *
 *   1. GCC/Clang `vector_size` attribute
 *      - looks like ordinary C, uses +, -, *, comparisons, etc.
 *      - lowered to whichever ISA the compiler is targeting
 *
 *   2. clang's `ext_vector_type` + Swizzle members (Apple-style).
 *
 * For the ISA-specific case (Intel intrinsics), use <immintrin.h>. The
 * trade-off: portable code is easier to read; intrinsics give you exact
 * control over which instructions execute.
 */
#include <stddef.h>
#include <stdint.h>

/* 8 lanes × int32 = 256-bit register width (AVX2 / NEON sve₂₅₆). */
typedef int32_t v8i __attribute__((vector_size(32)));

/*  v_add — `+` on a vector_size type → SIMD add.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3 -mavx2):
 *      v_add:
 *          vpaddd  ymm0, ymm1, ymm0       ; 8-lane SIMD add
 *          ret
 *
 *  WHY: `vector_size(32)` produces a 256-bit type. Operators are
 *  intrinsic. The compiler emits the exact SIMD instruction.
 */
v8i v_add(v8i a, v8i b) { return a + b; }

/*  v_mul — `*` on a vector_size type → SIMD mul.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3 -mavx2):
 *      v_mul:
 *          vpmulld ymm0, ymm1, ymm0       ; 8-lane packed multiply
 *          ret
 */
v8i v_mul(v8i a, v8i b) { return a * b; }

/*  v_max — manual max via mask & blend.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3 -mavx2):
 *      v_max:
 *          vpmaxsd ymm0, ymm0, ymm1       ; ONE instruction — the manual
 *                                          ; mask+and+or pattern is
 *                                          ; recognized and lowered to
 *                                          ; the native packed-max op.
 *          ret
 *
 *  WHY: InstCombine pattern-matches the `(m & a) | (~m & b)` idiom
 *  with `m = a > b` and rewrites to `select`. The SIMD selector then
 *  uses VPMAXSD (signed 32-bit packed max) when targeting AVX2.
 *  Three-operand vector form, one µop, fully pipelined.
 */
v8i v_max(v8i a, v8i b) {
    v8i m = a > b;
    return (m & a) | (~m & b);
}

/*  v_axpy — vectorized stream of {2*x + y}.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3 -mavx2): the body uses
 *      vpslld ymm0, ymm_x, 1               ; 2*x via shift
 *      vpaddd ymm_out, ymm0, ymm_y
 *      vmovdqu [out], ymm_out
 *  Or, if FMA is available and the type is float, a single VFMADD.
 *  With INT, x86 has no integer FMA, so it's shift+add.
 */
void v_axpy(v8i *out, const v8i *x, const v8i *y, size_t n) {
    for (size_t i = 0; i < n; i++) out[i] = x[i] * (v8i){2,2,2,2,2,2,2,2} + y[i];
}

/* On x86_64 (with -mavx2) the vector type is one register; on aarch64
 * (with NEON) it's split across two 128-bit Q registers, etc. */
