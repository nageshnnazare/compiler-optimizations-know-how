/* 07_interchange.c — LOOP INTERCHANGE
 * ============================================================================
 *
 * Why
 * ---
 *   C arrays are stored ROW-MAJOR: A[i][0], A[i][1], …, A[i][N-1] are
 *   contiguous; A[0][j], A[1][j], … are N*sizeof(T) bytes apart.
 *
 *   Striding by COLUMNS in the inner loop means the cache pulls in 64
 *   bytes (one cache line) of which you use ~4. Stride-by-rows uses all
 *   of it.
 *
 *     A[i][j] memory layout:                  cache line (64 B / 16 ints)
 *     ┌──────────────────────────────────┐    ┌──────────────────────────┐
 *     │ A[0][0] A[0][1] A[0][2] ...      │    │ 16 contiguous A[0][...]  │
 *     │ A[1][0] A[1][1] A[1][2] ...      │    └──────────────────────────┘
 *     │ A[2][0] A[2][1] A[2][2] ...      │
 *     └──────────────────────────────────┘
 *
 * Pass names
 * ----------
 *   LLVM:  LoopInterchangePass (off by default in older versions; needs
 *          -mllvm -enable-loopinterchange in some toolchains).
 *   GCC :  graphite-interchange.cc (-floop-nest-optimize).
 * ============================================================================
 */
#include <stddef.h>

#define N 1024

/*  bad_order — INNER loop strides by N elements.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): the compiler DID NOT interchange. Look at the inner-
 *  loop body:
 *      mov  [rdi + r8 - 12288], r9d              ; stride 4096 between
 *      mov  [rdi + r8 -  8192], r10d              ; consecutive stores
 *      mov  [rdi + r8 -  4096], r10d
 *      mov  [rdi + r8        ], r10d
 *      add  r8, 16384                             ; += 16 KB per inner iter
 *
 *  That's right — every inner iteration advances by 16 KB, touching a
 *  fresh cache line each time. Memory bandwidth bound.
 *
 *  WHY NOT INTERCHANGE: LoopInterchange is off by default at -O3 in
 *  LLVM (cost-model heuristic conservative; risks of regression on
 *  small N). With `-mllvm -enable-loopinterchange` it would swap the
 *  two loops, restoring stride-1 access.
 */
void bad_order(int A[N][N]) {
    for (int j = 0; j < N; j++) {
        for (int i = 0; i < N; i++) {
            A[i][j] = i * j;
        }
    }
}

/*  good_order — already the right order.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Inner loop strides by 4 bytes. The vectorizer LOVES this and SIMD-
 *  ifies the inner loop trivially.
 *
 *  Lesson: choose the right loop order yourself. The compiler will
 *  rarely save you.
 */
void good_order(int A[N][N]) {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            A[i][j] = i * j;
        }
    }
}

/*  transpose_naive — has BAD access pattern in one matrix no matter what.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Inherent to matrix transpose: one matrix is stride-1, the other is
 *  stride-N. Tiling (block transpose) is the standard fix:
 *
 *      for (int ii = 0; ii < N; ii += T)
 *        for (int jj = 0; jj < N; jj += T)
 *          for (int i = ii; i < ii + T; i++)
 *            for (int j = jj; j < jj + T; j++)
 *              dst[j][i] = src[i][j];
 *
 *  Inside a T×T tile both src and dst stay in L1. The compiler's
 *  polyhedral framework (Polly / Graphite) does this; the in-tree
 *  -O3 pipeline does NOT.
 */
void transpose_naive(int dst[N][N], const int src[N][N]) {
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            dst[j][i] = src[i][j];
}
