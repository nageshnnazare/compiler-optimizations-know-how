/* 03_peel.c — LOOP PEELING
 * ============================================================================
 *
 * Idea
 * ----
 *   Peel `k` iterations off the front (or back) of a loop. The peeled
 *   iterations execute outside the loop; the main loop then iterates
 *   from k.
 *
 * Why
 * ---
 *   – The first (or last) iteration may have a *different* value of
 *     some variable that constant-folds away in subsequent iterations.
 *   – The vectorizer peels until the pointer is properly aligned.
 *   – The unroller may peel to fix a non-multiple trip count.
 *
 * Pass names
 * ----------
 *   LLVM:  LoopPeelPass (and the vectorizer's own runtime peel).
 *   GCC :  -fpeel-loops, --param max-peeled-insns.
 *
 * Knobs
 * -----
 *   #pragma clang loop peel_count(N)
 *   -fpeel-loops, --param min-iters-for-peel
 * ============================================================================
 */
#include <stddef.h>

/*  peelable — running-sum / scan loop with a phi initialised to 0.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      peelable:
 *          test  rdx, rdx
 *          je    .exit
 *          mov   eax, edx
 *          and   eax, 3                    ; tail count
 *          cmp   rdx, 4
 *          jae   .unroll4                  ; main unrolled loop (factor 4)
 *          ...
 *      .unroll4:                            ; HEADER: 4 iters of fused scan
 *          and   rdx, -4
 *          xor   r8d,  r8d                  ; i = 0
 *          xor   ecx, ecx                   ; prev = 0
 *      .body:
 *          add   ecx, dword ptr [rsi + 4*r8       ] ; prev += b[i]
 *          mov   dword ptr [rdi + 4*r8       ], ecx ; a[i] = prev
 *          add   ecx, dword ptr [rsi + 4*r8 +  4]   ; prev += b[i+1]
 *          mov   dword ptr [rdi + 4*r8 +  4], ecx
 *          add   ecx, dword ptr [rsi + 4*r8 +  8]
 *          mov   dword ptr [rdi + 4*r8 +  8], ecx
 *          add   ecx, dword ptr [rsi + 4*r8 + 12]
 *          mov   dword ptr [rdi + 4*r8 + 12], ecx
 *          add   r8, 4
 *          cmp   rdx, r8
 *          jne   .body
 *      .tail:                               ; scalar epilogue for n % 4
 *          ; ...
 *
 *  WHY: The LLVM cost model decided NOT to formally "peel" here, instead
 *  unrolling by 4 + emitting a scalar epilogue. But the *concept* of
 *  peeling is what allows the first iteration's `prev=0` to fold away in
 *  cases where the loop body would otherwise contain a conditional on
 *  whether we're past the cold-start iteration. In this particular
 *  example the back-edge phi already has a known init value so the body
 *  needs no special case.
 */
void peelable(int *a, const int *b, size_t n) {
    int prev = 0;
    for (size_t i = 0; i < n; i++) {
        a[i] = prev + b[i];
        prev = a[i];
    }
}

/*  align_then_vector — vectorizer-driven runtime peel for alignment.
 *  ──────────────────────────────────────────────────────────────────────────
 *  When the compiler can't prove `a` and `b` are aligned, it can emit a
 *  *runtime peel*: scalar iterations until the pointer reaches alignment,
 *  then a vector main loop with aligned loads/stores, then a scalar tail.
 *
 *  The Intel asm conceptually looks like:
 *      align_then_vector:
 *          ; n == 0 early-out
 *          ; compute mis-alignment k = (16 - (a & 15)) / 4
 *          .scalar_peel:                ; 0..3 iters until a is 16-byte aligned
 *              addss xmm0, [rsi]
 *              movss [rdi], xmm0
 *              add   rdi, 4 ; add rsi, 4 ; dec k ; jnz .scalar_peel
 *          .vec_aligned_main:
 *              movaps xmm0, [rsi]
 *              addps  xmm0, [rdi]
 *              movaps [rdi], xmm0
 *              ; ... repeat for n/4 ...
 *          .scalar_tail:                ; 0..3 iters at the end
 *              ; ...
 *
 *  Modern Intel/AMD make unaligned movups nearly as fast as aligned,
 *  so this peel is often skipped at -O3 unless the cost model thinks
 *  the savings are non-trivial.
 */
void align_then_vector(float *a, const float *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        a[i] = a[i] + b[i];
    }
}
