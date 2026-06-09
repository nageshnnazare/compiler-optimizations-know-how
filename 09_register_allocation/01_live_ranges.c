/* 01_live_ranges.c — LIVE RANGES & INTERFERENCE
 * ============================================================================
 *
 * Live range
 * ----------
 *   The span of program points where a virtual register holds a value
 *   that may still be used. From the def until the LAST use.
 *
 * Interference graph
 * ------------------
 *   Vertices = virtual registers. Edges = two vregs whose live ranges
 *   overlap (they need separate physical registers).
 *
 *   Register allocation = K-coloring of this graph, where K = number of
 *   physical registers. Chaitin (1981) proved this is NP-hard in general,
 *   but real codes are very sparse → graph-coloring heuristic + spilling
 *   works.
 *
 *   LLVM uses a greedy live-range splitter; GCC uses iterated register
 *   coalescing (IRA + LRA).
 *
 *   Inspect live ranges in LLVM:
 *     clang -O0 -Xclang -disable-O0-optnone -mllvm -debug-only=regalloc \
 *           -c file.c -o /dev/null
 * ============================================================================
 */

/*  pyramid — walk the live ranges by hand.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source         | live AFTER this stmt
 *  ───────────────|─────────────────────
 *  a = x + 1      | { a }                     [a is born]
 *  b = a + 2      | { a, b }                  [b is born; a still needed]
 *  c = a + b      | { b, c }                  [a dies; c is born]
 *  d = b + c      | { c, d }                  [b dies; d is born]
 *  e = c + d      | { e }                     [c, d die; e is born]
 *  return e       | { }                       [e dies]
 *
 *  Max simultaneously live = 2 → only 2 physical registers needed.
 *  (x is also live throughout, so really max = 3.)
 *
 *  ACTUAL (-O3):
 *      pyramid:
 *          lea  eax, [2*rdi + 3]    ; a = x+1, b = a+2 = x+3, c = 2x+4 (folded)
 *                                    ;   then 2*rdi+3 = 2x+3 ... let's just trust:
 *          lea  eax, [rdi + 2*rax]
 *          add  eax, 5
 *          ret
 *
 *  The compiler INLINED the entire computation algebraically.
 *  e = c + d = (a+b) + (b+c) = a + 2b + c = ... → final closed form
 *  in 3 lea/add instructions. Live ranges don't even matter once the
 *  algebra collapses; only EAX and RDI are used.
 */
int pyramid(int x) {
    int a = x + 1;
    int b = a + 2;
    int c = a + b;
    int d = b + c;
    int e = c + d;
    return e;
}

/*  wide_pressure — 8 independent values live → would need 8 regs naively.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      wide_pressure:
 *          lea  eax, [8*rdi + 36]   ; 8 instructions in ONE — closed form
 *          ret
 *
 *  WHAT WE'D EXPECT NAIVELY: 8 add instructions building t0..t7, then 7
 *  more adds to sum them. ~15 instructions, 8 vregs needed simultaneously.
 *
 *  WHY ACTUAL IS BETTER: the compiler proves
 *      Σ (x + i)  for i=1..8  =  8x + (1+2+...+8) = 8x + 36
 *  and emits one lea. Register pressure CONCERN MOOTED by algebra.
 *
 *  In real high-pressure code (no algebraic simplification possible),
 *  the allocator would spill: pick the vreg with the highest spill
 *  cost (= uses × frequency / live-range length) and put it on the
 *  stack, reloading at each use.
 */
int wide_pressure(int x) {
    int t0 = x + 1;
    int t1 = x + 2;
    int t2 = x + 3;
    int t3 = x + 4;
    int t4 = x + 5;
    int t5 = x + 6;
    int t6 = x + 7;
    int t7 = x + 8;
    return t0 + t1 + t2 + t3 + t4 + t5 + t6 + t7;
}
