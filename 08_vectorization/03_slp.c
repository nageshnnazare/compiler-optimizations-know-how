/* 03_slp.c — STRAIGHT-LINE-CODE VECTORIZATION (SLP)
 * ============================================================================
 *
 * Idea
 * ----
 *   The loop vectorizer handles vector OPs across iterations of a loop.
 *   The SLP (Superword-Level Parallelism) vectorizer does the same for
 *   straight-line code: it finds groups of independent scalar ops on
 *   adjacent memory locations and fuses them into one SIMD op.
 *
 *     a[0] = b[0] + c[0];
 *     a[1] = b[1] + c[1];           ─►  movaps + addps + movaps
 *     a[2] = b[2] + c[2];                (3 instructions vs 12)
 *     a[3] = b[3] + c[3];
 *
 * Pass names
 * ----------
 *   LLVM: SLPVectorizerPass; VectorCombine handles smaller patterns.
 *   GCC : tree-vect-slp.cc.
 *
 * Why it pays off
 * ---------------
 *   – Straight-line code has no loop overhead, so the gain from the
 *     SIMD op is pure speed.
 *   – Often enables further fusion: a vectorized `mul` + `add` can
 *     become a single FMA.
 * ============================================================================
 */
#include <stddef.h>

/*  slp4 — 4 adjacent scalar adds; NEEDS restrict to fire.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3 -mavx2, NO restrict): SCALAR adds emitted, four separate
 *  `vaddss` + `vmovss` triples. SLP DID NOT FIRE.
 *
 *      slp4:
 *          vmovss xmm0, [rsi]
 *          vaddss xmm0, xmm0, [rdx]
 *          vmovss [rdi], xmm0
 *          ; ... three more ...
 *
 *  WHY: without restrict, the compiler can't prove `a`, `b`, `c` are
 *  disjoint; emitting a 16-byte vector load could read past the end of
 *  a partial overlap. Conservative scalar code.
 */
void slp4(float *a, const float *b, const float *c) {
    a[0] = b[0] + c[0];
    a[1] = b[1] + c[1];
    a[2] = b[2] + c[2];
    a[3] = b[3] + c[3];
}

/*  slp4_restrict — same body but with restrict; SLP fires.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3 -mavx2, with restrict):
 *      slp4_restrict:
 *          vmovups  xmm0, [rsi]         ; load b[0..3]
 *          vaddps   xmm0, xmm0, [rdx]    ; SIMD add c[0..3]
 *          vmovups  [rdi], xmm0          ; store a[0..3]
 *          ret
 *
 *  Three vector instructions instead of 4 × 3 = 12 scalar. ~4× fewer
 *  µops; ~4× less load/store throughput pressure.
 */
void slp4_restrict(float * restrict a,
                   const float * restrict b,
                   const float * restrict c) {
    a[0] = b[0] + c[0];
    a[1] = b[1] + c[1];
    a[2] = b[2] + c[2];
    a[3] = b[3] + c[3];
}

/*  add — operator on two short vectors via clang ext_vector_type.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3 -mavx2):
 *      add:
 *          vaddps  xmm0, xmm0, xmm2   ; lanes 0..3 of x + y
 *          vaddps  xmm1, xmm1, xmm3   ; lanes 4..7 of x + y
 *          ret
 *
 *  WHY: `float __attribute__((ext_vector_type(8)))` directly maps to a
 *  pair of XMM/YMM regs. Operators are intrinsic; no auto-vectorizer
 *  needed.
 */
typedef float vf8 __attribute__((ext_vector_type(8)));
vf8 add(vf8 x, vf8 y) { return x + y; }

/*  scale4 — scale 4 adjacent floats by a constant in a SIMD-friendly way.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3 -mavx2):
 *      scale4:
 *          vbroadcastss xmm0, xmm0        ; broadcast scale into 4 lanes
 *          vmulps       xmm0, xmm0, [rdi] ; element-wise multiply
 *          vmovups      [rdi], xmm0       ; store back
 *          ret
 *
 *  WHY: SLP sees 4 adjacent scalar `a[i] *= scale`. It packs them into
 *  one vmulps; the constant scale is broadcast to all 4 lanes.
 */
void scale4(float *a, float scale) {
    a[0] *= scale;
    a[1] *= scale;
    a[2] *= scale;
    a[3] *= scale;
}
