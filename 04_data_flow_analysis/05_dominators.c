/* 05_dominators.c — dominator-tree intuition in C
 *
 *           Entry (e)
 *             │
 *             A
 *           ┌─┴─┐
 *           B   C
 *           └─┬─┘
 *             D
 *             │
 *            Exit
 *
 *   dominators: e dom {e, A, B, C, D, Exit}
 *               A dom {A, B, C, D, Exit}
 *               B dom {B}
 *               C dom {C}
 *               D dom {D, Exit}
 *
 *   dominator tree:
 *               e
 *               │
 *               A
 *              /│\
 *             B C D
 *                 │
 *                Exit
 *
 *   idom(B) = idom(C) = A
 *   idom(D) = A
 *
 * Dominance is what allows hoisting:
 *   - To hoist instruction I from BB X to BB Y, Y must DOMINATE every use
 *     of I, AND I's operands must all be defined in (or dominate) Y.
 *
 * Dominance also identifies loops:
 *   - A back-edge X → Y is one where Y DOMINATES X. The natural loop is
 *     all nodes reachable from X without going through Y.
 */
/*  dom_demo — `t = x*x` computed once because A dominates B, C, D.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      dom_demo:
 *          imul  esi, esi             ; A: t = x * x   ← computed ONCE
 *          xor   eax, eax
 *          cmp   edi, 1
 *          setae al                   ; al = (cond != 0)
 *          lea   eax, [rsi + 2*rax]   ; t + 2*(cond?1:0)
 *          dec   eax                  ; subtract 1
 *          ret
 *
 *  This is t + (cond ? 1 : -1), computed branchlessly using a 0/1
 *  indicator and lea. The single `imul` is the proof that A's
 *  computation of `t` was REUSED in both branches — possible only
 *  because A dominates B, C, and D in the CFG.
 *
 *  Dominance is also why φ-node placement works: a φ at the merge BB D
 *  picks among reaching defs from B and C, both of which are
 *  immediately dominated by A.
 */
int dom_demo(int cond, int x) {
    int t = x * x;          /* A */
    int r;
    if (cond) {
        r = t + 1;          /* B */
    } else {
        r = t - 1;          /* C */
    }
    return r;               /* D */
}
