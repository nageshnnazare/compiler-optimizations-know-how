/* 03_cse.c — COMMON SUBEXPRESSION ELIMINATION
 * ============================================================================
 *
 * What it is
 * ----------
 *   If the same expression `e` is computed twice and *none of its operands
 *   were modified* between the two computations, compute it once and reuse.
 *
 *   This is a special case of *value numbering*: each value gets a number;
 *   any expression hashing to a number already in the table is replaced by
 *   a reference to the prior result.
 *
 *       ┌──────────────────┐         each entry: (op, operand-vns) → vn
 *       │  value table     │
 *       │  ──────────────  │
 *       │  load a   → v1   │
 *       │  load b   → v2   │
 *       │  mul v1 v2 → v3  │         later: any `mul v1 v2` becomes v3
 *       │  const 5  → v4   │
 *       │  add v3 v4 → v5  │
 *       └──────────────────┘
 *
 * Pass names
 * ----------
 *   LLVM:  EarlyCSE (per-BB), GVN (global, across CFG).
 *   GCC :  tree-ssa-dom.cc (dominator-based CSE), tree-ssa-pre.cc (PRE).
 *
 * Legality
 * --------
 *   • Same operands (no intervening write that may alias).
 *   • No throwing instruction between the two evaluations (in C++) that
 *     would change observable order.
 *
 * What KILLS CSE in practice
 * --------------------------
 *   • An opaque function call between the two uses — UNLESS the operands
 *     are register-resident scalars that cannot be reached through any
 *     pointer the callee owns.
 *   • A write through a pointer that may alias an operand.
 *   • For loads: any may-store, including memcpy/memset to overlapping
 *     memory and atomic/volatile operations.
 * ============================================================================
 */
#include <stdint.h>

/*  cse_safe — two identical subexpressions of value parameters.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source:
 *      t1 = (x * y) + 1;
 *      t2 = (x * y) + 1;
 *      return t1 + t2;     ;; algebraically  2*(x*y + 1)
 *
 *  NAIVE asm (-O0): two imul, two add, then a final add.
 *
 *  ACTUAL (-O3):
 *      cse_safe:
 *          imul  edi, esi          ; edi = x * y          (ONE multiply)
 *          lea   eax, [2*rdi + 2]  ; eax = 2*(x*y) + 2    (lea trick)
 *          ret
 *
 *  WHY: After GVN both `(x*y)+1` expressions get the same value number,
 *  so the second is replaced by a reference to the first. The remaining
 *  `t1 + t2` becomes `2*t1`, and InstCombine notices `2*(xy+1) = 2*xy+2`,
 *  which the instruction selector lowers to a single `lea` with scale=2.
 */
int cse_safe(int x, int y) {
    int t1 = (x * y) + 1;
    int t2 = (x * y) + 1;
    return t1 + t2;
}

/*  cse_call_with_scalars — call between the two evaluations.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      cse_call_with_scalars:
 *          push  r14; push rbx; push rax           ; save callee-saved & align
 *          mov   ebx, esi                           ; spill y in rbx
 *          mov   r14d, edi                          ; spill x in r14
 *          call  side_effecting@PLT
 *          imul  r14d, ebx                          ; ONE multiply — CSE survived!
 *          lea   eax, [2*r14 + 2]
 *          pop ...
 *
 *  WHAT YOU MIGHT EXPECT: that the opaque call would force the compiler to
 *  redo `x*y` because the call "might have changed something".
 *
 *  WHY IT STILL CSE's: `x` and `y` are *register-resident scalar parameters*.
 *  No pointer to them was passed to the callee; the callee cannot reach
 *  them through any memory effect. The compiler proves the values are
 *  unchanged across the call and CSE proceeds. (The price is that the
 *  values must live in *callee-saved* registers across the call — RBX
 *  and R14 — which is why the prologue saves them.)
 */
extern int side_effecting(void);

int cse_call_with_scalars(int x, int y) {
    int t1 = (x * y) + 1;
    (void)side_effecting();
    int t2 = (x * y) + 1;
    return t1 + t2;
}

/*  cse_call_on_load — the call DOES kill CSE of a load.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      cse_call_on_load:
 *          push  rbx
 *          mov   rbx, rdi              ; save pointer
 *          mov   eax, dword ptr [rdi]  ; t1 = *p     ← FIRST load
 *          imul  eax, eax              ; ... use t1 (squared, just to use it)
 *          push  rax
 *          call  side_effecting@PLT
 *          mov   eax, dword ptr [rbx]  ; t2 = *p     ← SECOND load (reloaded!)
 *          ...
 *
 *  WHY: The callee `side_effecting` is opaque. Even though our pointer
 *  `p` was not passed to it, the callee could have any side effect on
 *  global memory; absent more information (TBAA, restrict, attributes),
 *  the optimizer must assume `*p` could have changed. The second `*p`
 *  load is therefore re-issued.
 *
 *  FIX (one of):
 *    – mark side_effecting `__attribute__((const))` or `((pure))`
 *    – use `restrict` on the pointer, prove no other store happens
 *    – inline side_effecting (LTO) so the compiler sees its body
 */
int cse_call_on_load(const int *p) {
    int t1 = *p;
    int squared = t1 * t1;
    (void)side_effecting();
    int t2 = *p;
    return squared + t2;
}

/*  cse_with_pure_call — `pure` declares no side effects.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      cse_with_pure_call:
 *          imul  edi, esi
 *          lea   eax, [2*rdi + 2]
 *          ret
 *
 *  WHY: `__attribute__((pure))` ⇒ LLVM attribute `readonly`. The optimizer
 *  knows the call cannot WRITE memory, so CSE of the second `(x*y)+1`
 *  proceeds; what is more striking is that the call ITSELF is deleted
 *  because its result is discarded and it has no observable effect.
 *  (Use `const` instead of `pure` if the function also doesn't READ memory.)
 */
__attribute__((pure)) extern int pure_function(void);

int cse_with_pure_call(int x, int y) {
    int t1 = (x * y) + 1;
    (void)pure_function();
    int t2 = (x * y) + 1;
    return t1 + t2;
}

/*  cse_loads — `restrict` enables CSE of loads across a store.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      cse_loads:
 *          mov   eax, dword ptr [rdi]   ; ONE load of *a
 *          mov   dword ptr [rsi], 99     ; store to *b
 *          add   eax, eax                ; eax = 2 * *a
 *          ret
 *
 *  WITHOUT restrict, the compiler would have to assume `a` and `b` alias
 *  and reload `*a` after the store — two loads.
 *
 *  WHY: `restrict` is a programmer promise to the optimizer that the
 *  memory regions reachable through `a` and `b` are disjoint for the
 *  lifetime of the function. LLVM lowers `restrict` to the `noalias`
 *  parameter attribute, and Alias Analysis returns NoAlias for any
 *  load through one against any store through the other.
 */
int cse_loads(const int * restrict a, int * restrict b) {
    int x1 = a[0];
    b[0] = 99;
    int x2 = a[0];
    return x1 + x2;
}
