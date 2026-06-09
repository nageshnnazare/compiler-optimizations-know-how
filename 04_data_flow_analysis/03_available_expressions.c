/* 03_available_expressions.c — AVAILABLE EXPRESSIONS ANALYSIS
 * ============================================================================
 *
 * An expression `e` is AVAILABLE at point p iff EVERY path from entry to
 * p computes e and no operand of e is redefined between that computation
 * and p.
 *
 *      t1 = a * b;            ;; BB1: GEN={a*b}  KILL={ exprs using a OR b }
 *           │
 *           ▼
 *      use of (a*b)           ;; BB2: a*b ∈ IN[BB2] → replace use with t1
 *
 *   Forward, MUST (∩) — fact survives a join only if it holds on ALL paths.
 *      IN[B] = ⋂ OUT[P]  over predecessors P
 *      OUT[B] = GEN[B] ∪ (IN[B] \ KILL[B])
 *
 * Used by CSE / GVN. If e ∈ IN[p] and there's a temporary t holding it,
 * the use of e at p is replaced by t.
 * ============================================================================
 */

/*  cse_friendly — `a*b` is available at the second use → CSE'd.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source:
 *      t1 = a*b + 1;
 *      t2 = a*b + 2;            ; a, b not redefined → a*b is available
 *      return t1 + t2;          ; = 2*(a*b) + 3
 *
 *  ACTUAL (-O3):
 *      cse_friendly:
 *          imul edi, esi               ; ONE multiply: a * b
 *          lea  eax, [2*rdi + 3]        ; 2*(a*b) + 3 in one lea
 *          ret
 */
int cse_friendly(int a, int b) {
    int t1 = a * b + 1;
    int t2 = a * b + 2;
    return t1 + t2;
}

/*  cse_blocked — assignment to `a` KILLs availability of `a*b`.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source:
 *      t1 = a*b + 1;
 *      a  = 0;                  ; KILL set adds (a*b)
 *      t2 = a*b + 2;            ; a*b is NO LONGER available; recompute
 *      return t1 + t2;
 *
 *  ACTUAL (-O3):
 *      cse_blocked:
 *          imul edi, esi               ; t1 = a*b + 1 (uses original a)
 *          lea  eax, [rdi + 3]          ; result = (a*b + 1) + 2 = a*b + 3
 *          ret
 *
 *  WHAT HAPPENED:
 *    – The compiler did NOT use a*b for t2 (kill set blocks the CSE).
 *    – Instead it computed t2 = 0*b + 2 = 2 by constant folding the
 *      *new* value of a.
 *    – Then t1 + t2 = (a*b + 1) + 2 = a*b + 3, hence the lea.
 *
 *  Without the killing assignment `a = 0`, the optimizer would have
 *  reused the previous a*b computation. Here it had to compromise:
 *  one mul + one lea instead of one mul + one lea (same cost!) because
 *  the second `a*b` collapsed to a constant.
 */
int cse_blocked(int a, int b) {
    int t1 = a * b + 1;
    a = 0;
    int t2 = a * b + 2;
    return t1 + t2;
}
