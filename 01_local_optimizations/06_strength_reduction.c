/* 06_strength_reduction.c — STRENGTH REDUCTION
 * ============================================================================
 *
 * Idea
 * ----
 *   Replace expensive operations with semantically-equivalent but cheaper
 *   ones. The savings can be 1 cycle (mul → shift) or 30+ cycles (div →
 *   multiply-by-magic).
 *
 * Where it lives
 * --------------
 *   – Mid-end: LLVM's `InstCombine`, GCC's `forwprop` + `match.pd`.
 *   – Backend: the instruction selector replaces multiplies by power-of-
 *     two and small constants with shift/add/lea combinations.
 *   – DAG-combiner: replaces divides by constants with the
 *     Granlund-Montgomery multiply-by-magic sequence.
 *
 * Why it matters
 * --------------
 *   Latencies on modern x86 (typical):
 *      add/sub/shift/lea   1 cycle
 *      imul (32-bit)       3-4 cycles
 *      idiv (32-bit)       20-30 cycles, NOT pipelined
 *   Cutting a div out of a hot loop is the single biggest "free" win you
 *   can get from the compiler.
 * ============================================================================
 */
#include <stdint.h>

/*  u_mul2 — x*2 → x+x via lea.
 *  ──────────────────────────────────────────────────────────────────────────
 *  EXPECTED (naive): shl eax, 1     ; or:  imul eax, 2
 *
 *  ACTUAL (-O3):
 *      u_mul2:
 *          lea  eax, [rdi + rdi]    ; eax = x + x
 *          ret
 *
 *  WHY: `lea` with base+index runs on the address-generation unit, which
 *  is free of port pressure on the ALU; it also doesn't clobber flags.
 *  On Intel `shl` would be the same latency but contests for ALU port 1.
 */
uint32_t u_mul2(uint32_t x)  { return x * 2; }

/*  u_mul8 — x*8 → x<<3 → 8*x lea.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      u_mul8:
 *          lea  eax, [8*rdi]        ; eax = 8*x via address-mode scale
 *          ret
 *
 *  Note that `lea` supports scale ∈ {1,2,4,8}, so *2, *3, *4, *5, *8, *9
 *  all become a single lea.
 */
uint32_t u_mul8(uint32_t x)  { return x * 8; }

/*  u_mul9 — x*9 → 8*x + x via lea base+8*idx.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      u_mul9:
 *          lea  eax, [rdi + 8*rdi]  ; eax = x + 8*x = 9*x  — one instruction!
 *          ret
 *
 *  Likewise *3 = `lea [rdi + 2*rdi]`, *5 = `lea [rdi + 4*rdi]`.
 */
uint32_t u_mul9(uint32_t x)  { return x * 9; }

/*  u_mul10 — x*10 → 2x then 5*(2x) = 10x.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      u_mul10:
 *          add  edi, edi            ; edi = 2*x       (could also be lea)
 *          lea  eax, [rdi + 4*rdi]  ; eax = 5*2x = 10*x
 *          ret
 *
 *  WHY: 10 ≠ scale-supportable; the selector decomposes 10 = 2 * 5 and
 *  uses two cheap instructions instead of `imul eax, 10` (3-cycle).
 *  Total: 2 cycles, single µop each, no flag clobber on the lea.
 */
uint32_t u_mul10(uint32_t x) { return x * 10; }

/*  u_mul7 — x*7 → 8*x - x.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      u_mul7:
 *          lea  eax, [8*rdi]        ; eax = 8*x
 *          sub  eax, edi            ; eax = 8*x - x = 7*x
 *          ret
 *
 *  This is "shift-and-subtract", picked because 7 is `2^k - 1` for k=3.
 *  In general, the selector picks among shift+add, shift+sub, lea, and
 *  imul based on a per-target latency table.
 */
uint32_t u_mul7(uint32_t x)  { return x * 7; }

/*  u_div2 — unsigned divide by 2 is just a logical shift.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      u_div2:
 *          mov  eax, edi
 *          shr  eax                 ; eax >>= 1     (single-bit shift)
 *          ret
 *
 *  WHY: For *unsigned* division by a power of two, `x / 2^k = x >> k`
 *  exactly, with no rounding correction.
 */
uint32_t u_div2(uint32_t x)  { return x / 2; }

/*  u_div10 — unsigned divide by 10 → multiply by magic constant.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      u_div10:
 *          mov  ecx, edi
 *          mov  eax, 3435973837     ; = 0xCCCCCCCD = ceil(2^35 / 10)
 *          imul rax, rcx            ; full 64-bit product
 *          shr  rax, 35             ; take high bits = floor(x/10)
 *          ret
 *
 *  WHY: The Granlund-Montgomery algorithm replaces `x / d` by
 *      (x * M) >> s
 *  where M and s are precomputed constants depending on d and the width.
 *  An imul is 3 cycles and a shr is 1; total 4 cycles vs ~25 for `div`.
 *  The optimizer always prefers this for constant divisors.
 */
uint32_t u_div10(uint32_t x) { return x / 10; }

/*  s_div2 — signed divide by 2 is NOT just a shift!
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      s_div2:
 *          mov  eax, edi
 *          shr  eax, 31             ; isolate sign bit (0 or 1)
 *          add  eax, edi            ; eax = x + sign_bit
 *          sar  eax                 ; arithmetic shift right by 1
 *          ret
 *
 *  WHY: C / C++ require *truncation toward zero*. `sar` truncates toward
 *  -∞ (e.g. -1 >> 1 = -1, but -1 / 2 must be 0). The fix is the classic
 *  "round-toward-zero correction": add `(x < 0)` to x before shifting,
 *  so negative odd numbers gain 1 and round up to zero correctly.
 */
int32_t s_div2(int32_t x)   { return x / 2; }

/*  u_mod16 vs s_mod16 — same divisor, very different code.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      u_mod16:
 *          mov  eax, edi
 *          and  eax, 15             ; one AND, done.
 *          ret
 *
 *      s_mod16:
 *          mov  eax, edi
 *          lea  ecx, [rax + 15]     ; rounding correction = x + 15
 *          test edi, edi
 *          cmovns ecx, edi          ; if x ≥ 0 → use x; else use x+15
 *          and  ecx, -16            ; floor((corrected)/16) * 16
 *          sub  eax, ecx            ; x - that = signed mod 16
 *          ret
 *
 *  WHY: Unsigned mod-2^k is `x & (2^k - 1)`. Signed isn't, because for
 *  e.g. x = -1, `-1 & 15 = 15` (wrong) whereas `-1 % 16 = -1`. The
 *  generated code computes the truncation correctly without using `idiv`.
 */
int32_t  s_mod16(int32_t x)   { return x % 16; }
uint32_t u_mod16(uint32_t x)  { return x % 16; }

/*  scale_index — loop-strength-reduction preview.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Naïve emission would multiply (i * 4) every iteration.
 *  After IV-strength reduction the loop holds a *pointer* that increments
 *  by 16 (= 4 * sizeof(int)) each iteration; no multiply needed in the body.
 *  See 02_loop_optimizations/09_iv_simplification.c.
 */
void scale_index(int *a, int n) {
    for (int i = 0; i < n; i++) {
        a[i * 4] = i;
    }
}
