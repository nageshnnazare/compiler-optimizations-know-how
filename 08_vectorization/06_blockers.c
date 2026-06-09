/* 06_blockers.c — WHAT STOPS THE VECTORIZER, AND HOW TO FIX IT
 * ============================================================================
 *
 * For each pattern: the failing reason and the fix.
 *
 * Diagnose with the optimizer remarks:
 *   clang -O3 -Rpass-missed=loop-vectorize \
 *             -Rpass-analysis=loop-vectorize \
 *             -c 06_blockers.c -o /dev/null
 *   gcc   -O3 -fopt-info-vec-missed -c 06_blockers.c -o /dev/null
 * ============================================================================
 */
#include <stddef.h>
#include <math.h>

/*  (1) carried — `a[i] = a[i-1] + 1` is a serial recurrence.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): the inner loop is unrolled but NOT vectorized:
 *      .body:
 *          lea  esi, [rax + rdx]
 *          inc  esi
 *          mov  [rdi + 4*rdx + 4], esi
 *          ; ... more scalar iterations ...
 *
 *  WHY: SCEV recognises that `a[i] = a[0] + i`, which is a closed form.
 *  The compiler unrolls but every store still depends on the previous.
 *
 *  FIX: rewrite the algorithm to be parallel:
 *      int v = a[0];
 *      for (i=1;i<n;i++) a[i] = v + i;
 *  Now `i` is the IV; the writes are independent → vectorizable.
 */
void carried(int *a, size_t n) {
    for (size_t i = 1; i < n; i++) a[i] = a[i - 1] + 1;
}

/*  (2) indirect — `a[idx[i]]` requires a gather instruction.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3 -mavx2): might emit `vpgatherdd` if cost model allows;
 *  more often emits a scalar loop because gather is expensive (slow on
 *  Intel pre-AVX-512, ~10-20 cycles for 8 lanes).
 *
 *  ACTUAL (-O3 -mavx512f): cleaner gather path.
 *
 *  FIX: if `idx` is sortable, sort the access pattern and use a stride.
 *  If you control the layout, use SOA (struct-of-arrays) to make the
 *  inner access stride-1.
 */
void indirect(int *out, const int *in, const int *idx, size_t n) {
    for (size_t i = 0; i < n; i++) out[i] = in[idx[i]];
}

/*  (3) find_first — early exit prevents vectorization.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): scalar loop with a conditional `je .return`. The
 *  vectorizer needs a single-exit shape and can't tolerate the
 *  data-dependent break.
 *
 *  FIX 1: use a vectorized intrinsic-based search (memchr for bytes).
 *  FIX 2: process the whole array, then find the first match via a
 *  separate reduction step (acceptable when n is small).
 */
int find_first(const int *a, size_t n, int v) {
    for (size_t i = 0; i < n; i++) if (a[i] == v) return (int)i;
    return -1;
}

/*  (4) opaque_in_body — unknown call blocks vectorization.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): scalar loop with `call opaque` each iteration.
 *
 *  FIX: declare opaque `__attribute__((const))` (no memory access).
 *  Then the body is `a[i] = const_func(a[i])`. The vectorizer still
 *  can't vectorize calls in general (no SIMD ABI), but at least it
 *  CSEs / fuses the surrounding code.
 *
 *  BETTER FIX: inline the function (visible body or LTO).
 *  ALTERNATIVE: write a vector version with `__attribute__((target("avx2")))`
 *  and use `#pragma omp simd` or intrinsics.
 */
extern int opaque(int);
void opaque_in_body(int *a, size_t n) {
    for (size_t i = 0; i < n; i++) a[i] = opaque(a[i]);
}

/*  (5) may_alias — no restrict → runtime memcheck + dual loops.
 *  ──────────────────────────────────────────────────────────────────────────
 *  See 02_loop_optimizations/12_versioning.c. The fix is to add
 *  `restrict` to the pointer parameters.
 */
void may_alias(int *a, const int *b, const int *c, size_t n) {
    for (size_t i = 0; i < n; i++) a[i] = b[i] + c[i];
}

/*  (6) reduce_float — fp reduction needs -ffast-math to vectorize.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): scalar `addss` chain.
 *  ACTUAL (-O3 -ffast-math): full 4-accumulator `vaddps` vectorized
 *  reduction (see 02_reduction.c).
 *
 *  FIX: -ffast-math, or per-loop:
 *      #pragma omp simd reduction(+:s)
 *  enables associative-add for THIS loop only.
 */
float reduce_float(const float *a, size_t n) {
    float s = 0.0f;
    for (size_t i = 0; i < n; i++) s += a[i];
    return s;
}

/*  (7) math_in_body — sinf() doesn't have a vector implementation.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): scalar `call sinf` each iteration.
 *
 *  FIX: use a SIMD math library:
 *    gcc:   -mveclibabi=svml  -lmvec  (uses Intel SVML)
 *           -fopenmp-simd     (libm uses _ZGVbN4v_sinf etc.)
 *    clang: -fveclib=libmvec  (or =SVML, =SLEEF depending on link target)
 *  With one of these, the vectorizer rewrites the call to a SIMD-math
 *  helper that processes 4 or 8 floats per call.
 */
void math_in_body(float *a, size_t n) {
    for (size_t i = 0; i < n; i++) a[i] = sinf(a[i]);
}
