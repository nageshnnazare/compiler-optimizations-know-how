/* 01_pgo_demo.c — minimal PGO demo
 *
 * Build (Clang):
 *   clang -O3 -fprofile-generate=./pgo -o pgo_demo_inst 01_pgo_demo.c
 *   ./pgo_demo_inst 10000000 1
 *   llvm-profdata merge -output=pgo/default.profdata pgo/
 *   clang -O3 -fprofile-use=pgo/default.profdata -o pgo_demo_opt 01_pgo_demo.c
 *   objdump -d pgo_demo_opt | wc -l
 *
 * Build (GCC):
 *   gcc -O3 -fprofile-generate -o pgo_demo_inst 01_pgo_demo.c
 *   ./pgo_demo_inst 10000000 1            # writes 01_pgo_demo.gcda
 *   gcc -O3 -fprofile-use -o pgo_demo_opt 01_pgo_demo.c
 *
 * The expected effect: when run with a heavily-skewed input (1 vs 999999
 * occurrences), the compiler lays out the hot branch as fall-through.
 * Compare `pgo_demo_inst` vs `pgo_demo_opt` with objdump.
 */
#include <stdio.h>
#include <stdlib.h>

__attribute__((noinline))
static int hot(int x)  { return x + 1; }

__attribute__((noinline))
static int cold(int x) { return x * 1000003 ^ 0xdeadbeef; }

int main(int argc, char **argv) {
    long n = (argc > 1) ? atol(argv[1]) : 1000000;
    int  c = (argc > 2) ? atoi(argv[2]) : 1;   /* 1 = "hot path very rare" */
    long s = 0;
    for (long i = 0; i < n; i++) {
        if ((i % 1000000) < c) s += cold((int)i);
        else                   s += hot((int)i);
    }
    printf("%ld\n", s);
    return 0;
}
