/* 04_dead_code.c — DEAD CODE ELIMINATION (DCE, ADCE, BDCE)
 * ============================================================================
 *
 * Three flavours
 * --------------
 *   DCE  – instruction with no users AND no side effects → delete.
 *   ADCE – assume EVERY instruction dead; mark live anything that affects
 *          observable output (returns, stores to escaped pointers, calls
 *          with side effects); then sweep. ADCE removes entire chains of
 *          temporaries that DCE alone would leave behind.
 *   BDCE – Bit-Tracking DCE: tracks per-bit demand; if the program only
 *          ever uses the low 8 bits of a value, the upper-bit computation
 *          is dead and removed.
 *
 *               ┌──────────┐
 *               │   ret v  │ ← anchor: live
 *               └────┬─────┘
 *                    │ uses(v)
 *                    ▼
 *               ┌──────────┐
 *               │ v = ...  │ ← reachable from anchor → live
 *               └──────────┘
 *               ┌──────────┐
 *               │ u = expensive_pure_fn()  │ ← not reachable → DEAD
 *               └──────────┘
 *
 * Pass names
 * ----------
 *   LLVM: DCEPass, ADCEPass, BDCEPass.
 *   GCC : tree-ssa-dce.cc.
 *
 * What stops DCE
 * --------------
 *   – Volatile (read or write).
 *   – Atomics.
 *   – Calls without `pure`/`const`/`readnone`/`readonly` (or LTO body).
 *   – Anything with `__attribute__((used))` or `__asm__ volatile`.
 * ============================================================================
 */
#include <stdint.h>

/*  dce_basic — sef() is `const`, so its call AND result are deletable.
 *  ──────────────────────────────────────────────────────────────────────────
 *  NAIVE (-O0): a real `call sef ; mov [rsp-N], eax ; ...` sequence.
 *
 *  ACTUAL (-O3):
 *      dce_basic:
 *          lea   eax, [rdi + 1]    ; just `return x + 1`. NO call at all.
 *          ret
 *
 *  WHY: `__attribute__((const))` ⇒ LLVM `readnone`. The call has no
 *  observable effect: its result is discarded, and it doesn't touch any
 *  memory the caller could observe. DCE deletes both the call AND the
 *  store to `unused`.
 */
__attribute__((const)) extern int sef(int x);
extern int with_effects(int x);

int dce_basic(int x) {
    int unused = sef(x);
    (void)unused;
    return x + 1;
}

/*  dce_keeps_side_effects — the call survives even though its result is unused.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      dce_keeps_side_effects:
 *          push  rbx
 *          mov   ebx, edi          ; spill x (live across call)
 *          call  with_effects@PLT  ; CALL KEPT (might do I/O, write globals…)
 *          inc   ebx               ; compute x + 1
 *          mov   eax, ebx
 *          pop   rbx
 *          ret
 *
 *  WHY: Absent any attribute, `with_effects` is *opaque*. The compiler
 *  must conservatively assume the call may read/write memory, throw, or
 *  do I/O. Its result is dead, but the call itself must remain. The
 *  computation `x + 1` is reordered to AFTER the call so `x` only needs
 *  to be preserved (in callee-saved RBX) across the call.
 */
int dce_keeps_side_effects(int x) {
    int unused = with_effects(x);
    (void)unused;
    return x + 1;
}

/*  adce_chain — three locals chain into nothing.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source:
 *      int a = x * 2;     ;; feeds only b
 *      int b = a + 1;     ;; feeds only c
 *      int c = b - 4;     ;; nothing reads c (the (void)c cast doesn't count)
 *      return x;
 *
 *  ACTUAL (-O3):
 *      adce_chain:
 *          mov   eax, edi          ; eax = x   ← the WHOLE chain disappeared
 *          ret
 *
 *  WHY: ADCE starts from the *anchor* set {return value, side-effecting
 *  stores, ...} and marks live anything reachable BACKWARD through use-def
 *  edges. Nothing in {a, b, c} is reachable; ADCE deletes them in one
 *  sweep. Plain DCE would need three iterations (delete c first; then b
 *  becomes useless; then a).
 */
int adce_chain(int x) {
    int a = x * 2;
    int b = a + 1;
    int c = b - 4;
    (void)c;
    return x;
}

/*  low_byte — BDCE eliminates the high-bit computation.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source:
 *      uint32_t hi = (x >> 24) << 24;  ; high byte in position
 *      uint32_t lo = x & 0xff;         ; low byte
 *      return lo + (hi & 0);           ; the `hi & 0` proves hi dead
 *
 *  ACTUAL (-O3):
 *      low_byte:
 *          movzx eax, dil          ; eax = zero-extend(x & 0xff). ONE insn.
 *          ret
 *
 *  WHY: BDCE tracks which bits of each SSA value are demanded by anchors.
 *  `lo + (hi & 0)` ≡ `lo` because `hi & 0 = 0`. So the demanded bits of
 *  `hi` are ∅ → its entire computation is dead. Then `lo = x & 0xff`
 *  means only the low 8 bits of `x` are demanded; the instruction
 *  selector emits `movzx eax, dil` (load DIL = low byte of EDI, zero-
 *  extend to EAX) — one byte-load instruction, no AND mask needed.
 */
uint32_t low_byte(uint32_t x) {
    uint32_t hi = (x >> 24) << 24;
    uint32_t lo = x & 0xff;
    return lo + (hi & 0);
}
