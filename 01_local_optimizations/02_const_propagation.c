/* 02_const_propagation.c — CONSTANT & COPY PROPAGATION
 * ============================================================================
 *
 * What it is
 * ----------
 *   Constant propagation: every use of a name with a known constant value
 *   is replaced with that constant.
 *   Copy propagation:     every use of a copy `b = a` is replaced with `a`.
 *
 * In SSA both reduce to a single hash-table walk over the def-use graph:
 *
 *     ┌──────────────┐         walk the use-edges out of each
 *     │ def: a := 5  │ ───►    constant-or-copy definition, rewriting
 *     └──────────────┘         every use, then refolding the consumer.
 *
 * Constant propagation is *aggressively cascading*: rewriting one use can
 * make the consumer expression foldable, which produces another constant,
 * which cascades into ITS users, until a fixed point.
 *
 * SCCP (Sparse Conditional Constant Propagation) is the smart version: it
 * additionally tracks which CFG edges are *executable* and refuses to
 * merge values arriving on dead edges.
 *
 * Pass names
 * ----------
 *   LLVM: SCCPPass, IPSCCPPass (interprocedural), InstCombinePass.
 *   GCC:  tree-ssa-ccp.cc (CP), tree-ssa-copy.cc (copy prop).
 * ============================================================================
 */
#include <stdint.h>

/*  simple_chain — cascading propagation
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source steps:
 *      a = 5            ; def
 *      b = a + 2        ; → b = 5 + 2 → b = 7
 *      c = b * 3        ; → c = 7 * 3 → c = 21
 *      d = c            ; copy prop → d == c
 *      return d         ; → return 21
 *
 *  NAIVE x86-64 (-O0):                  ACTUAL x86-64 (-O3):
 *      mov   [rsp-4], 5                 simple_chain:
 *      mov   eax, [rsp-4]                   mov   eax, 21
 *      add   eax, 2                          ret
 *      mov   [rsp-8], eax
 *      mov   eax, [rsp-8]
 *      imul  eax, 3
 *      mov   [rsp-12], eax
 *      ...
 *      ret
 *
 *  WHY: After mem2reg promotes a,b,c,d to SSA values, SCCP walks the
 *  chain. Each definition's lattice value moves down from ⊤ (unknown) to
 *  a concrete constant, and the *meet* of those constants with their
 *  successor's transfer function produces another constant. Four
 *  iterations later, %d is the lattice element `21`, and the return
 *  rewrites to `ret i32 21`. ConstantFolding finishes the job.
 */
int simple_chain(void) {
    int a = 5;
    int b = a + 2;
    int c = b * 3;
    int d = c;
    return d;
}

/*  sccp_demo — *conditional* constant propagation kills dead arms.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Naïve CP would not be able to prove the `else` arm is unreachable.
 *  SCCP does: it marks the edge `flag == false → else` as non-executable
 *  because `flag` provably has lattice value `1`.
 *
 *  ACTUAL (-O3):
 *
 *      sccp_demo:
 *          lea   eax, [rdi + 1]    ; eax = x + 1; the `else` branch is GONE
 *          ret
 *
 *  WHY: SCCP runs as a fixed-point over the (lattice × CFG-edge) state.
 *  The lattice element of `flag` is `1`. The conditional branch's
 *  transfer function says "if condition is constant 1, only the true-edge
 *  is executable". Once the else-edge is dead, the φ at the join (if
 *  there were one) drops its else-input, and the branch+merge collapses.
 *
 *  Without SCCP (i.e. with plain CP), the compiler can still arrive at
 *  this answer via InstCombine + SimplifyCFG, but in larger programs only
 *  SCCP is precise enough to prune long dead-code chains.
 */
int sccp_demo(int x) {
    int flag = 1;
    if (flag) {
        return x + 1;
    } else {
        return x - 99;
    }
}

/*  copy_prop — chains of copies vanish entirely.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source:    b = a; c = b; d = c; return d + 1;
 *
 *  ACTUAL (-O3):
 *
 *      copy_prop:
 *          lea   eax, [rdi + 1]    ; eax = a + 1 — every copy collapsed
 *          ret
 *
 *  NAIVE (-O0) would do 3 memory copies through stack slots.
 *
 *  WHY: After mem2reg every `int t = x` becomes `%t = %x` at the IR
 *  level — a zero-cost rename, NOT a real move. The register allocator's
 *  *coalescer* then assigns all four SSA names to the same physical
 *  register (RDI / EAX); there is nothing left to copy.
 */
int copy_prop(int a) {
    int b = a;
    int c = b;
    int d = c;
    return d + 1;
}
