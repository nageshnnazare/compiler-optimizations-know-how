/* 01_reaching_definitions.c — REACHING DEFINITIONS ANALYSIS
 * ============================================================================
 *
 * The CFG and the reaching sets
 * -----------------------------
 *
 *      d1:  x = 10              <-- BB1: GEN={d1}        KILL={d2}
 *           │
 *      ┌────┴─────┐
 *      ▼          ▼
 *   d2:x=20    (no def)         <-- BB2: GEN={d2} KILL={d1}, BB3: GEN=KILL=∅
 *      │          │
 *      └────┬─────┘
 *           ▼
 *        use of x              <-- BB4: IN={d1, d2} — TWO defs reach here
 *
 *   Forward, MAY (∪) analysis:
 *      OUT[B] = GEN[B] ∪ (IN[B] \ KILL[B])
 *      IN [B] = ⋃ OUT[P] over predecessors P
 *
 *   The two reaching defs at BB4's use of x are EXACTLY what tells the
 *   SSA construction pass it must place a φ-node at the start of BB4:
 *
 *      BB4:  x4 = φ(x_from_BB2, x_from_BB3)
 *
 * Uses of reaching defs
 * ---------------------
 *   – φ-node placement during SSA construction
 *   – Use-def chain construction
 *   – Constant propagation lattice setup
 *
 * ----------------------------------------------------------------------------
 *
 *  reaches — what actually gets emitted.
 *  ----------------------------------------------------------------------------
 *  ACTUAL (-O3):
 *      reaches:
 *          test  edi, edi               ; cond ?
 *          mov   ecx, 10                ; load alternative #1 (d1)
 *          mov   eax, 20                ; load alternative #2 (d2)
 *          cmove eax, ecx               ; if !cond → eax = ecx = 10
 *          ret
 *
 *  WHY: After SSA construction places the φ-node for x at the merge,
 *  SimplifyCFG converts the if into a `select`. The selector emits a
 *  CMOV: branch-free, two literal materializations and one conditional
 *  move. No branch mis-prediction risk.
 */
int reaches(int cond) {
    int x = 10;             /* d1 */
    if (cond) {
        x = 20;             /* d2 */
    }
    return x;               /* SSA: x_ret = φ(x@BB2 = d2, x@BB3 = d1) */
}
