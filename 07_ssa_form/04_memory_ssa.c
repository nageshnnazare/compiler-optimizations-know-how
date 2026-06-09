/* 04_memory_ssa.c — observing MemorySSA reasoning
 *
 * MemorySSA tracks an SSA-like chain of "memory states" so that loads and
 * stores can participate in the same global optimizations as scalars.
 *
 *   ; pseudo-MemorySSA
 *   1 = MemoryDef(LiveOnEntry)   ;  store i32 %x, ptr %p
 *   2 = MemoryDef(1)             ;  store i32 %y, ptr %q
 *   ; the load below
 *   MemoryUse(2)                 ;  load  i32, ptr %r
 *
 * GVN walks the MemoryUse → its defining MemoryDef chain to ask:
 *   "is %r aliased by store 2? by store 1? if not, what value is at %r?"
 *
 * Dump in LLVM:
 *   clang -O1 -mllvm -print-memoryssa -S 04_memory_ssa.c -o /dev/null
 */
/*  memssa_demo — three pointers, possibly aliased.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      memssa_demo:
 *          mov  dword ptr [rdi], ecx          ; *p = x
 *          mov  dword ptr [rsi], r8d          ; *q = y
 *          mov  eax, dword ptr [rdx]          ; load *r
 *          ret
 *
 *  WHY NOT FORWARDED: AA returns MayAlias for both `*p` vs `*r` and
 *  `*q` vs `*r` (no restrict, no type info to disambiguate). MemorySSA
 *  models the load as MemoryUse(*p) ∩ MemoryUse(*q): the load must be
 *  re-issued because either store could have written to *r.
 *
 *  WITH restrict on all three pointers, the optimizer would prove
 *  *r aliases NEITHER *p nor *q, and the load could be moved BEFORE
 *  the stores. Try recompiling with:
 *      int memssa_restrict(int * restrict p, int * restrict q,
 *                          int * restrict r, int x, int y) { ... }
 *  → the asm reorders the load and stores freely.
 */
int memssa_demo(int *p, int *q, int *r, int x, int y) {
    *p = x;
    *q = y;
    return *r;       /* MemorySSA + AA decide whether to forward x or y */
}
