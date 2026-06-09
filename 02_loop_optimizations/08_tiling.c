/* 08_tiling.c — LOOP TILING / BLOCKING
 * ============================================================================
 *
 * Why
 * ---
 *   Matrix multiplication is the canonical example: the working set is
 *   O(N²) per matrix but you only touch O(T²) elements of B per inner
 *   pass when blocked. With T=32, each tile is 32×32×8 B = 8 KB → fits
 *   easily in L1 D-cache.
 *
 *   Reuse per element BEFORE tiling:    ~1
 *   Reuse per element AFTER tiling:     ~T
 *   For T = 32, that's a 32× drop in DRAM traffic.
 *
 *     Big A * B = C tile layout:
 *     ┌──┬──┬──┬──┐  *  ┌──┬──┬──┬──┐  =  ┌──┬──┬──┬──┐
 *     │  │  │  │  │     │  │  │  │  │     │T │T │T │T │
 *     ├──┼──┼──┼──┤     ├──┼──┼──┼──┤     ├──┼──┼──┼──┤
 *     │  │  │  │  │     │  │  │  │  │     │T │T │T │T │  ← compute one
 *     ├──┼──┼──┼──┤     ├──┼──┼──┼──┤     ├──┼──┼──┼──┤    C-tile fully
 *     │  │  │  │  │     │  │  │  │  │     │T │T │T │T │    before moving
 *     └──┴──┴──┴──┘     └──┴──┴──┴──┘     └──┴──┴──┴──┘    on
 *
 * Where it lives
 * --------------
 *   Off by default in both -O3 pipelines. Need:
 *     LLVM:  -mllvm -polly  (out-of-tree but in many distros)
 *     GCC :  -floop-nest-optimize -fgraphite-identity
 *   Otherwise hand-tile.
 * ============================================================================
 */

#define N 512
#define T 32

/*  matmul_naive — the textbook triple loop.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): the inner k-loop vectorizes nicely (innermost stride-1
 *  on A and stride-N on B), but the B accesses thrash the cache for
 *  large N. Naïve performance on a modern x86: a few GFLOPS — about an
 *  ORDER OF MAGNITUDE less than the same machine can do via BLAS.
 */
void matmul_naive(double C[N][N], const double A[N][N], const double B[N][N]) {
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            double s = 0;
            for (int k = 0; k < N; k++)
                s += A[i][k] * B[k][j];
            C[i][j] = s;
        }
}

/*  matmul_tiled — six-deep nest hand-blocked.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): the inner i/j/k triple still vectorizes; the outer
 *  ii/jj/kk triple bounds the tile size that lives in L1. On a typical
 *  desktop x86, this version is 2-4× faster than `matmul_naive` for
 *  N=512.
 *
 *  WHAT YOU MIGHT EXPECT WITHOUT TILING: compiler auto-tiling. It
 *  rarely happens because:
 *    – The cost model has too many free variables (cache size, etc.).
 *    – Wrong tiling can make code SLOWER (e.g. T=1 is just naïve again).
 *    – Existing tools (Polly, Graphite) compute tile sizes from the
 *      target's cache parameters but are still off by default.
 *
 *  The lesson: for math kernels, tile by hand and let the compiler
 *  vectorize the inner loops. (Or better: use a BLAS library.)
 */
void matmul_tiled(double C[N][N], const double A[N][N], const double B[N][N]) {
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            C[i][j] = 0.0;

    for (int ii = 0; ii < N; ii += T)
        for (int jj = 0; jj < N; jj += T)
            for (int kk = 0; kk < N; kk += T)
                for (int i = ii; i < ii + T; i++)
                    for (int j = jj; j < jj + T; j++) {
                        double s = C[i][j];
                        for (int k = kk; k < kk + T; k++)
                            s += A[i][k] * B[k][j];
                        C[i][j] = s;
                    }
}
