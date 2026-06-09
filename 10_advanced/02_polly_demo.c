/* 02_polly_demo.c — a polyhedral-friendly loop nest
 *
 * Try (Linux; needs an LLVM with Polly enabled):
 *   clang -O3 -mllvm -polly -mllvm -polly-omp-backend=LLVM \
 *         -mllvm -polly-parallel -S 02_polly_demo.c -o 02_polly_demo.s
 *
 * GCC graphite:
 *   gcc -O3 -floop-nest-optimize -floop-parallelize-all -ftree-parallelize-loops=4 \
 *       -S 02_polly_demo.c -o 02_polly_demo.s
 *
 * The interesting transformation is interchange + tiling + parallel.
 */
#define N 1024
void mm(double C[N][N], const double A[N][N], const double B[N][N]) {
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            for (int k = 0; k < N; k++)
                C[i][j] += A[i][k] * B[k][j];
}
