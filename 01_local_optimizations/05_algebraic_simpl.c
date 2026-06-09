/* 05_algebraic_simpl.c — ALGEBRAIC SIMPLIFICATION
 * ============================================================================
 *
 *   Replace expressions by mathematically-equivalent but cheaper forms.
 *
 *     Integer identities the compiler will ALWAYS apply
 *     -------------------------------------------------
 *     x + 0       → x                  x | 0       → x
 *     x * 1       → x                  x ^ 0       → x
 *     x * 0       → 0                  x & -1      → x
 *     x - x       → 0                  x & 0       → 0
 *     x ^ x       → 0                  (x>>a)>>b   → x >> (a+b)
 *     -(-x)       → x                  x << 0      → x
 *     ~~x         → x                  ~(-x)       → x - 1
 *     !!x         → x != 0             x / 1       → x
 *     x % 1       → 0
 *
 * Pass names
 * ----------
 *   LLVM: InstSimplify, InstCombine, Reassociate.
 *   GCC : fold-const.cc, tree-ssa-forwprop.cc, match.pd patterns.
 *
 * FLOATING-POINT — read carefully
 * -------------------------------
 *   By default these are NOT applied because IEEE-754 doesn't satisfy
 *   the usual axioms:
 *     x + 0.0   ≠ x  in general,   because  (-0.0) + 0.0 == +0.0
 *     x * 1.0   ≠ x  in general,   because  NaN * 1.0  is "NaN with sNaN bit"
 *     x - x     ≠ 0  in general,   because  NaN - NaN == NaN
 *     (a+b)+c   ≠ a+(b+c)          because rounding is order-dependent
 *
 *   Flags that grant permission:
 *     -ffast-math               (umbrella; turns on all of the below)
 *     -fno-signed-zeros         enables (x + 0.0) → x
 *     -ffinite-math-only        enables (x - x)   → 0
 *     -funsafe-math-optimizations  enables reassoc.
 *     -fno-trapping-math        enables removing FP ops whose results are
 *                               discarded but might trap.
 * ============================================================================
 */
#include <stdint.h>

/*  add_zero, mul_one, double_neg, shift_zero — identities that collapse to `mov`.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      add_zero:     mov eax, edi ; ret      ; identity: return x
 *      mul_one:      mov eax, edi ; ret      ; identity
 *      double_neg:   mov eax, edi ; ret      ; -(-x) = x
 *      shift_zero:   mov eax, edi ; ret      ; x << 0 = x
 *
 *  WHY: One InstCombine rewrite rule per identity. After they fire the
 *  function body is `ret %x`, and codegen emits a 1-instruction copy
 *  from EDI to EAX (the SysV-ABI return register).
 */
int add_zero(int x)    { return x + 0; }
int mul_one(int x)     { return x * 1; }
int double_neg(int x)  { return -(-x); }
int shift_zero(int x)  { return x << 0; }

/*  mul_zero, sub_self, xor_self — collapse to zero.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      mul_zero:   xor eax, eax ; ret        ; canonical "zero register"
 *      sub_self:   xor eax, eax ; ret
 *      xor_self:   xor eax, eax ; ret
 *
 *  WHY: All three fold to the constant 0. Codegen emits `xor reg,reg`,
 *  which is the *idiomatic* zeroing on x86:
 *     – 2 bytes (vs 5 for `mov eax, 0`)
 *     – on modern CPUs, the renamer recognizes it as a "zeroing idiom"
 *       and breaks the false dependency on EAX.
 */
int mul_zero(int x)   { return x * 0; }
int sub_self(int x)   { return x - x; }
int xor_self(int x)   { return x ^ x; }

