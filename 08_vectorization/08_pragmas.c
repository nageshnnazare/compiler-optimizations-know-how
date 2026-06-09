/* 08_pragmas.c — pragmas that influence the vectorizer
 *
 * Use them when the compiler is being too conservative AND you have a
 * proof of correctness in your head.  Misusing them can produce silent
 * wrong-results.
 *
 *   GCC ivdep                  : "no loop-carried memory dependencies"
 *   Clang loop vectorize(...)  : enable/disable vectorize
 *   Clang loop interleave(...) : enable/disable interleave (unroll-and-jam)
 *   OpenMP simd                : portable directive across compilers
 */
#include <stddef.h>

/*  with_ivdep — promise no loop-carried memory deps; vectorizer fires.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3 -mavx2): the runtime memcheck is GONE (because of the
 *  pragma/ivdep) and only the vector main + scalar tail remain. With
 *  AVX2 the body uses ymm registers:
 *      .vec_body:
 *          vmovdqu  ymm1, [rsi + 4*rcx]
 *          vpaddd   ymm1, ymm1, ymm1            ; 2x
 *          vpaddd   ymm1, ymm1, [rsi + 4*rcx]   ; +b[i] = 3x
 *          vpaddd   ymm1, ymm1, ymm0            ; +1
 *          vmovdqu  [rdi + 4*rcx], ymm1
 *          ; ... 3 more interleaved chunks ...
 *
 *  WARNING: if you LIE to the compiler (pointers actually alias), you
 *  get silent wrong results. Use only when you have a proof.
 */
void with_ivdep(int *a, const int *b, size_t n) {
#if defined(__clang__)
#  pragma clang loop vectorize(enable) interleave(enable)
#elif defined(__GNUC__)
#  pragma GCC ivdep
#endif
    for (size_t i = 0; i < n; i++) a[i] = b[i] * 3 + 1;
}

void with_omp_simd(float *a, const float *b, size_t n) {
#pragma omp simd
    for (size_t i = 0; i < n; i++) a[i] = b[i] * 3.0f + 1.0f;
}

/*  omp_simd_reduction — float reduction without -ffast-math.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3 -fopenmp-simd -mavx2): the FP reduction VECTORIZES even
 *  without -ffast-math, because `reduction(+:s)` declares the
 *  reduction is allowed to reassociate.
 *
 *  WHY: the pragma is an explicit, scoped grant of associative-add for
 *  THIS loop. Cleaner than -ffast-math (which is global and can hurt
 *  other parts of the program).
 */
void omp_simd_reduction(const float *a, size_t n, float *out) {
    float s = 0.0f;
#pragma omp simd reduction(+:s)
    for (size_t i = 0; i < n; i++) s += a[i];
    *out = s;
}

/* Force NO vectorization. */
void scalar_only(int *a, const int *b, size_t n) {
#if defined(__clang__)
#  pragma clang loop vectorize(disable) interleave(disable)
#elif defined(__GNUC__)
#  pragma GCC novector
#endif
    for (size_t i = 0; i < n; i++) a[i] = b[i] + 1;
}
