/* 05_tail_call.c — TAIL CALL ELIMINATION (TCE / Sibling Calls)
 * ============================================================================
 *
 * What it is
 * ----------
 *   `... ; call f ; ret` is replaced by `... ; jmp f`. The caller's
 *   stack frame is REUSED by the callee. Tail-recursive calls become
 *   loops with NO stack growth.
 *
 *   "Sibling calls" are the same transformation across DIFFERENT
 *   functions (not just recursion) — possible when the ABIs match.
 *
 * Why it matters
 * --------------
 *   – Constant stack usage for tail-recursive algorithms (no SIGSEGV).
 *   – One fewer call/ret pair → better Return-Address-Stack accuracy →
 *     better branch prediction.
 *   – Often enables a return to be predicted directly to the next
 *     instruction after the original *grand*caller's call site.
 *
 * Pass names
 * ----------
 *   LLVM:  TailCallElimPass (early) + backend MachineTailCall (final).
 *   GCC :  -foptimize-sibling-calls (on by default at -O2+).
 *
 * Requirements
 * ------------
 *   – Caller and callee ABIs are compatible (same return type / cleanup).
 *   – The call's return value flows DIRECTLY to caller's return.
 *   – No destructors / cleanup in scope (C++).
 *   – No `va_list` passed by reference.
 * ============================================================================
 */

/*  fact_tail — tail-recursive factorial becomes a LOOP.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): the function is no longer recursive at the asm level.
 *  Instead the compiler:
 *    1. TCE'd the recursion → loop.
 *    2. Recognised the multiplicative reduction.
 *    3. VECTORIZED the loop using SSE pmulld, processing 8 elements per
 *       iteration with 4-wide vector multiplications.
 *
 *  The relevant labels are:
 *      fact_tail:
 *          ...           ; jump to vectorized loop
 *      LBB0_4:           ; main vector loop (×8 elements per iter)
 *          pmulld xmm1, xmm5
 *          pmulld xmm0, xmm2
 *          ...
 *      LBB0_6:           ; scalar tail loop (residual <8 iterations)
 *      LBB0_7:
 *          ret
 *
 *  WHAT WE'D EXPECT WITHOUT TCE: a stack-eating recursive call:
 *      fact_tail:
 *          test edi, edi
 *          je   .base
 *          imul esi, edi
 *          dec  edi
 *          call fact_tail        ; ← recursion, frame per call
 *          ret
 *      .base:
 *          mov  eax, esi
 *          ret
 *
 *  WHY: After TCE the IR is a loop `while (n) { acc *= n; n--; }`. SCEV
 *  + LoopVectorize recognise the multiplicative reduction and SIMD-ify.
 *  The "function call" is gone; the "loop" is vectorized. Two
 *  optimizations stacked.
 */
int fact_tail(int n, int acc) {
    if (n == 0) return acc;
    return fact_tail(n - 1, acc * n);
}

/*  sibling — tail call across functions.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      sibling:
 *          jmp  _other            ; TAILCALL
 *
 *  WHAT WE'D EXPECT WITHOUT TCE:
 *      sibling:
 *          inc  edi
 *          call _other
 *          ret
 *
 *  WHY: The result of `other(x+1)` is the function's return value, and
 *  both signatures are `int(int)` — ABI compatible. TCE turns the
 *  `call ... ; ret` into a single `jmp`. The caller's stack frame is
 *  reused.
 *
 *  NOTE: The `x + 1` adjustment happens *before* the jmp (gets folded
 *  with the argument register update). In the actual asm, since `x` is
 *  already in EDI and we want EDI+1, the compiler often emits one
 *  `inc edi` and jmps. The exact shape depends on the prologue.
 */
extern int other(int);

int sibling(int x) {
    return other(x + 1);
}

/*  musttail — force the optimizer to perform TCE.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Clang's `[[clang::musttail]]` (also `__attribute__((musttail))`) is a
 *  HARD requirement: if the compiler cannot tail-call, it errors out.
 *  Use this for interpreters and other code where the runtime
 *  correctness depends on no stack growth.
 */
#if defined(__clang__)
__attribute__((noinline))
int musttail_demo(int n, int acc) {
    if (n == 0) return acc;
    __attribute__((musttail)) return musttail_demo(n - 1, acc + n);
}
#endif
