/* 05_out_of_ssa.c — the swap problem
 *
 * Sometimes a loop produces a back-edge whose φs would, if naïvely
 * lowered, generate self-destroying copies. Example:
 *
 *      loop:
 *          x1 = phi(x0 from entry, y2 from latch)
 *          y1 = phi(y0 from entry, x2 from latch)
 *          x2 = something(x1, y1)
 *          y2 = something_else(x1, y1)
 *          br loop
 *
 * After naïve out-of-SSA you might write
 *      copy x1 ← y2
 *      copy y1 ← x2          ← but x2 was computed FROM x1 — already overwritten!
 *
 * The out-of-SSA pass detects the cycle and inserts a temporary:
 *      tmp ← x2
 *      copy x1 ← y2
 *      copy y1 ← tmp
 *
 * To witness this directly is hard in C source; the canonical case is a
 * tight swap loop:
 */
/*  swap_loop — swap a[i] and b[i] each iteration.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3, simplified): the inner loop body is
 *      mov  edx, dword ptr [rdi + 4*rcx]      ; t = a[i]
 *      mov  r8d, dword ptr [rsi + 4*rcx]      ; load b[i]
 *      mov  dword ptr [rdi + 4*rcx], r8d      ; a[i] = b[i]
 *      mov  dword ptr [rsi + 4*rcx], edx      ; b[i] = t
 *
 *  Notice the EXPLICIT temporary `edx`. The out-of-SSA pass detected
 *  that lowering the cycle of phi-copies would corrupt one value if
 *  done naively; it inserted the temp to break the cycle. This is the
 *  "swap problem" that every textbook describes; the actual code looks
 *  like the classic 4-instruction swap.
 *
 *  Note: a vectorized variant (using XMM swaps via pshufd or VPERM2I)
 *  is also possible; the cost model picks one based on n and alignment.
 */
void swap_loop(int *a, int *b, int n) {
    for (int i = 0; i < n; i++) {
        int t = a[i];
        a[i] = b[i];
        b[i] = t;
    }
}

/* Or the in-place reverse: every iteration swaps two array positions.
 * Inspect the assembly to confirm the compiler uses a real temporary. */
void reverse_array(int *a, int n) {
    int lo = 0, hi = n - 1;
    while (lo < hi) {
        int t = a[lo];
        a[lo] = a[hi];
        a[hi] = t;
        lo++; hi--;
    }
}
