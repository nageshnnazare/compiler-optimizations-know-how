/* 09_bit_tricks.c — BIT-LEVEL IDIOM RECOGNITION
 * ============================================================================
 *
 * Both GCC and LLVM have explicit pattern matchers that recognise hand-
 * rolled bit-manipulation idioms and lower them to dedicated machine
 * instructions when the target has them.
 *
 *   Source idiom                              Lowered to
 *   ──────────────────────────────            ─────────────────────────────
 *   x & -x                                    BLSI       (BMI1)
 *   x & (x - 1)                               BLSR       (BMI1)
 *   popcount loop                              POPCNT     (-mpopcnt)
 *   ctz loop                                   TZCNT/BSF
 *   (x << n) | (x >> (W - n))                  ROL        (always available)
 *   ((u64)hi << 32) | lo                       single 64-bit load
 *   byte-swap pattern                          BSWAP
 *   adjacent-byte loads + shifts               wide load (chapter 6)
 *
 * Why this matters
 * ----------------
 *   On modern x86, POPCNT is 3 cycles for the whole word; the equivalent
 *   loop is ~64 cycles. TZCNT is 3 cycles; the loop is ~32. BLSI/BLSR
 *   replace a 2-instruction sequence with a single 1-cycle one.
 * ============================================================================
 */
#include <stdint.h>

/*  isolate_lowest_set — `x & -x`
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3, baseline x86-64):
 *      isolate_lowest_set:
 *          mov  eax, edi
 *          neg  eax
 *          and  eax, edi
 *          ret
 *
 *  With `-mbmi`:
 *      isolate_lowest_set:
 *          blsi eax, edi            ; ONE instruction
 *          ret
 *
 *  WHY: BMI1 (Haswell+) added BLSI specifically for this idiom. The
 *  compiler keeps the 3-instruction expansion when targeting baseline
 *  CPUs that lack BMI1; the IR is identical, only the lowering changes.
 */
uint32_t isolate_lowest_set(uint32_t x)   { return x & -x; }

/*  clear_lowest_set — `x & (x - 1)`
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      clear_lowest_set:
 *          lea  eax, [rdi - 1]      ; eax = x - 1
 *          and  eax, edi            ; eax = (x-1) & x
 *          ret
 *
 *  With `-mbmi`: a single `blsr` instruction.
 */
uint32_t clear_lowest_set(uint32_t x)     { return x & (x - 1); }

/*  popcount_loop — Brian Kernighan's bit-count loop.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3, baseline x86-64):
 *      popcount_loop:
 *          xor  eax, eax
 *          test edi, edi
 *          je   .end
 *      .body:
 *          inc  eax
 *          lea  ecx, [rdi - 1]
 *          and  ecx, edi
 *          mov  edi, ecx
 *          jne  .body
 *      .end:
 *          ret
 *
 *  With `-mpopcnt`:
 *      popcount_loop:
 *          popcnt eax, edi          ; ONE instruction; constant 3 cycles
 *          ret
 *
 *  WHY: LoopIdiomRecognize sees the Kernighan pattern (`n++; x &= x-1`)
 *  and replaces the entire loop with `popcnt` when available. The bound-
 *  by-set-bits loop becomes a constant-time op.
 */
int popcount_loop(uint32_t x) {
    int n = 0;
    while (x) { n++; x &= x - 1; }
    return n;
}

/*  ctz_loop — count trailing zeros by repeated shift.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3, baseline x86-64): a loop similar in shape to popcount_loop
 *  above. With `-mbmi`:
 *      ctz_loop:
 *          tzcnt eax, edi           ; counts the trailing zero bits in one op
 *          ret
 *
 *  WHY: Same mechanism. The compiler hangs onto the loop only as a
 *  fallback when no hardware ctz instruction is available on the target.
 */
int ctz_loop(uint32_t x) {
    int n = 0;
    while ((x & 1u) == 0u) { x >>= 1; n++; }
    return n;
}

/*  rotl — rotate-left idiom.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      rotl:
 *          mov  ecx, esi
 *          mov  eax, edi
 *          rol  eax, cl             ; ONE rotate instruction
 *          ret
 *
 *  WHY: x86 has a native ROL since the 8086. The compiler recognises the
 *  *exact* pattern `(x << n) | (x >> ((W - n) & (W-1)))` and replaces it
 *  with ROL. The masking on the shift count is essential to avoid the
 *  UB of shifting by ≥ width.
 */
uint32_t rotl(uint32_t x, unsigned n) {
    n &= 31u;
    return (x << n) | (x >> ((32 - n) & 31u));
}

/*  join_halves — combine two 32-bit halves into a 64-bit value.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      join_halves:
 *          shl  rdi, 32             ; hi << 32
 *          mov  eax, esi            ; eax = lo (zero-extends to rax)
 *          or   rax, rdi            ; combine
 *          ret
 *
 *  Three instructions; would be `lea rax, [rdi*0x100000000 + rsi]`
 *  except `lea` doesn't support scale > 8. The selector picks the
 *  shl/mov/or sequence.
 */
uint64_t join_halves(uint32_t hi, uint32_t lo) {
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

/*  bytes_to_word — four byte loads + shifts collapse to one 32-bit load.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3, little-endian x86):
 *      bytes_to_word:
 *          mov  eax, dword ptr [rdi] ; ONE 32-bit load — no shifts, no ORs
 *          ret
 *
 *  WHY: The "load combining" pass (a.k.a. `Load Combine` in LLVM,
 *  `store-merging` and friends in GCC) recognizes adjacent narrow loads
 *  with the byte-shift pattern and replaces them with a single wide
 *  load. Requires the target to be little-endian *and* the address to be
 *  loadable as a 4-byte access (alignment OK on x86).
 *
 *  For big-endian targets the result is a wide load + `bswap`.
 */
uint32_t bytes_to_word(const uint8_t *p) {
    return  (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/*  bswap_pattern — recognized byte-swap.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      bswap_pattern:
 *          mov   eax, edi
 *          bswap eax                ; ONE instruction
 *          ret
 *
 *  WHY: x86's BSWAP swaps the byte order of a 32-bit register. The
 *  compiler matches the canonical "mask-shift-OR" pattern of a byte swap
 *  and emits the dedicated instruction. The same applies on aarch64
 *  (`rev w0, w0`) and POWER (`brw`).
 */
uint32_t bswap_pattern(uint32_t x) {
    return ((x & 0x000000FFu) << 24)
         | ((x & 0x0000FF00u) << 8)
         | ((x & 0x00FF0000u) >> 8)
         | ((x & 0xFF000000u) >> 24);
}
