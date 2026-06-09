/* 01_licm.c — LOOP-INVARIANT CODE MOTION
 * ============================================================================
 *
 * What it is
 * ----------
 *   Any computation whose operands don't depend on the loop's induction
 *   variable AND that has no side effects is moved into the loop's
 *   *preheader* — executed exactly once instead of N times.
 *
 *   Picture:
 *                ┌──────────────┐
 *                │  preheader   │ ◄── t = x*y + k   (HOISTED HERE)
 *                └──────┬───────┘
 *                       ▼
 *              ┌──── header  ──────┐
 *              │ φ i               │
 *              │ cmp i, n          │
 *              │ br exit           │
 *              └──┬────────────┬───┘
 *           body  │            │ exit
 *                 ▼            ▼
 *           a[i] = t*a[i]    [exit BB]
 *                 │
 *                 ▼
 *              latch (i++)
 *                 └──► header
 *
 * Pass names
 * ----------
 *   LLVM:  LICMPass; uses MemorySSA for loads/stores.
 *   GCC :  tree-ssa-loop-im.cc (Invariant Motion).
 *
 * Legality
 * --------
 *   – Operands defined outside the loop.
 *   – No side effects (`!hasSideEffects()`).
 *   – Speculatable: hoisting it past the loop guard must not crash. A load
 *     from a possibly-null pointer is NOT speculatable; division by a
 *     possibly-zero divisor is NOT speculatable.
 * ============================================================================
 */
#include <stddef.h>

/*  licm_hoist — hoist a 3-instruction expression out of the loop.
 *  ──────────────────────────────────────────────────────────────────────────
 *  WHAT WE'D EXPECT WITHOUT LICM:
 *      ; body: re-compute x*y+k each iter
 *      .loop:
 *          mov   eax, dword ptr [rdi + 4*rcx]
 *          mov   ebx, edx       ; x
 *          imul  ebx, esi       ; x*y          ← every iteration
 *          add   ebx, r8d       ; +k           ← every iteration
 *          imul  eax, ebx
 *          mov   dword ptr [rdi + 4*rcx], eax
 *          inc   rcx
 *          cmp   rcx, rsi
 *          jl    .loop
 *
 *  ACTUAL (-O3, abbreviated; full vectorization on):
 *      licm_hoist:
 *          test  rsi, rsi
 *          je    .exit
 *          imul  ecx, edx                      ; ←─┐
 *          add   ecx, r8d                      ; ←─┴── HOISTED into preheader
 *                                              ;       (executed ONCE)
 *          cmp   rsi, 8
 *          jae   .vec_loop                     ;  vectorized main loop
 *          ...
 *      .vec_loop:
 *          movdqu xmm1, [rdi + 4*rdx]
 *          movdqu xmm2, [rdi + 4*rdx + 16]
 *          pmulld xmm1, xmm0                   ; xmm0 is `t` broadcast
 *          pmulld xmm2, xmm0
 *          movdqu [rdi + 4*rdx], xmm1
 *          movdqu [rdi + 4*rdx + 16], xmm2
 *          add   rdx, 8
 *          cmp   rax, rdx
 *          jne   .vec_loop
 *
 *  WHY: LICM walks the loop body, asks for each instruction
 *       "are all your operands loop-invariant? are you side-effect free
 *        and speculatable?" If yes, splice into the preheader. Then LICM
 *  is given a clean body which the vectorizer can SIMD-ify; without
 *  hoisting, the per-iter recomputation would PREVENT vectorization
 *  because the body would contain non-vectorizable serial operations.
 */
void licm_hoist(int *a, size_t n, int x, int y, int k) {
    for (size_t i = 0; i < n; i++) {
        a[i] = a[i] * (x * y + k);
    }
}

/*  licm_load — memory LICM with `restrict`.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      licm_load:
 *          test  rdx, rdx
 *          je    .exit
 *          mov   eax, dword ptr [rsi]   ; ←── LOAD b[0] HOISTED into preheader
 *          ...
 *      .vec_loop:
 *          movdqu xmm1, [rdi + 4*rsi]
 *          paddd  xmm1, xmm0            ; xmm0 = broadcast(b[0])
 *          movdqu [rdi + 4*rsi], xmm1
 *          ...
 *
 *  WHY: `restrict` ⇒ noalias. Alias Analysis returns NoAlias between
 *  `*b` (the hoisted load) and any store through `*a`. With that, the
 *  load is loop-invariant — its memory is provably not written by the
 *  loop body — and LICM is free to lift it.
 */
void licm_load(int * restrict a, const int * restrict b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        a[i] += b[0];
    }
}

/*  licm_blocked — same function MINUS restrict, watch what happens.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): the compiler emits a *runtime alias check* and TWO loops:
 *
 *      licm_blocked:
 *          ; runtime memcheck: does a[0..n] overlap b[0..1]?
 *          lea   rax, [rdi + 4*rdx]
 *          lea   rcx, [rsi + 4]
 *          cmp   rdi, rcx
 *          setb  cl                     ; a-start < b-end?
 *          cmp   rsi, rax
 *          setb  al                     ; b-start < a-end?
 *          test  al, cl                 ; both true → overlap
 *          jne   .scalar_path           ; aliased → fall back to scalar
 *          ; non-aliased fast path:
 *          mov   eax, dword ptr [rsi]   ; load b[0] HOISTED (vectorized too)
 *          ...
 *      .scalar_path:
 *          ; reload b[0] inside the loop (no hoist) because aliasing is possible
 *          ...
 *
 *  WHY: Without `restrict` the compiler can't prove no-alias at compile
 *  time. Instead it *versions* the loop (chapter 02 #12): a runtime
 *  pointer comparison picks the fast path when the pointers don't
 *  overlap. The fast path is identical to `licm_load` above; the slow
 *  path has the load INSIDE the loop body.
 */
void licm_blocked(int *a, const int *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        a[i] += b[0];
    }
}

/*  licm_div_unsafe — a divide that CANNOT be speculated.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): `1000 / d` is computed INSIDE the loop, every iteration.
 *
 *  WHY: If we hoisted `1000/d` into the preheader and `d == 0`, the
 *  preheader would trigger a SIGFPE that the original program would NOT
 *  have raised when `n == 0` (because the body would never execute).
 *  LICM refuses to hoist potentially-trapping operations past the loop's
 *  trip-count test.
 */
void licm_div_unsafe(int *a, size_t n, int d) {
    for (size_t i = 0; i < n; i++) {
        a[i] = a[i] + 1000 / d;
    }
}

/*  licm_div_safe — guard makes the divide speculatable; hoist proceeds.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Manual hoist works too — but here we just gate with `if (d == 0) return`.
 *  Now the compiler KNOWS d is non-zero when the divide executes; the
 *  divide is hoisted into the preheader and the loop becomes pure
 *  add-by-constant, which then vectorizes.
 */
void licm_div_safe(int *a, size_t n, int d) {
    if (d == 0) return;
    int inv = 1000 / d;
    for (size_t i = 0; i < n; i++) {
        a[i] = a[i] + inv;
    }
}
