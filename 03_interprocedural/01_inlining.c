/* 01_inlining.c — INLINING MECHANICS
 * ============================================================================
 *
 * What it is
 * ----------
 *   Replace `call f(args)` at the call site with a *copy* of f's body,
 *   with parameters substituted for arguments. Then re-run all the local
 *   optimizations on the merged code.
 *
 * Why it is the most important optimization
 * -----------------------------------------
 *
 *      inline ─┬─► constants in args flow into callee → constant-fold
 *              ├─► dead args removed from callee
 *              ├─► alias-analysis no longer hits the "opaque call" wall
 *              ├─► loop opts on the inlined body
 *              └─► tail-call merge with caller's epilogue
 *
 *   Almost every other inter-procedural optimization is a special case
 *   or downstream consequence of inlining.
 *
 * Cost model
 * ----------
 *   Each candidate gets a (cost, benefit) score. Inline iff
 *   `benefit - cost ≥ threshold`. The cost counts callee instructions
 *   that survive inlining; the benefit counts compile-time-folded
 *   computation in the merged code.
 *
 *   Knobs:
 *     LLVM:  -mllvm -inline-threshold=N            default ~225
 *     GCC :  --param max-inline-insns-single=N     default ~70
 * ============================================================================
 */
#include <stddef.h>

/* static + inline = "may be inlined; emit a copy only if needed." */
static inline int square(int x) { return x * x; }

/*  caller_const — constant arg lets the body fold completely.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source:  return square(7);
 *
 *  WHAT YOU'D NAIVELY EXPECT WITHOUT INLINING:
 *      caller_const:
 *          mov  edi, 7
 *          call square
 *          ret
 *  AND   square:
 *          imul edi, edi
 *          mov  eax, edi
 *          ret
 *
 *  ACTUAL (-O3):
 *      caller_const:
 *          mov  eax, 49         ; everything folded to a literal
 *          ret
 *
 *  WHY: inlining substitutes `x = 7` into square's body → `7 * 7`
 *  → constant folder → 49. The original call site disappears, and
 *  `square` either disappears from the binary (it was `static inline`)
 *  or remains only as a possible callee for other TUs (none here).
 */
int caller_const(void) {
    return square(7);
}

/*  caller_var — variable arg; inlined but cannot fold.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      caller_var:
 *          lea   eax, [rdi + 1]   ; eax = n + 1
 *          imul  eax, eax         ; eax = (n+1) * (n+1)
 *          ret
 *
 *  ONE function body, two instructions, NO call. The compiler did:
 *    inline square(n+1) → (n+1) * (n+1)
 *    instruction-select  → lea + imul
 */
int caller_var(int n) {
    return square(n + 1);
}

__attribute__((always_inline)) static inline int double_it(int x) { return x + x; }

/*  caller_force — always_inline + algebraic simplification.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source:  return double_it(n) + double_it(n+1);
 *
 *  IDEAL TRANSFORMATION:
 *      double_it(n)   = 2n
 *      double_it(n+1) = 2(n+1) = 2n + 2
 *      sum            = 4n + 2
 *
 *  ACTUAL (-O3):
 *      caller_force:
 *          lea  eax, [4*rdi + 2]   ; ONE lea: 4n + 2
 *          ret
 *
 *  WHY: After both calls inline, InstCombine sees `(n+n) + ((n+1)+(n+1))`,
 *  reassociates to `4n + 2`, and the selector emits the lea pattern.
 *  The two helper calls become ZERO machine instructions.
 */
int caller_force(int n) { return double_it(n) + double_it(n + 1); }

/*  slow_path / hot_path — noinline + cold attribute.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      hot_path:
 *          cmp  edi, 1001
 *          jge  slow_path         ; TAIL CALL on the unlikely branch
 *          add  edi, edi          ; hot path: x * 2
 *          mov  eax, edi
 *          ret
 *      slow_path:
 *          ... (real implementation: arithmetic sequence sum)
 *
 *  WHAT WE LOSE BY MARKING `slow_path` NOINLINE: a function call. WHAT
 *  WE GAIN: hot_path is 4 instructions and fits in 16 bytes of I-cache;
 *  slow_path's loop doesn't bloat the hot function. Common pattern for
 *  rare error paths.
 */
__attribute__((noinline)) static int slow_path(int x) {
    int s = 0;
    for (int i = 0; i < x; i++) s += i;
    return s;
}

int hot_path(int x) {
    if (__builtin_expect(x > 1000, 0)) return slow_path(x);
    return x * 2;
}

/*  factorial / call_fact — recursion + full unrolling.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source:
 *      int factorial(int n) { return n<=1 ? 1 : n * factorial(n-1); }
 *      int call_fact(void)  { return factorial(5); }
 *
 *  IDEAL: const-fold factorial(5) → 120; emit `mov eax, 120; ret`.
 *  ACTUAL (-O3): the compiler often does exactly that for tiny constant
 *  args. For larger args it inlines partially and may leave the
 *  recursive call.
 *
 *  Note: `factorial` itself in the asm above shows the *standalone*
 *  emitted version (with SSE vectorization of the running product
 *  triggered by SCEV recognising the multiplication chain). That body
 *  is only emitted because the function might be called from another
 *  TU. With LTO it would disappear if call_fact were the only caller.
 */
int factorial(int n) {
    return n <= 1 ? 1 : n * factorial(n - 1);
}

int call_fact(void) {
    return factorial(5);
}