/*  shift_chain — combine sequential shifts.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source:   (x >> 3) >> 5    ;; algebraically: x >> 8
 *
 *  ACTUAL (-O3):
 *      shift_chain:
 *          mov   eax, edi
 *          sar   eax, 8        ; ONE arithmetic shift by 8
 *          ret
 *
 *  WHY: InstCombine sees `(shr X, c1) shr c2` and rewrites to
 *  `shr X, c1+c2` provided `c1+c2 < bitwidth(X)`. For signed shifts
 *  it preserves SAR (arithmetic right shift) because `>>` on a signed
 *  int has implementation-defined behaviour that GCC/Clang define as
 *  arithmetic shift (sign extension).
 */
int shift_chain(int x) { return (x >> 3) >> 5; }

/*  bool_idiom — !!x becomes the canonical "is non-zero" pattern.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      bool_idiom:
 *          xor   eax, eax        ; zero return register
 *          test  edi, edi        ; flags = x & x   (compact "is x zero?")
 *          setne al              ; AL = (x != 0) ? 1 : 0
 *          ret
 *
 *  NOTE: `test edi, edi` is preferred over `cmp edi, 0` for the same
 *  reason — it's a one-byte-shorter encoding and breaks the dep on the
 *  constant 0 operand.
 */
int bool_idiom(int x)  { return !!x; }

/*  not_minus — ~(-x) = x - 1 in two's complement.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      not_minus:
 *          lea   eax, [rdi - 1]   ; ONE instruction: x - 1
 *          ret
 *
 *  WHY: For two's-complement integers, `~y` = `-y - 1`. Substituting
 *  `y = -x` gives `~(-x) = -(-x) - 1 = x - 1`. InstCombine has a
 *  specific rule for this; the instruction selector uses `lea` with
 *  displacement -1 to compute `x - 1` in one micro-op.
 */
int not_minus(int x)  { return ~(-x); }

/*  reassoc — (x + 1) + 2 → x + 3.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      reassoc:
 *          lea   eax, [rdi + 3]   ; ONE lea
 *          ret
 *
 *  WHY: Constant operands gather to one side via Reassociate, then
 *  fold. Even for signed ints (where overflow would be UB), the rule is
 *  legal because `(x + 1) + 2` and `x + 3` overflow on EXACTLY the same
 *  inputs.
 */
int reassoc(int x) { return (x + 1) + 2; }

/* ─── Floating-point: compare default vs -ffast-math ──────────────────── */

/*  fadd_zero — default compilation does NOT simplify.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3, no -ffast-math):
 *      fadd_zero:
 *          xorps xmm1, xmm1         ; xmm1 = 0.0f
 *          addss xmm0, xmm1         ; xmm0 = x + 0.0f
 *          ret
 *
 *  ACTUAL (-O3 -ffast-math) — would become:
 *      fadd_zero:
 *          ret                      ; xmm0 already holds x → identity
 *
 *  WHY: With signed zeros enabled (the default), x + 0.0f when x is -0.0
 *  must return +0.0, not -0.0. Removing the add is wrong in that one
 *  case. -fno-signed-zeros (subset of -ffast-math) opts out.
 */
float fadd_zero(float x)   { return x + 0.0f; }

/*  fmul_one — fmul by 1.0 is also blocked by default…
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): a single `ret`!
 *
 *      fmul_one:
 *          ret                      ; identity recognised
 *
 *  WHY: x * 1.0f IS recognized as identity by default, because IEEE-754
 *  defines `x * 1` to equal x for ALL x including NaN (the sNaN bit
 *  pattern is preserved by multiplication by 1.0). Contrast with the
 *  fadd case where signed zero is the obstacle.
 */
float fmul_one(float x)    { return x * 1.0f; }

/*  fsub_self — NOT folded by default.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      fsub_self:
 *          subss xmm0, xmm0         ; the subtract STAYS
 *          ret
 *
 *  WHY: For NaN inputs, `NaN - NaN == NaN`, not 0. Without
 *  -ffinite-math-only, the optimizer cannot prove the operand is finite,
 *  so the subtract is preserved.
 */
float fsub_self(float x)   { return x - x; }
