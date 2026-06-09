/* 03_alias_analysis.c — ALIAS ANALYSIS FROM THE USER'S SIDE
 * ============================================================================
 *
 * Alias analysis answers: "can pointer p and q ever point to overlapping
 * memory?" Three answers:
 *     NoAlias    – provably disjoint
 *     MustAlias  – provably the same address
 *     MayAlias   – everything else (the optimizer's worst case)
 *
 * Pass names
 * ----------
 *   LLVM:  BasicAA (instruction-level), TBAA (types), ScopedNoAliasAA,
 *          GlobalsAA, CFLAA.
 *   GCC :  alias.cc + points-to (tree-ssa-alias.cc).
 *
 * What gets STUCK without good AA
 * -------------------------------
 *   – LICM cannot hoist loads.
 *   – CSE cannot reuse loads across stores.
 *   – DSE cannot eliminate stores.
 *   – Vectorizer must emit runtime memchecks.
 *
 * Tools to give the optimizer better AA info
 * ------------------------------------------
 *   – `restrict` on pointer parameters (→ `noalias` attribute).
 *   – Stick to one type per object (TBAA assumes `int *` ≠ `float *`).
 *   – Use `memcpy` for type-punning (defined behaviour + lowered to one
 *     load).
 *   – Mark functions `pure` / `const` so calls don't pessimize AA.
 * ============================================================================
 */
#include <stddef.h>
#include <string.h>

/*  no_restrict — pointers may alias; b[0] must be reloaded each iter.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): the compiler emits a runtime alias check and TWO loops:
 *
 *      no_restrict:
 *          test  rdx, rdx
 *          je    .exit
 *          mov   eax, dword ptr [rsi]      ; load b[0] HOISTED into preheader
 *          cmp   rdx, 8
 *          jae   .vec
 *          ...
 *      .vec:                                 ; vectorized fast path
 *          movd  xmm0, eax
 *          pshufd xmm0, xmm0, 0              ; broadcast b[0]
 *          movdqu [rdi + 4*rsi      ], xmm0  ; vector store
 *          movdqu [rdi + 4*rsi + 16], xmm0
 *          add   rsi, 8
 *          cmp   rcx, rsi
 *          jne   .vec
 *      .scalar:
 *          mov   [rdi + 4*rcx], eax          ; scalar fallback
 *          inc   rcx
 *          cmp   rdx, rcx
 *          jne   .scalar
 *
 *  WAIT — the load IS hoisted even without restrict? YES, because the
 *  compiler emits a runtime memcheck. If a and b are far apart enough,
 *  the vector loop with hoisted broadcast runs. Otherwise the scalar
 *  loop with non-hoisted load runs. The behaviour is correct in both
 *  cases.
 *
 *  This is the "loop versioning for alias" pattern from chapter 02 #12.
 */
void no_restrict(int *a, const int *b, size_t n) {
    for (size_t i = 0; i < n; i++) a[i] = b[0];
}

/*  with_restrict — no runtime check needed.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): the runtime memcheck is GONE; only the vectorized
 *  loop + scalar tail remain. Smaller code, one less compare per call.
 *
 *  WHY: `restrict` ⇒ LLVM `noalias` parameter attribute. AA returns
 *  NoAlias for any (store through a) vs (load through b). LICM hoists
 *  the load unconditionally, the vectorizer doesn't need its memcheck.
 */
void with_restrict(int * restrict a, const int * restrict b, size_t n) {
    for (size_t i = 0; i < n; i++) a[i] = b[0];
}

/*  bad_type_pun — `*(float *)p` is STRICT-ALIASING UB.
 *  ──────────────────────────────────────────────────────────────────────────
 *  The C standard says you can't access an int* as float* unless via a
 *  char* or memcpy. TBAA (Type-Based Alias Analysis) assumes int and
 *  float-typed pointers point to DIFFERENT objects. Cast-aliasing
 *  breaks this assumption silently. The compiler might:
 *    – emit the load (current behaviour)
 *    – propagate stale values for *p that "couldn't have been written
 *      by the float reader" → wrong results in larger programs
 *    – outright delete the load
 *  Behaviour is undefined and varies by version/flags. DON'T DO THIS.
 */
float bad_type_pun(int *p) {
    return *(float *)p;
}

/*  good_type_pun — memcpy is the portable, defined idiom.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): the call to memcpy is OPTIMIZED AWAY:
 *      good_type_pun:
 *          movss xmm0, dword ptr [rdi]     ; ONE 32-bit float load
 *          ret
 *
 *  WHY: The optimizer recognises a sizeof-sized memcpy with constant
 *  size and replaces it with a single load. No function call, no
 *  byte-by-byte copy. TBAA doesn't get confused because memcpy is
 *  formally a sequence of `char` accesses, which are allowed to alias
 *  ANY type.
 */
float good_type_pun(int *p) {
    float f;
    memcpy(&f, p, sizeof f);
    return f;
}
