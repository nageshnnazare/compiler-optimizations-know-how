/* 10_rerolling.c — LOOP REROLLING
 * ============================================================================
 *
 * Idea
 * ----
 *   The OPPOSITE of unrolling. If source code is already (hand-)
 *   unrolled, sometimes the optimizer prefers a clean loop. LoopReroll
 *   detects the pattern and rebuilds the loop, then later the vectorizer
 *   may SIMD-ify it.
 *
 *   In practice modern compilers SLP-vectorize unrolled straight-line
 *   code directly, so explicit rerolling fires rarely. The example
 *   below is what you DO see: the SLP vectorizer recognised four
 *   independent loads + adds and fused them into one wide SIMD reduce.
 *
 * Pass names
 * ----------
 *   LLVM:  LoopRerollPass; also SLPVectorizerPass for the straight-line
 *          case.
 *   GCC :  rerolling is not a separate pass; SLP handles most cases.
 * ============================================================================
 */

/*  unrolled4 — a hand-unrolled 4-element sum.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source: return a[0] + a[1] + a[2] + a[3];
 *
 *  NAIVE EXPECTED (-O3, no SLP):
 *      mov  eax, [rdi]
 *      add  eax, [rdi + 4]
 *      add  eax, [rdi + 8]
 *      add  eax, [rdi + 12]
 *      ret
 *
 *  ACTUAL (-O3):
 *      unrolled4:
 *          movdqu xmm0, xmmword ptr [rdi]    ; load all 4 ints at once
 *          pshufd xmm1, xmm0, 0xEE           ; [a2,a3,a2,a3] (high half)
 *          paddd  xmm1, xmm0                 ; [a0+a2, a1+a3, ...]
 *          pshufd xmm0, xmm1, 0x55           ; broadcast lane 1
 *          paddd  xmm0, xmm1                 ; lane0 += lane1
 *          movd   eax, xmm0                  ; extract lane 0
 *          ret
 *
 *  WHY: SLP-vectorizer noticed 4 adjacent contiguous loads + an
 *  associative reduction tree, fused them into one 128-bit load, and
 *  used SSE shuffle/add for the horizontal reduce. Fewer total µops
 *  *and* shorter critical path than the scalar chain.
 */
int unrolled4(const int *a) {
    return a[0] + a[1] + a[2] + a[3];
}

/*  unrolled8 — same idea, 8 elements; two 16-byte loads then reduce.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      unrolled8:
 *          movdqu xmm0, [rdi]
 *          movdqu xmm1, [rdi + 16]
 *          paddd  xmm1, xmm0                  ; element-wise sum of the
 *                                              ; two halves
 *          pshufd xmm0, xmm1, 0xEE            ; horizontal reduce as above
 *          paddd  xmm0, xmm1
 *          pshufd xmm1, xmm0, 0x55
 *          paddd  xmm1, xmm0
 *          movd   eax, xmm1
 *          ret
 */
int unrolled8(const int *a) {
    return a[0] + a[1] + a[2] + a[3]
         + a[4] + a[5] + a[6] + a[7];
}

/*  store_unrolled — four identical stores → broadcast + single vector store.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      store_unrolled:
 *          movd   xmm0, esi                  ; xmm0 = [v, 0, 0, 0]
 *          pshufd xmm0, xmm0, 0              ; xmm0 = [v, v, v, v]
 *          movdqu xmmword ptr [rdi], xmm0     ; ONE 16-byte store
 *          ret
 *
 *  WHY: Storing the same value to 4 adjacent slots is a broadcast +
 *  vector-store pattern. SLP recognises it and emits the three-
 *  instruction sequence above instead of four mov-stores.
 */
void store_unrolled(int *a, int v) {
    a[0] = v;
    a[1] = v;
    a[2] = v;
    a[3] = v;
}
