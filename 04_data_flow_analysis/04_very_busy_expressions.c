/* 04_very_busy_expressions.c — VERY-BUSY-EXPRESSIONS ANALYSIS
 * ============================================================================
 *
 * An expression e is "very busy" at p iff EVERY path FROM p computes e
 * before any operand of e is changed.
 *
 *      Entry (BB1)
 *      │
 *      if (cond)
 *      /         \
 *  BB2:         BB3:
 *  use a*b      use a*b
 *
 *   Then (a*b) is very busy at BB1 → it can be HOISTED into BB1, saving
 *   the second computation regardless of which branch is taken.
 *
 *   Backward, MUST (∩). The dual of "available expressions".
 *      OUT[B] = ⋂ IN[S]  over successors S
 *      IN [B] = USE[B] ∪ (OUT[B] \ KILL[B])
 *
 * Used by Partial Redundancy Elimination (PRE):
 *   LLVM: GVNHoist, MergedLoadStoreMotion (memory variant)
 *   GCC : tree-ssa-pre.cc
 * ============================================================================
 */

/*  hoistable — (a*b) is computed on BOTH paths.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      hoistable:
 *          imul edi, esi                ; (a*b) HOISTED to here, ONCE
 *          xor  eax, eax
 *          cmp  edx, 1
 *          setae al                     ; al = (cond != 0)
 *          lea  eax, [rdi + 2*rax]      ; (a*b) + 2*(cond != 0)
 *          dec  eax                     ; subtract 1
 *          ret
 *
 *  WHY: GVNHoist sees `a*b` is very busy at the if's join, so it pulls
 *  the computation into the dominator (the if's condition block) where
 *  it is executed exactly once. The if/else then collapses to a branch-
 *  less `select 1, -1` which the codegen lowers to a 0/1 indicator
 *  multiplied by 2 minus 1 → +1 / -1.
 */
int hoistable(int a, int b, int cond) {
    if (cond) return a * b + 1;
    else      return a * b - 1;
}

/*  not_hoistable — one arm uses a+b, the other a*b.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      not_hoistable:
 *          lea  eax, [rsi + rdi]        ; a+b  (one arm)
 *          mov  ecx, esi
 *          imul ecx, edi                ; a*b  (other arm)
 *          inc  ecx                     ; a*b + 1
 *          test edx, edx
 *          cmovne eax, ecx              ; pick a*b+1 if cond, else a+b
 *          ret
 *
 *  WHY: `a*b` is NOT very busy at entry — the else path doesn't compute
 *  it. The hoist would *speculatively* execute a multiply even when not
 *  needed. The cost model decides the saving doesn't pay (the if/else
 *  was going to lower to a CMOV anyway). Both expressions are computed
 *  speculatively before the test; CMOV picks.
 */
int not_hoistable(int a, int b, int cond) {
    if (cond) return a * b + 1;
    else      return a + b;
}
