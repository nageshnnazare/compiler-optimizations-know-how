/* 05_alignment.c — ALIGNMENT, PEELING, AND RUNTIME CHECKS
 * ============================================================================
 *
 * Why alignment matters
 * ---------------------
 *   – Aligned loads (`vmovaps`) require the address to be naturally
 *     aligned to the vector width; misaligned segfaults on older CPUs.
 *   – Unaligned loads (`vmovups`) ALWAYS work; on Sandy Bridge and
 *     later they have the same throughput as aligned, so the difference
 *     is essentially zero on modern x86.
 *   – On older CPUs (pre-Nehalem, in-order ARM, etc.) misaligned loads
 *     can be 2-4× slower.
 *
 * What the optimizer does
 * -----------------------
 *   1. If alignment is known (global with `__attribute__((aligned))`,
 *      `__builtin_assume_aligned`, alloc'd via aligned_alloc) → emit
 *      aligned loads directly.
 *   2. If alignment is unknown → emit unaligned loads (modern targets)
 *      OR emit a scalar peel + aligned main + scalar tail (old targets,
 *      or cost-model says alignment matters).
 *
 * Pass names
 * ----------
 *   LLVM: LoopVectorize handles peeling; Alignment Tracker prop.
 *   GCC : tree-vect-loop-manip.cc (peeling).
 * ============================================================================
 */
#include <stddef.h>
#include <stdlib.h>

/*  aligned_unknown — `a` could be anything; loop uses UNALIGNED loads.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3 -mavx2):
 *      .vec_body:
 *          vmovups  ymm1, [rdi + 4*rcx]            ; UNALIGNED
 *          vmovups  ymm2, [rdi + 4*rcx + 32]
 *          vmovups  ymm3, [rdi + 4*rcx + 64]
 *          vmovups  ymm4, [rdi + 4*rcx + 96]
 *          vaddps   ymm1, ymm1, ymm1                ; *2
 *          vaddps   ymm1, ymm1, ymm0                ; +1.0 (ymm0 broadcast)
 *          ; ... three more ...
 *          add      rcx, 32
 *
 *  WHY UNALIGNED IS FINE: on a modern x86 the cost of `vmovups` ≈
 *  `vmovaps` for naturally-aligned data. The compiler skips the
 *  alignment peel because the cost model says it's not worth it.
 */
void aligned_unknown(float *a, size_t n) {
    for (size_t i = 0; i < n; i++) a[i] = a[i] * 2.0f + 1.0f;
}

/*  aligned_64 — `__builtin_assume_aligned` promises 64-byte alignment.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Behaviorally identical to `aligned_unknown` on modern x86 (the
 *  hardware doesn't care), but on AVX-512 and on some ARM cores the
 *  compiler will emit ALIGNED loads (`vmovaps` / `ldr q ...` with the
 *  aligned indicator), which are 1-cycle faster on the latency-bound
 *  edge cases.
 *
 *  TROUBLE: if `a` is NOT actually 64-byte aligned, the program will
 *  SIGSEGV. `__builtin_assume_aligned` is a PROMISE, not a check.
 */
void aligned_64(float *a, size_t n) {
    a = __builtin_assume_aligned(a, 64);
    for (size_t i = 0; i < n; i++) a[i] = a[i] * 2.0f + 1.0f;
}

/*  touch_global — global with attribute((aligned(64))); aligned loads emitted.
 *  ──────────────────────────────────────────────────────────────────────────
 *  G is placed at a 64-byte boundary by the linker. The compiler knows
 *  the alignment at compile time and emits aligned vector ops without
 *  any runtime check or peel.
 */
__attribute__((aligned(64))) float G[1024];

void touch_global(size_t n) {
    for (size_t i = 0; i < n; i++) G[i] = G[i] * 2.0f + 1.0f;
}

/*  make_aligned — allocate from the heap with guaranteed alignment.
 *  ──────────────────────────────────────────────────────────────────────────
 *  `aligned_alloc(64, n)` returns a pointer guaranteed to be 64-byte
 *  aligned. Use this when allocating buffers for SIMD work. The C11
 *  standard requires the size to be a multiple of the alignment;
 *  posix_memalign() is the slightly-easier-to-use alternative.
 */
float *make_aligned(size_t n) {
    return (float *)aligned_alloc(64, n * sizeof(float));
}
