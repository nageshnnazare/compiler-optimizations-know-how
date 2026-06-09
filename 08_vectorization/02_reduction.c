/* 02_reduction.c — REDUCTIONS (sum, max, hash)
 * ============================================================================
 *
 * Idea
 * ----
 *   A reduction loop accumulates many values into one scalar:
 *     int s = 0; for (i) s += a[i];
 *
 *   Vectorized form uses K parallel partial accumulators:
 *
 *       acc0 = acc1 = acc2 = acc3 = 0           ; 4 parallel sums
 *       for (i = 0; i + 16 <= n; i += 16) {
 *           acc0 += vload(a + i);
 *           acc1 += vload(a + i + 4);
 *           acc2 += vload(a + i + 8);
 *           acc3 += vload(a + i + 12);
 *       }
 *       s = horizontal_sum(acc0 + acc1 + acc2 + acc3)
 *
 * INTEGER reductions: always vectorize, ALWAYS (integer add is exactly
 *   associative).
 *
 * FLOAT reductions: vectorize ONLY with -ffast-math, because the
 *   parallel partial-sum tree changes the rounding order, which can
 *   give DIFFERENT results (last-bit differences are common).
 *
 * Why bother?
 * -----------
 *   – Integer add: 1 cycle latency; serial chain bound by latency.
 *     With 4 parallel acc's we get 4 ops per cycle (ILP).
 *   – With AVX2 we further process 8 ints per op → 32 ints/cycle
 *     peak, vs 1 for the naive scalar chain.
 * ============================================================================
 */
#include <stddef.h>
#include <stdint.h>

/*  sum_int — integer reduction; ALWAYS vectorized.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3 -mavx2): main body uses FOUR parallel YMM accumulators.
 *      .vec:
 *          vpxor   xmm0, xmm0, xmm0          ; 4× zero accumulators
 *          vpxor   xmm1, xmm1, xmm1
 *          vpxor   xmm2, xmm2, xmm2
 *          vpxor   xmm3, xmm3, xmm3
 *      .vec_body:                              ; 32 ints per iter
 *          vpaddd  ymm0, ymm0, [rdi + 4*rax]
 *          vpaddd  ymm1, ymm1, [rdi + 4*rax + 32]
 *          vpaddd  ymm2, ymm2, [rdi + 4*rax + 64]
 *          vpaddd  ymm3, ymm3, [rdi + 4*rax + 96]
 *          add     rax, 32
 *          cmp     rcx, rax
 *          jne     .vec_body
 *      ; horizontal reduce ymm0..ymm3 down to a scalar in eax
 *
 *  KEY POINT: the four ymm accumulators are INDEPENDENT — they form
 *  four parallel partial sums, exploiting instruction-level
 *  parallelism. On modern x86 you have 4 SIMD ALU ports → 4 vpaddd's
 *  per cycle. The serial-chain "naive" form bounds at 1 add/cycle.
 *  Speedup: 4 × 8 = 32× on the critical path, in addition to the
 *  per-element SIMD win.
 */
int sum_int(const int *a, size_t n) {
    int s = 0;
    for (size_t i = 0; i < n; i++) s += a[i];
    return s;
}

/*  sum_float — float reduction; needs -ffast-math (or equivalent) to vec.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3, NO fast-math): SCALAR loop:
 *      .scalar:
 *          addss  xmm0, dword ptr [rdi + 4*rax]
 *          inc    rax
 *          cmp    rsi, rax
 *          jne    .scalar
 *
 *  ACTUAL (-O3 -ffast-math): the same beautiful vectorized form as
 *  sum_int, but with `vaddps ymm0, ymm0, [...]`.
 *
 *  WHY DEFAULT BLOCKS IT: parallel partial sums change the *order* of
 *  floating-point additions. `(a+b)+c` may round differently from
 *  `a+(b+c)`. Default flags must preserve scalar order; -ffast-math
 *  (or specifically -fassociative-math + -fno-trapping-math) opts in.
 */
float sum_float(const float *a, size_t n) {
    float s = 0;
    for (size_t i = 0; i < n; i++) s += a[i];
    return s;
}

/*  max_int — max reduction; ALWAYS vectorized.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3 -mavx2): four parallel VPMAXSD accumulators in the inner
 *  loop, then horizontal reduce. Same shape as the sum example.
 *  Integer max is associative and commutative ⇒ no flags required.
 */
int max_int(const int *a, size_t n) {
    int m = a[0];
    for (size_t i = 1; i < n; i++) if (a[i] > m) m = a[i];
    return m;
}

/*  hash_djb2 — multiplicative hash; NOT vectorizable.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): scalar loop. The dependency chain is
 *      h_next = h*33 + c
 *  which is a TIGHT serial chain across iterations. Each iteration
 *  needs the previous value of h.
 *
 *  WHY THE COMPILER CAN'T VECTORIZE: a recurrence `x[i] = f(x[i-1])`
 *  has no parallelism without rewriting the algorithm (e.g. into a
 *  polynomial in the inputs). DJB-style hashes are intentionally
 *  recursive on the running state to ensure cryptographic-like mixing.
 */
uint32_t hash_djb2(const char *s, size_t n) {
    uint32_t h = 5381;
    for (size_t i = 0; i < n; i++) h = h * 33u + (uint32_t)s[i];
    return h;
}
