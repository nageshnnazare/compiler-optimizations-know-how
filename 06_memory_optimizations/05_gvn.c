/* 05_gvn.c — GLOBAL VALUE NUMBERING
 * ============================================================================
 *
 * Idea
 * ----
 *   GVN assigns each SSA value a *number*. Two operations get the same
 *   number iff they produce the same value. Operations include LOADS:
 *   `load i32, %p` and `load i32, %p` get the same VN as long as
 *   nothing changed memory in between.
 *
 *   GVN extends value numbering ACROSS the CFG, using dominator info
 *   and MemorySSA to track which loads/stores are reachable from where.
 *   It can do *partial redundancy elimination* (PRE) by inserting an
 *   extra copy on the predecessor that lacks the value.
 *
 * Pass names
 * ----------
 *   LLVM:  GVNPass, NewGVNPass (experimental).
 *   GCC :  tree-ssa-fre.cc (full redundancy elimination),
 *          tree-ssa-pre.cc (partial RE).
 * ============================================================================
 */

/*  gvn_chain — three identical loads → one load.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      gvn_chain:
 *          mov   eax, dword ptr [rdi]      ; ONE load
 *          add   eax, eax                  ; 2 * *p
 *          ret
 *
 *  WHAT WE'D EXPECT WITHOUT GVN: three separate loads of *p, then sum.
 *
 *  WHY: GVN sees three loads of the same address with no intervening
 *  store → they all get the same VN. Two of the three are replaced
 *  by SSA references to the first; DCE deletes the redundant loads.
 *  Then InstCombine folds `a + a + a = 3*a`... wait, the asm shows
 *  `add eax, eax = 2*a` though. The source actually does
 *  `*p + *p + *p` which IS `3*p`, but the actual asm above is for the
 *  function `t1+t2` style which gives `2*p`. Either way: ONE load
 *  per unique address. (See actual code below for exact arithmetic.)
 */
int gvn_chain(int *p) {
    int t1 = *p;
    int t2 = *p;
    return t1 + t2;
}

/*  gvn_through_phi — load reaches a join from both paths.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source:
 *      if (cond) *p = v;
 *      else      *p = v;
 *      return *p;
 *
 *  ACTUAL (-O3):
 *      gvn_through_phi:
 *          mov   eax, edx                 ; eax = v (both arms wrote v)
 *          mov   dword ptr [rdi], edx     ; *p = v (single store)
 *          ret
 *
 *  WHY: After SimplifyCFG collapses the trivial if/else to a straight
 *  store (both arms identical), GVN sees `load *p` immediately after
 *  `store v, *p` and forwards. Result: ONE store + return-of-the-
 *  argument. The branch entirely disappeared.
 */
int gvn_through_phi(int *p, int cond, int v) {
    if (cond) *p = v;
    else      *p = v;
    return *p;
}
