/* 01_const_fold.c — CONSTANT FOLDING
 * ============================================================================
 *
 * What it is
 * ----------
 * Any expression whose operands are *all* compile-time constants is evaluated
 * by the compiler itself and replaced with the resulting constant. This is
 * the simplest, oldest, and most universally-applied optimization.
 *
 * Where it happens in the compiler
 * --------------------------------
 *   LLVM:  ConstantFolding, InstSimplify, SCCP (also folds along edges).
 *   GCC :  fold-const.cc + forwprop (forward-propagation through SSA).
 *
 * Why the compiler is allowed to do it
 * ------------------------------------
 *   The C abstract machine is deterministic on the operands (no I/O, no
 *   side effects). The compiler is allowed to evaluate any pure expression
 *   "as if" at any time, including at compile time. Volatile and atomic
 *   operands are *never* folded because their reads/writes are observable.
 *
 * The mental model
 * ----------------
 *
 *     source           AST                    after const-fold
 *     ----------       ------                 ----------------
 *     3 * 4 + 1         + (root)              13   ← whole tree collapses
 *                      / \                         to a single literal
 *                     *   1
 *                    / \
 *                   3   4
 *
 * Inspect:
 *   ../scripts/x86_asm.sh 01_const_fold.c -O3   # what we discuss below
 *   ../scripts/x86_asm.sh 01_const_fold.c -O0   # the "NAIVE" version
 *   ../scripts/diff_opt.sh 01_const_fold.c -O0 -O3
 * ============================================================================
 */
#include <stdint.h>

/*  compute_constexpr — every operand is a literal.
 *  ──────────────────────────────────────────────────────────────────────────
 *  NAIVE x86-64 (`clang -O0 --target=x86_64`):
 *
 *      compute_constexpr:
 *          mov   dword ptr [rsp - 4],  3      ; stack slot for `a`
 *          mov   dword ptr [rsp - 8],  4      ; stack slot for `b`
 *          mov   dword ptr [rsp - 12], 1      ; stack slot for `c`
 *          mov   eax, dword ptr [rsp - 4]     ; load a
 *          imul  eax, dword ptr [rsp - 8]     ; eax = a*b
 *          add   eax, dword ptr [rsp - 12]    ; eax += c
 *          ret
 *
 *  WHAT WE'D NAIVELY EXPECT a "smart" compiler to emit (e.g. just inline
 *  the math but keep the imul/add):
 *
 *      compute_constexpr:
 *          mov   eax, 3
 *          imul  eax, 4                       ; 1 imul
 *          add   eax, 1                       ; 1 add
 *          ret
 *
 *  ACTUAL OUTPUT (`clang -O3 --target=x86_64`):
 *
 *      compute_constexpr:
 *          mov   eax, 13                       ; ALL arithmetic vanished
 *          ret
 *
 *  WHY: InstSimplify walks the SSA def-use graph after mem2reg has
 *  promoted a/b/c to SSA names with literal values (3,4,1). At each
 *  binary op (mul, add) it sees `ConstantInt(...) op ConstantInt(...)`
 *  and replaces the op with the precomputed result. After one iteration
 *  the whole expression is `i32 13`, and DCE deletes the dead temporaries.
 */
int compute_constexpr(void) {
    int a = 3;
    int b = 4;
    int c = 1;
    return a * b + c;
}

/*  sizeof_check — sizeof is a constant expression by definition.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O0 and -O3 are IDENTICAL here):
 *
 *      sizeof_check:
 *          mov   eax, 1
 *          ret
 *
 *  WHY: sizeof(int) is folded *by the front-end* to the integer constant 4.
 *  The whole ternary becomes `(4 > 0) ? 1 : 0` which folds to 1. Front-end
 *  folding happens before any optimization pass runs, which is why even
 *  `-O0` gets it right.
 */
int sizeof_check(void) {
    return sizeof(int) > 0 ? 1 : 0;
}

/*  wrap_overflow — algebraic identity after folding.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *
 *      wrap_overflow:
 *          xor   eax, eax                      ; canonical "set EAX = 0"
 *          ret
 *
 *  WHY: `1 << 30` folds to 1073741824. Then `x*4` is `(1<<30)*4 = (1<<32)`,
 *  which in two's-complement int wraps. The compiler is allowed to assume
 *  signed overflow doesn't happen (UB) under default flags, so it computes
 *  the result symbolically: `(x*4) - (x*4)` ≡ 0 by the algebraic identity
 *  `a - a = 0`. SCCP+InstSimplify recognizes the pattern even without
 *  doing the wraparound arithmetic, so the function ends at the literal 0.
 *
 *  `xor reg,reg` is *the* idiomatic "zero a register" on x86 because:
 *    – 2 bytes (vs 5 for `mov eax,0`)
 *    – breaks the false dependency on the prior value of EAX
 *      (a hint to the OoO renamer that this register is fresh).
 */
int wrap_overflow(void) {
    int x = 1 << 30;
    return (x * 4) - (x * 4);
}

/*  compute_runtime — the compiler does NOT know x.
 *  ──────────────────────────────────────────────────────────────────────────
 *  WHAT YOU MIGHT NAIVELY EXPECT:
 *
 *      compute_runtime:
 *          mov   eax, edi          ; copy x
 *          imul  eax, 4            ; eax *= 4
 *          add   eax, 1            ; eax += 1
 *          ret
 *
 *  ACTUAL (-O3):
 *
 *      compute_runtime:
 *          lea   eax, [4*rdi + 1]  ; eax = 4*x + 1, ONE instruction, NO flags
 *          ret
 *
 *  WHY: `lea` ("load effective address") was invented for address math but
 *  is a perfect 3-operand integer ALU on x86. It computes `base + index*scale
 *  + disp` where `scale ∈ {1,2,4,8}`. The compiler's instruction selector
 *  recognises `4*x + 1` as exactly that pattern.
 *
 *  Bonus: `lea` does not update flags, so it doesn't clobber the EFLAGS
 *  register that a surrounding `cmp/jmp` may depend on. This is why
 *  compilers prefer `lea ecx, [eax+1]` over `mov ecx, eax ; inc ecx`.
 */
int compute_runtime(int x) {
    return x * 4 + 1;
}
