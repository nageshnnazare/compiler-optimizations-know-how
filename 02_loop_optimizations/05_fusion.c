/* 05_fusion.c — LOOP FUSION (jamming)
 * ============================================================================
 *
 * Idea
 * ----
 *   Two adjacent loops with the same trip count and compatible
 *   dependencies become one. Halves the loop overhead and improves cache
 *   locality (a[i] written by loop 1 is still in the L1 when loop 2
 *   reads it).
 *
 *     BEFORE                                  AFTER
 *     ──────                                  ─────
 *     for (i=0;i<n;i++) a[i] = b[i] + 1;       for (i=0;i<n;i++) {
 *     for (i=0;i<n;i++) c[i] = a[i] * 2;          int t = b[i] + 1;
 *                                                  a[i] = t;
 *                                                  c[i] = t * 2;     ; or 2*(b[i]+1)
 *                                              }
 *
 * Legality
 * --------
 *   – Same loop bounds.
 *   – For every iteration i in the fused loop, all reads in `loop2(i)`
 *     of values produced by loop1 must read iteration-i's value, NOT
 *     iteration-(i±k)'s value.
 *
 * Pass names
 * ----------
 *   LLVM:  LoopFusePass  (experimental; -mllvm -enable-loop-fusion).
 *   GCC :  tree-ssa-loop-distribute.cc handles both fusion and fission.
 * ============================================================================
 */
#include <stddef.h>

/*  fusable_pair — IDEAL candidate.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3) note: LLVM does NOT currently fuse this by default
 *  (fusion is conservative; the cost model often vectorizes each loop
 *  independently). The asm shows TWO vector loops emitted:
 *
 *      .loop1_vec:                              ; a[i] = b[i] + 1
 *          movdqu xmm1, [rdx + 4*r8]
 *          movdqu xmm2, [rdx + 4*r8 + 16]
 *          psubd  xmm1, xmm0                    ; subtract -1 (which is +1)
 *          psubd  xmm2, xmm0                     ; (xmm0 = -1 broadcast)
 *          movdqu [rdi + 4*r8],      xmm1
 *          movdqu [rdi + 4*r8 + 16], xmm2
 *          ...
 *      .loop2_vec:                              ; c[i] = a[i] * 2
 *          ...
 *
 *  WHAT IDEAL FUSION WOULD PRODUCE:
 *      .fused_loop:
 *          movdqu xmm1, [rdx + 4*r8]            ; load b[i:i+4]
 *          psubd  xmm1, xmm0                    ; +1
 *          movdqu [rdi + 4*r8], xmm1            ; store a[i:i+4]
 *          paddd  xmm2, xmm1, xmm1              ; t * 2
 *          movdqu [rcx + 4*r8], xmm2            ; store c[i:i+4]
 *          ...
 *      WIN: ONE pass over the index range, the a[i] write/read pair
 *      stays in L1, two loops of overhead become one.
 *
 *  Apply the fusion manually if the heuristic doesn't fire:
 *      for (size_t i = 0; i < n; i++) {
 *          int t = b[i] + 1;
 *          a[i] = t;
 *          c[i] = t * 2;
 *      }
 */
void fusable_pair(int *a, int *c, const int *b, size_t n) {
    for (size_t i = 0; i < n; i++) a[i] = b[i] + 1;
    for (size_t i = 0; i < n; i++) c[i] = a[i] * 2;
}

/*  not_fusable — illegal because loop2 reads a[i+1], not a[i].
 *  ──────────────────────────────────────────────────────────────────────────
 *  Trying to fuse this would read a[i+1] BEFORE loop 1 has written it,
 *  changing program semantics. The pass quickly rejects the candidate
 *  via dependence analysis on the array subscript.
 */
void not_fusable(int *a, int *c, const int *b, size_t n) {
    for (size_t i = 0; i < n; i++)     a[i] = b[i] + 1;
    for (size_t i = 0; i + 1 < n; i++) c[i] = a[i + 1] * 2;
}
