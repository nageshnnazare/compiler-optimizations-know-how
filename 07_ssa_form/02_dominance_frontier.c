/* 02_dominance_frontier.c — picture of dominance frontier
 *
 *      Entry
 *        │
 *        A
 *      ┌─┴─┐
 *      B   C
 *      │   │
 *      └─┬─┘
 *        D
 *        │
 *       Exit
 *
 *   DF(B) = { D }  → if we assign to v in B, place φ(v) in D.
 *   DF(C) = { D }  → likewise.
 *   DF(A) = ∅     → no φ needed for vars defined in A only.
 *
 * In source code, this CFG arises from any if/else with a single join. The
 * φ at D is what unifies the values produced by the two arms.
 */
/*  df_demo — φ at D produced by the if/else.
 *  ──────────────────────────────────────────────────────────────────────────
 *  IR (-O1) shows the φ explicitly:
 *      entry:
 *        br i1 %cond, label %B, label %C
 *      D:                                  ; B and C both jump here
 *        %v = phi i32 [ 10, %B ], [ 20, %C ]
 *        ret i32 %v
 *
 *  At -O3 SimplifyCFG converts the φ to a select, then the backend
 *  emits a CMOV:
 *      df_demo:
 *          test  edi, edi
 *          mov   ecx, 20
 *          mov   eax, 10
 *          cmove eax, ecx          ; if cond == 0 → eax = 20
 *          ret
 *
 *  The φ at D is the SSA encoding of "two reaching defs at the same
 *  use"; the dominance frontier tells you exactly where to place such
 *  φ's during SSA construction.
 */
int df_demo(int cond) {
    int v;
    if (cond) {
        v = 10;     /* assign in B */
    } else {
        v = 20;     /* assign in C */
    }
    return v;       /* use in D — φ here */
}
