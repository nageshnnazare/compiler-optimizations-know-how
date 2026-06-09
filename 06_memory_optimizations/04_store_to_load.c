/* 04_store_to_load.c — STORE-TO-LOAD FORWARDING
 * ============================================================================
 *
 * Idea
 * ----
 *   A `load` from address X that follows a `store` to address X with the
 *   same size, with NO ALIASING WRITE in between, can be replaced by the
 *   STORED VALUE — the load never touches memory.
 *
 *       store i32 %v, ptr %p
 *       %x = load i32, ptr %p     ; → %x = %v (load eliminated)
 *
 *   This is what GVN/EarlyCSE achieves at the IR level. The CPU has its
 *   own hardware store-to-load forwarder (a few cycles to bypass the L1
 *   cache), but the optimizer's version is FREE: zero µops at runtime.
 *
 * Pass names
 * ----------
 *   LLVM:  EarlyCSEPass, GVNPass, MemorySSA.
 *   GCC :  tree-ssa-fre.cc, store-merging.
 *
 * Blocked by
 * ----------
 *   – Any intervening write that may alias.
 *   – Opaque function calls without `pure`/`const`/LTO body.
 *   – Volatile/atomic on either op.
 *   – Different sizes (load size ≠ store size).
 * ============================================================================
 */
extern void opaque(int *p);

/*  forward — the load is replaced by the stored value.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      forward:
 *          mov   dword ptr [rdi], esi     ; *p = v
 *          lea   eax, [rsi + 1]           ; return v + 1   (load gone)
 *          ret
 *
 *  WHAT WE'D EXPECT WITHOUT FORWARDING:
 *      forward:
 *          mov   dword ptr [rdi], esi
 *          mov   eax, dword ptr [rdi]     ; redundant reload
 *          inc   eax
 *          ret
 *
 *  WHY: EarlyCSE sees the store IMMEDIATELY followed by a load of the
 *  same address with the same type — replaces the load with the stored
 *  SSA value `%v`. The reload disappears.
 */
int forward(int *p, int v) {
    *p = v;
    return *p + 1;
}

/*  forward_chain — chain of three store+load pairs.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source:    *p = v;  int t1 = *p;  *p = t1 + 1;  int t2 = *p;  *p = t2 + 1;
 *             return *p;
 *
 *  ACTUAL (-O3):
 *      forward_chain:
 *          lea  eax, [rsi + 2]            ; final value = v + 2
 *          mov  dword ptr [rdi], eax      ; ONE store of v+2
 *          ret
 *
 *  WHY: forwarding cascades — each load becomes the previous store's
 *  value, each store's value combines with the next. The chain
 *  collapses; DSE removes the intermediate stores; only ONE final
 *  store remains. From 6 memory ops to 1.
 */
int forward_chain(int *p, int v) {
    *p = v;
    int t1 = *p;
    *p = t1 + 1;
    int t2 = *p;
    *p = t2 + 1;
    return *p;
}

/*  blocked — opaque call between store and load → forwarding blocked.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      blocked:
 *          push  rbx
 *          mov   rbx, rdi
 *          mov   dword ptr [rdi], esi     ; *p = v
 *          call  opaque                   ; opaque() may modify *p
 *          mov   eax, dword ptr [rbx]     ; RELOAD *p
 *          pop   rbx
 *          ret
 *
 *  WHY: opaque() may have done anything to memory — including writing
 *  through some pointer that aliases p. The compiler conservatively
 *  re-issues the load.
 *
 *  FIX:
 *    – mark opaque as `__attribute__((const))` or `(pure)` — both let
 *      AA prove opaque can't write *p;
 *    – use `restrict` if applicable;
 *    – inline opaque (visible body or LTO).
 */
int blocked(int *p, int v) {
    *p = v;
    opaque(p);
    return *p;
}
