/* 06_dead_store_elim.c — DEAD STORE ELIMINATION (DSE)
 * ============================================================================
 *
 * Idea
 * ----
 *   A store to a location that is OVERWRITTEN before being read is dead
 *   and removed. Similarly, the store right before the end of the
 *   object's lifetime (function return, free, lifetime.end) is dead.
 *
 *     store i32 1, %p
 *     store i32 2, %p        ; ← second store kills the first
 *     load   %p              ; sees 2; the load of "1" never happens
 *
 * Pass names
 * ----------
 *   LLVM:  DSEPass, plus interaction with MemorySSA.
 *   GCC :  tree-ssa-dse.cc.
 *
 * What blocks DSE
 * ---------------
 *   – VOLATILE store (must be observable).
 *   – Atomic store (synchronisation visible).
 *   – Aliased load between the two stores (then the first is alive).
 *   – Escape: if the pointer escaped, the compiler can't prove the
 *     store is dead.
 * ============================================================================
 */

/*  overwrite — first store dead, second wins.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      overwrite:
 *          mov   dword ptr [rdi], esi      ; ONE store: *p = v
 *          ret
 *
 *  WAIT — where did `*p = 0` go? Gone. DSE deleted it; only the final
 *  store survives.
 */
void overwrite(int *p, int v) {
    *p = 0;
    *p = v;
}

/*  overlapping_memsets — second memset overwrites the first.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      overlapping_memsets:
 *          xorps   xmm0, xmm0
 *          movups  xmmword ptr [rdi + 48], xmm0
 *          movups  xmmword ptr [rdi + 32], xmm0
 *          movups  xmmword ptr [rdi + 16], xmm0
 *          movups  xmmword ptr [rdi],      xmm0   ; 64 bytes of zero
 *          ret
 *
 *  WHAT THE SOURCE WROTE: memset(buf, 0xFF, 32) then memset(buf, 0, 64).
 *  The first memset is COMPLETELY OVERWRITTEN by the second (which
 *  covers more bytes). DSE removed the 0xFF store; only the all-zero
 *  64-byte store survives, inline-expanded as 4 × 16-byte stores.
 */
#include <string.h>
void overlapping_memsets(char *buf) {
    memset(buf, 0xFF, 32);   /* dead */
    memset(buf, 0,    64);
}

/*  volatile_store — must not be deleted.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      volatile_store:
 *          mov   dword ptr [rdi], 0       ; FIRST store kept (volatile)
 *          mov   dword ptr [rdi], esi     ; SECOND store kept
 *          ret
 *
 *  WHY: `volatile` says "every store is observable, do not coalesce".
 *  Both stores must appear in program order in the final asm. Used for
 *  MMIO, signal flags, debug counters, anything the C abstract machine
 *  doesn't model.
 */
void volatile_store(volatile int *p, int v) {
    *p = 0;
    *p = v;
}

/*  dead_locally — store to a local that nothing reads → entire function empty.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      dead_locally:
 *          ret                              ; ONE instruction!
 *
 *  WHY: `local` is a stack scalar; SROA + mem2reg promoted it. The
 *  store to a register-promoted local with no readers is dead;
 *  the alloca is removed; the function body has zero side effects.
 */
void dead_locally(void) {
    int local = 42;
    (void)local;
}
