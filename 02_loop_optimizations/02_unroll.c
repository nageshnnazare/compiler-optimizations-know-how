/* 02_unroll.c — LOOP UNROLLING (full + partial)
 * ============================================================================
 *
 * Two flavours
 * ------------
 *   FULL unroll       trip count is a small known constant; the loop is
 *                     completely replaced by its straight-line expansion.
 *
 *   PARTIAL unroll    trip count unknown or large; replicate body N times,
 *                     keep a scalar epilogue for `n % N` leftovers.
 *
 * Why
 * ---
 *   – Removes loop-control overhead (one cmp+jcc per N iterations).
 *   – Exposes ILP (independent ops on different unrolled iterations
 *     dispatch in parallel on a wide OoO core).
 *   – Enables vectorization: 4 independent scalar iterations become one
 *     4-wide SIMD iteration.
 *
 * Pass names
 * ----------
 *   LLVM:  LoopUnrollPass (full + partial). Cost model in TTI.
 *   GCC :  tree-loop-unroll.cc; --param max-unroll-times bounds factor.
 *
 * Pragmas
 * -------
 *   #pragma GCC unroll N          (GCC)
 *   #pragma clang loop unroll_count(N)
 *   #pragma clang loop unroll(disable|enable|full)
 * ============================================================================
 */
#include <stddef.h>

/*  full_unroll — trip count is the constant 4 → the loop DISAPPEARS.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      .LCPI0_0:
 *          .long 0, 7, 14, 21               ; precomputed i*7 for i=0..3
 *      full_unroll:
 *          movaps  xmm0, xmmword ptr [rip + LCPI0_0]
 *          movups  xmmword ptr [rdi], xmm0   ; ONE 16-byte store, NO loop
 *          ret
 *
 *  WHAT WE'D NAIVELY EXPECT: 4 scalar stores `mov [rdi+4*i], 7*i`.
 *
 *  WHY: Three things happened in sequence.
 *    1. LoopUnroll saw trip count = 4 and replaced the loop with four
 *       straight-line statements `a[0]=0; a[1]=7; a[2]=14; a[3]=21`.
 *    2. The SLP-vectorizer noticed 4 contiguous adjacent constant stores
 *       and folded them into one 16-byte vector store.
 *    3. The 4 constants were materialized in `.rodata`; the runtime cost
 *       is now one cache-friendly load + one store.
 */
void full_unroll(int *a) {
    for (int i = 0; i < 4; i++) {
        a[i] = i * 7;
    }
}

/*  partial_unroll — unknown trip count → main vector loop + scalar tail.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3, simplified):
 *      partial_unroll:
 *          test  rcx, rcx
 *          je    .exit                              ; n == 0
 *          ; runtime alias check between a/b, a/c (memcheck)
 *          ...
 *          ja    .vector_loop                       ; fast path
 *      .scalar_loop:                                ; fallback (small n or alias)
 *          mov   r10d, [rdx + 4*r8]
 *          add   r10d, [rsi + 4*r8]
 *          mov   [rdi + 4*r8], r10d
 *          inc   r8
 *          dec   r9
 *          jne   .scalar_loop
 *      .vector_loop:                                ; chunks of 4 (or 8) elements
 *          ; vectorized add of 4 ints at a time, then maybe 4 more (interleave 2)
 *          ...
 *
 *  WHY: With pointers that *might* alias, the optimizer:
 *    1. Emits a runtime memcheck guard.
 *    2. On the "no alias" branch, unrolls + vectorizes the loop body by
 *       factor 4 (SSE) or 8 (AVX2) with interleave 2.
 *    3. Emits a scalar epilogue for `n % VF` leftovers.
 *    4. Emits a *scalar* version of the whole loop for when memcheck
 *       fails or n is small (cost model says vector overhead > savings).
 */
void partial_unroll(int *a, const int *b, const int *c, size_t n) {
    for (size_t i = 0; i < n; i++) {
        a[i] = b[i] + c[i];
    }
}

/*  unroll_4 — pragma overrides the cost model and forces factor 4.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Use sparingly. The compiler's heuristic is usually right; pragmas
 *  shine when YOU know something the cost model doesn't (e.g. the loop
 *  is on the critical path of a benchmark whose body is too small to
 *  trigger the heuristic).
 */
void unroll_4(int *a, const int *b, size_t n) {
#if defined(__clang__)
#  pragma clang loop unroll_count(4)
#elif defined(__GNUC__)
#  pragma GCC unroll 4
#endif
    for (size_t i = 0; i < n; i++) {
        a[i] = b[i] * 2;
    }
}

/*  unroll_disabled — pragma forces NO unrolling.
 *  ──────────────────────────────────────────────────────────────────────────
 *  The resulting loop will look like 3 instructions in the body and a
 *  single branch back to the top. Often slower than the default; useful
 *  for code-size constrained builds or to keep the i-cache footprint
 *  small in a function that is only sporadically hot.
 */
void unroll_disabled(int *a, const int *b, size_t n) {
#if defined(__clang__)
#  pragma clang loop unroll(disable)
#elif defined(__GNUC__)
#  pragma GCC unroll 1
#endif
    for (size_t i = 0; i < n; i++) {
        a[i] = b[i] * 2;
    }
}

/*  reduce — sum-of-array reduction.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3) is something like (sketch):
 *      reduce:
 *          test  rsi, rsi
 *          je    .ret_zero
 *          ; preheader: zero 4 vector accumulators
 *          pxor  xmm0, xmm0
 *          pxor  xmm1, xmm1
 *          pxor  xmm2, xmm2
 *          pxor  xmm3, xmm3
 *      .vec:
 *          paddd xmm0, xmmword ptr [rdi]        ; 4 independent accumulators
 *          paddd xmm1, xmmword ptr [rdi + 16]    ; → exposes ILP
 *          paddd xmm2, xmmword ptr [rdi + 32]
 *          paddd xmm3, xmmword ptr [rdi + 48]
 *          add   rdi, 64
 *          ...
 *          ; horizontal reduce xmm0..xmm3 into eax at the end
 *
 *  WHY: Integer + is associative, so the vectorizer is free to use four
 *  parallel partial sums (PARTIAL UNROLL by 4 with INTERLEAVE). That
 *  cuts the dependency chain length from N down to N/4 and makes the
 *  loop bound only by load bandwidth, not by the add's 1-cycle latency.
 */
int reduce(const int *a, size_t n) {
    int sum = 0;
    for (size_t i = 0; i < n; i++) sum += a[i];
    return sum;
}
