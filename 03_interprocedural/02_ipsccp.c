/* 02_ipsccp.c — INTERPROCEDURAL SPARSE CONDITIONAL CONSTANT PROPAGATION
 * ============================================================================
 *
 * Idea
 * ----
 *   SCCP (chapter 04) computes constant values within a function. IPSCCP
 *   extends the analysis ACROSS function boundaries: if every call site
 *   of `f(x, flag)` passes the same value for `flag`, that parameter
 *   becomes a constant inside `f`. Branches that depended on it fold
 *   away.
 *
 *   The closure of "all calls see the same constant" only works for:
 *     – `static` functions (compiler sees ALL callers in the TU), or
 *     – functions with internal linkage after LTO joins TUs.
 *
 * Pass names
 * ----------
 *   LLVM:  IPSCCPPass.
 *   GCC :  tree-ipa-cp (IPA constant prop) + tree-ipa-prop.
 * ============================================================================
 */
extern void foo(int x);
extern void bar(int x);

static void helper(int x, int flag) {
    if (flag) foo(x);
    else      bar(x);
}

/*  caller_a, caller_b, caller_c — all three pass flag=1.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      caller_a:
 *          mov  edi, 1
 *          jmp  foo                ; TAIL CALL straight to foo(1)
 *      caller_b:
 *          mov  edi, 2
 *          jmp  foo                ; tail call foo(2)
 *      caller_c:
 *          mov  edi, 3
 *          jmp  foo                ; tail call foo(3)
 *
 *  WHAT WE'D EXPECT WITHOUT IPSCCP:
 *      caller_a:
 *          mov  edi, 1            ; x
 *          mov  esi, 1            ; flag
 *          jmp  helper
 *
 *  WHY: IPSCCP analyses helper and finds:
 *    – `x` has different values at different calls → lattice ⊥
 *    – `flag` is always 1 → lattice 1
 *  It then specialises helper assuming `flag == 1`; the `else` branch is
 *  pruned and helper becomes `foo(x)`. Now the call is a 1-statement
 *  forwarder, which the inliner happily inlines into each caller. The
 *  result: each caller directly tail-calls `foo`, the helper function
 *  is eliminated, and the `bar` symbol need not be referenced at all.
 *
 *  Total wins:
 *    – fewer instructions (no helper prologue/epilogue);
 *    – `bar` may be DCE'd by the linker (chapter 03 LTO);
 *    – better branch prediction (tail-call, no return mispredict).
 */
void caller_a(void) { helper(1, 1); }
void caller_b(void) { helper(2, 1); }
void caller_c(void) { helper(3, 1); }
