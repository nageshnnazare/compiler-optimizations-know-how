/* 04_unswitch.c — LOOP UNSWITCHING
 * ============================================================================
 *
 * Idea
 * ----
 *   A branch INSIDE a loop whose condition is *loop-invariant* is moved
 *   OUTSIDE the loop. The loop is duplicated, once per outcome.
 *
 *     BEFORE                                  AFTER
 *     ──────                                  ─────
 *     for (i=0;i<n;i++) {                     if (mode) {
 *       if (mode)  body_A(i);                   for (i=0;i<n;i++) body_A(i);
 *       else       body_B(i);                  } else {
 *     }                                          for (i=0;i<n;i++) body_B(i);
 *                                              }
 *
 * Cost / benefit
 * --------------
 *   COST    code-size doubles.
 *   BENEFIT each loop body has zero per-iteration branch overhead AND
 *           can be optimized independently (vectorized, unrolled).
 *
 * Pass names
 * ----------
 *   LLVM:  SimpleLoopUnswitchPass (new), LoopUnswitchPass (legacy).
 *   GCC :  tree-ssa-loop-unswitch.cc.
 *
 * Knobs (LLVM)
 * ------------
 *   -mllvm -unswitch-threshold=N           default ~100
 * ============================================================================
 */
#include <stddef.h>

/*  unswitchable — `mode` is loop-invariant; the if is hoisted out.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3, simplified):
 *      unswitchable:
 *          test  rdx, rdx
 *          je    .exit
 *          test  ecx, ecx                  ; check mode ONCE up front
 *          je    .mode_zero
 *          ; --- mode != 0: vectorized "a[i] = b[i] * 2" loop ---
 *      .mode_nonzero_vec:
 *          movdqu xmm0, [rsi + 4*rcx]
 *          movdqu xmm1, [rsi + 4*rcx + 16]
 *          paddd  xmm0, xmm0                ; *2 by adding to self
 *          paddd  xmm1, xmm1
 *          movdqu [rdi + 4*rcx],      xmm0
 *          movdqu [rdi + 4*rcx + 16], xmm1
 *          add    rcx, 8
 *          ...
 *      .mode_zero:
 *          ; --- mode == 0: vectorized "a[i] = b[i] + 5" loop ---
 *          ; (separate loop body, no branch inside)
 *
 *  WHAT WE'D EXPECT WITHOUT UNSWITCHING:
 *      .loop:
 *          mov  eax, [rsi + 4*rcx]
 *          test ecx, ecx
 *          je   .else
 *          shl  eax, 1              ; *2
 *          jmp  .store
 *      .else:
 *          add  eax, 5
 *      .store:
 *          mov  [rdi + 4*rcx], eax
 *          inc  rcx
 *          cmp  rcx, rdx
 *          jl   .loop
 *
 *  WHY: With the branch inside the body, the vectorizer can't easily
 *  SIMD-ify because the two arms are different ops. After unswitching
 *  each loop is pure straight-line vectorizable code; both halves run
 *  at the maximum SIMD width.
 */
void unswitchable(int *a, const int *b, size_t n, int mode) {
    for (size_t i = 0; i < n; i++) {
        if (mode)
            a[i] = b[i] * 2;
        else
            a[i] = b[i] + 5;
    }
}

/*  unswitch_via_flag — invariant condition lives in a separate variable.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Same outcome as `unswitchable` above. The compiler hoists the load
 *  `*opt` into the preheader (LICM), reduces it to a 0/1 flag, then
 *  unswitches the loop on that flag.
 *
 *  Note: with `restrict` or a load that AA proves invariant, this is
 *  reliable. Without those guarantees, the load might be re-issued and
 *  unswitching may not fire (the compiler can't prove the condition is
 *  truly loop-invariant).
 */
void unswitch_via_flag(int *a, const int *b, size_t n, const int *opt) {
    int flag = (*opt != 0);
    for (size_t i = 0; i < n; i++) {
        if (flag)
            a[i] = b[i] * 2;
        else
            a[i] = b[i] + 5;
    }
}
