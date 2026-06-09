/* 06_constant_propagation.c — constant-propagation lattice
 *
 * SCCP (Sparse Conditional Constant Propagation) tracks each SSA name's
 * value as one of:
 *
 *       ⊤  ("undefined yet")
 *       ╱   ╲
 *      … -1 0 1 2 …    (specific constants)
 *       ╲   ╱
 *       ⊥  ("not a constant")
 *
 * Meet rules:
 *   meet(⊤, x) = x
 *   meet(x, x) = x
 *   meet(x, y) = ⊥   if x ≠ y, both constants
 *
 * SCCP is "conditional" because it also tracks which CFG edges are
 * executable. If an edge is proven unreachable, the φ doesn't pick up
 * values along it.
 *
 * Picture for sccp_demo:
 *
 *     y1 = 5            (lattice value: 5)
 *     br true → BB2     (edge BB1→BB3 not executable)
 *     BB2: y2 = 10      (lattice value: 10)
 *     BB3: y3 = y1 + 1  (UNREACHABLE; not visited)
 *     BB4: y4 = φ(y2 from BB2,
 *                 y3 from BB3)        ← BB3 edge dead → φ collapses to y2 = 10
 *     return y4         (lattice value: 10) → compiles to `mov eax, 10`
 */
/*  sccp_demo — only the true arm is executable.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      sccp_demo:
 *          mov  eax, 10
 *          ret
 *
 *  WHY: SCCP marks BB1→BB_else as non-executable (condition is constant
 *  1). The lattice value of `y` collapses to {10}. The φ at the join
 *  has only one executable input, picks 10. The whole function returns
 *  the literal 10.
 *
 *  COMPARE WITH NAIVE CP: it would merge both arms (10 from one, 6 from
 *  the other) and conclude y = ⊥ ("not constant") → emit the if/else.
 *  SCCP's edge-executability tracking is what makes the difference.
 */
int sccp_demo(void) {
    int x = 5;
    int y;
    if (1)              /* true branch always taken; edge to else is dead */
        y = 10;
    else
        y = x + 1;
    return y;
}
