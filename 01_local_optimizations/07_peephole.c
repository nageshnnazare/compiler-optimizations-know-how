/* 07_peephole.c — PEEPHOLE / INSTRUCTION COMBINING
 * ============================================================================
 *
 * Idea
 * ----
 *   Recognize a small sequence of operations and rewrite it as an
 *   equivalent shorter or cheaper sequence. LLVM's `InstCombine` is a
 *   library of ~1000 such rewrite rules applied to a fixed point.
 *
 *   At the machine level, the backend's `peephole2` (GCC) and
 *   `MachineCSE/MachineCombiner` (LLVM) do the same on machine IR.
 *
 *   Examples:
 *
 *     C source pattern                  emitted instruction
 *     -----------------                 -------------------
 *     x ^ x                             xor reg, reg            (zero idiom)
 *     x == 0 (used as boolean)          test reg, reg ; setcc
 *     x = -y                            neg reg
 *     x = ~(-y)                         lea  reg, [y - 1]       (= y - 1)
 *     x = (a > b) ? a : b               cmp + cmov              (no branch)
 *     x = abs(y)                        cdq + xor + sub         (branchless)
 *     mov reg, 0                        xor reg, reg            (shorter)
 * ============================================================================
 */
#include <stdint.h>

/*  byte_merge — `(a & 0x0F) | (b & 0xF0)`
 *  ──────────────────────────────────────────────────────────────────────────
 *  NAIVE EXPECTED (-O3, lazy compiler):
 *      and  edi, 15
 *      and  esi, 240
 *      or   edi, esi
 *      mov  eax, edi
 *      ret
 *
 *  ACTUAL (-O3):
 *      byte_merge:
 *          and  edi, 15
 *          and  esi, 240
 *          lea  eax, [rsi + rdi]    ; or → add is legal here, then folded with mov
 *          ret
 *
 *  WHY: `(a & 0x0F)` and `(b & 0xF0)` have *disjoint set bits* (the masks
 *  don't overlap). Under this guarantee, `x | y == x + y`. InstCombine
 *  knows that rule and rewrites `or` → `add`, which the selector then
 *  fuses with the move via `lea`.
 */
uint32_t byte_merge(uint32_t a, uint32_t b) {
    return (a & 0x0Fu) | (b & 0xF0u);
}

/*  extract_nibble — without BMI2: shift + mask.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3, baseline x86-64):
 *      extract_nibble:
 *          mov  eax, edi
 *          shr  eax, 4
 *          and  eax, 15
 *          ret
 *
 *  With `-mbmi2`:
 *      extract_nibble:
 *          mov  eax, 0x0404         ; control: 4-bit field starting at bit 4
 *          bextr eax, edi, eax      ; single bit-field extract
 *          ret
 *
 *  WHY: When the target has BMI2, the bit-field-extract idiom `(x >> c1) &
 *  ((1 << c2) - 1)` lowers to one `bextr`. Otherwise the canonical
 *  shr+and pair stays.
 */
uint32_t extract_nibble(uint32_t x) {
    return (x >> 4) & 0x0Fu;
}

/*  branchless_sign — `(x>0) - (x<0)` returns -1, 0, or +1.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      branchless_sign:
 *          mov   ecx, edi
 *          sar   ecx, 31            ; ecx = -1 if x<0 else 0
 *          xor   eax, eax
 *          test  edi, edi
 *          setne al                 ; al = 1 if x≠0 else 0
 *          or    eax, ecx           ; combine: -1 / 0 / +1
 *          ret
 *
 *  WHY: The compiler recognizes the (>0)-(<0) idiom as the C "signum"
 *  function and synthesizes a 3-instruction branchless implementation.
 *  Faster than two branches; the cost is a couple of extra instructions
 *  but no mispredict penalty.
 */
int branchless_sign(int x) {
    return (x > 0) - (x < 0);
}

/*  abs_branchless — `(x + sign(x)) ^ sign(x)`.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      abs_branchless:
 *          mov   eax, edi
 *          neg   eax                ; eax = -x   (sets flags)
 *          cmovs eax, edi           ; if NEG produced negative (=> x was -INT_MIN)
 *                                    ; or, if -x is positive (x was negative),
 *                                    ; keep -x. Otherwise restore x.
 *          ret
 *
 *  WHY: InstCombine + the backend identify the hand-rolled abs idiom and
 *  replace it with a 3-instruction branchless equivalent using `cmov`.
 *  This is faster than the textbook formula on modern OoO cores.
 */
int abs_branchless(int x) {
    int mask = x >> 31;
    return (x + mask) ^ mask;
}

/*  test_for_zero — `x == 0 ? 1 : 0`
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      test_for_zero:
 *          xor   eax, eax           ; zero return reg (also breaks dep)
 *          test  edi, edi           ; flags = x & x  → "is x zero?"
 *          sete  al                 ; AL = ZF (1 if zero)
 *          ret
 *
 *  WHY: The canonical "boolean-from-int" pattern. Three instructions, no
 *  branch; the `xor` zeroes the upper bits of EAX so the SETcc-into-AL
 *  yields a clean 0/1 in EAX.
 */
uint32_t test_for_zero(uint32_t x) {
    return x == 0 ? 1u : 0u;
}

/*  signmask_from_bool — produce 0 or -1 from a condition.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      signmask_from_bool:
 *          xor  eax, eax
 *          neg  edi                 ; flags: CF set iff edi != 0
 *          sbb  eax, eax            ; eax = -CF  → 0 or -1
 *          ret
 *
 *  WHY: `-(x != 0)` is the SIMD-mask idiom. The compiler synthesises it
 *  using the `NEG → CF` side effect plus `SBB reg,reg` which produces
 *  `reg = reg - reg - CF = -CF`. Cute, fast, branch-free.
 */
int signmask_from_bool(int cond) {
    return -(cond != 0);
}
