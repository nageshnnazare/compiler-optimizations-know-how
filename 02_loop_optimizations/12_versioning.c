/* 12_versioning.c — LOOP VERSIONING
 * ============================================================================
 *
 * Idea
 * ----
 *   When the compiler cannot statically prove a property required by an
 *   optimization (no-alias, alignment, finite n, ...), it can emit a
 *   *runtime guard* and produce TWO versions of the loop: the optimized
 *   one taken when the guard succeeds, and a safe fallback otherwise.
 *
 *   The cost is code size (two loops) and one runtime test.
 *   The benefit is the optimized loop fires whenever the guard passes,
 *   which is usually almost always.
 *
 * Common applications
 * -------------------
 *   – Memcheck guards for the vectorizer (no overlap → safe to SIMD).
 *   – Alignment-versioning (peel until aligned vs. unaligned loop).
 *   – Trip-count specialisation (small n → scalar, large n → vector).
 * ============================================================================
 */
#include <stddef.h>

/*  add_arrays — pointers MIGHT alias.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): the compiler emits THREE paths:
 *
 *      add_arrays:
 *          test  rcx, rcx
 *          je    .exit              ; n == 0
 *          cmp   rcx, 12
 *          jb    .scalar_small      ; n < 12 → not worth vectorizing
 *
 *          ; --- Memcheck guard for vectorized fast path ---
 *          mov   rax, rdi
 *          sub   rax, rsi
 *          cmp   rax, 32            ; abs(a - b) < 32 ?
 *          setb  al
 *          mov   r8,  rdi
 *          sub   r8,  rdx
 *          cmp   r8,  32            ; abs(a - c) < 32 ?
 *          setb  r8b
 *          or    r8b, al
 *          je    .vec_safe          ; both far apart → safe to vectorize
 *
 *      .scalar_small:               ; small n OR aliased
 *          xor   eax, eax
 *      .scalar_body:
 *          mov   r10d, [rdx + 4*r8]
 *          add   r10d, [rsi + 4*r8]
 *          mov   [rdi + 4*r8], r10d
 *          inc   r8
 *          dec   r9
 *          jne   .scalar_body
 *          ; ...
 *
 *      .vec_safe:                   ; vectorized body using SSE
 *          ; ... 4-way packed adds ...
 *
 *  WHY: The optimizer can't prove `a`, `b`, `c` are disjoint. Instead
 *  of conservatively scalarizing, it emits a runtime memcheck that
 *  picks the vectorized path most of the time (real callers rarely
 *  pass aliasing pointers).
 */
void add_arrays(int *a, const int *b, const int *c, size_t n) {
    for (size_t i = 0; i < n; i++) a[i] = b[i] + c[i];
}

/*  add_arrays_restrict — restrict eliminates the runtime check.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Same source body as above. The asm omits the memcheck branch
 *  entirely; the compiler emits a single vector loop + scalar
 *  epilogue. Smaller code, one fewer compare per call.
 */
void add_arrays_restrict(int * restrict a,
                         const int * restrict b,
                         const int * restrict c,
                         size_t n) {
    for (size_t i = 0; i < n; i++) a[i] = b[i] + c[i];
}

/*  aligned_or_not — vectorizer's alignment versioning.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Depending on the cost model, the compiler may emit:
 *    – a scalar prologue that handles misaligned head iterations,
 *    – an aligned vector loop body,
 *    – a scalar epilogue for the tail.
 *
 *  On modern Intel/AMD the unaligned `movups`/`vmovdqu` is essentially
 *  free for naturally-aligned data, so the prologue is often skipped.
 *  Older CPUs (pre-Nehalem) penalised unaligned loads, hence the
 *  conservative scheme.
 */
void aligned_or_not(float *a, const float *b, size_t n) {
    for (size_t i = 0; i < n; i++) a[i] = a[i] + b[i];
}
