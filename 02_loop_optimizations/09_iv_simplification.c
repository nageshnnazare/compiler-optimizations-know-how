/* 09_iv_simplification.c — INDUCTION VARIABLE OPTIMIZATION
 * ============================================================================
 *
 * Idea
 * ----
 *   An *induction variable* (IV) of a loop is a value whose change per
 *   iteration is constant (modulo the trip count). Examples:
 *     i in `for (i=0;i<n;i++)`        (step +1)
 *     p in `for (p=a;p<a+n;p++) ...`  (step +sizeof(*p))
 *
 *   IVOPT (LLVM's IndVarSimplifyPass, GCC's ivopts) does:
 *     1. CANONICALIZE   reduce to a single i64 counter where possible
 *     2. STRENGTH REDUCE  replace `a[i*c]` with a pointer that steps by c
 *     3. LFTR              rewrite the exit test in terms of the new IV
 *     4. ELIMINATE         drop redundant IVs (one pointer drives two arrays)
 *
 * Modeled by Scalar Evolution (SCEV) / GCC tree-scalar-evolution.cc, a
 * closed-form description of `value(i) = base + i * step`.
 * ============================================================================
 */
#include <stddef.h>

/*  sum_strided — `a[i*4 + 8]` becomes a pointer increment of 16.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3) main loop body (4-way unrolled, base offset already
 *  added):
 *      .vec:
 *          add  eax, [r8 - 48]
 *          add  eax, [r8 - 32]
 *          add  eax, [r8 - 16]
 *          add  eax, [r8       ]
 *          add  rdx, 4
 *          add  r8,  64            ; pointer step = 4 iters * 16 bytes
 *          cmp  rsi, rdx
 *          jne  .vec
 *
 *  WHAT WE'D EXPECT WITHOUT IVOPT: each iteration computes
 *  `r8 = a + i*16 + 32`, requiring an extra multiply per iter.
 *
 *  WHY: IVOPT recognises the linear address pattern via SCEV. It
 *  rewrites the loop in terms of a single pointer p that starts at
 *  `a + 8 * sizeof(int) = a + 32` and increments by 16 each iter. No
 *  multiply in the body; just an `add r8, 16`. (After unrolling x4,
 *  the increment is +64.)
 */
int sum_strided(const int *a, size_t n) {
    int s = 0;
    for (size_t i = 0; i < n; i++) s += a[i * 4 + 8];
    return s;
}

/*  dual_index — two parallel index computations collapse.
 *  ──────────────────────────────────────────────────────────────────────────
 *  After IVOPT both `a[i*2]` and `b[i*2+1]` collapse to two pointers
 *  that each step by 8 (= 2 * sizeof(int)) per iteration. No multiply,
 *  no separate index counter — the loop closes back on the END of one
 *  of the pointers.
 */
void dual_index(int *a, int *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        a[i * 2]     = (int)i;
        b[i * 2 + 1] = (int)i * 3;
    }
}

/*  sum_with_check — redundant bound check removed by range analysis.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source has `if (i < n) s += a[i];` inside a loop bounded by `i < n`.
 *  Trivially redundant. SCEV proves `i < n` is loop-invariant (always
 *  true within the loop), and the inner `if` collapses → straight-line
 *  accumulator loop.
 *
 *  ACTUAL (-O3): identical asm to a plain sum loop. The wasted check
 *  is GONE.
 */
int sum_with_check(const int *a, size_t n) {
    int s = 0;
    for (size_t i = 0; i < n; i++) {
        if (i < n) s += a[i];
    }
    return s;
}
