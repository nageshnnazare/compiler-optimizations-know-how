/* 04_tail_call.c — TAIL CALL FROM THE CONTROL-FLOW SIDE
 * ============================================================================
 *
 *   Chapter 03's `05_tail_call.c` covers tail-call elimination as an
 *   interprocedural transform. Here we look at the *control-flow*
 *   consequence: `call ... ; ret` becomes `jmp ...`. The caller's
 *   stack frame is reclaimed before the callee runs.
 *
 *   In the CFG it shows up as: the function's exit-block contains a
 *   `jmp` instead of a `call`+`ret` pair, AND the Return Address Stack
 *   prediction is more accurate (one fewer push/pop).
 * ============================================================================
 */
extern int callee(int);

/*  caller_tail — textbook tail call.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      caller_tail:
 *          inc  edi                 ; x + 1 in argument register
 *          jmp  callee              ; TAILCALL: no frame, no ret
 */
int caller_tail(int x) {
    return callee(x + 1);
}

/*  sum_to — tail-recursive sum.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      sum_to:
 *          ; SCEV closed-form: acc + n*(n+1)/2  (no loop, no recursion)
 *          mov   eax, esi          ; acc
 *          test  edi, edi
 *          je    .ret
 *          add   eax, edi          ; + n
 *          lea   ecx, [rdi - 1]
 *          ; ... (closed-form arithmetic) ...
 *          ret
 *
 *  WHY: TCE first removes the recursion → loop. Then SCEV recognises
 *  the loop as `Σi=1..n i = n(n+1)/2` and substitutes the closed form.
 *  What started as a tail-recursive function with O(n) calls becomes
 *  O(1) machine code. TCE alone would have given a loop.
 */
int sum_to(int n, int acc) {
    if (n == 0) return acc;
    return sum_to(n - 1, acc + n);
}

/*  f / g — mutual recursion; TCE turns BOTH into a loop.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      f:
 *          xor  eax, eax
 *          test edi, edi
 *          jle  .ret_zero
 *          add  edi, 2             ; pre-bias for loop test
 *      .loop:                      ; SHARED loop body for both f and g
 *          cmp  edi, 3
 *          je   .ret_g_base
 *          add  edi, -2            ; x -= 2 each round-trip f→g→f
 *          cmp  edi, 3
 *          jge  .loop
 *      .ret_zero:
 *          ret
 *
 *  WHY: After tail-call elimination, both functions become loops over
 *  the same state. The compiler then notices the loops are isomorphic
 *  and emits a single shared loop. From two recursive functions to
 *  one tight loop with no calls — the recursion has been "fused".
 */
int g(int);

int f(int x) {
    if (x <= 0) return 0;
    return g(x - 1);
}

int g(int x) {
    if (x <= 0) return 1;
    return f(x - 1);
}
