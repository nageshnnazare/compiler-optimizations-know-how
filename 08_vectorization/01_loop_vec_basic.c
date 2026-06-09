/* 01_loop_vec_basic.c — AUTO-VECTORIZATION 101
 * ============================================================================
 *
 * The vectorizer's job
 * --------------------
 *   Take a loop that processes one element per iteration and emit code
 *   that processes K elements at a time using a SIMD register (K = 4
 *   for SSE int, 8 for AVX2 int, 16 for AVX-512 int).
 *
 *     SCALAR              SIMD (VF=8, AVX2)
 *     ──────              ─────────────────
 *     mov   edx, [rsi]    vmovdqu ymm0, [rsi]      ; 8 ints in one register
 *     add   edx, [rcx]    vpaddd  ymm0, ymm0,
 *     mov   [rdi], edx              [rcx]
 *                         vmovdqu [rdi], ymm0
 *
 * The vectorizer's questions for each loop
 * ----------------------------------------
 *   1. Trip count: known? At least VF?
 *   2. Memory: do reads/writes overlap? (alias analysis)
 *   3. Reductions: associative? floating-point requires -ffast-math.
 *   4. Side effects: any divergent branches? Calls?
 *   5. Cost: is VF×scalar-cost > vector-cost + overhead?
 *
 * Why a vectorized loop is often EMITTED TWICE (or thrice)
 * --------------------------------------------------------
 *   Compiler emits up to FOUR variants for one loop:
 *     – guarded vector main loop (alias-check passed, n ≥ VF)
 *     – scalar tail loop for n % VF leftovers
 *     – scalar fallback when alias check fails
 *     – degenerate ε-loop for n == 0
 *
 * Pass names
 * ----------
 *   LLVM: LoopVectorizePass, VPlanCostModel.
 *   GCC : tree-vect-loop.cc, tree-vectorizer.cc.
 *
 * Optimization remarks (USE THESE)
 * --------------------------------
 *   clang -O3 -Rpass=loop-vectorize file.c          ; what got vectorized
 *   clang -O3 -Rpass-missed=loop-vectorize file.c   ; why something DIDN'T
 *   clang -O3 -Rpass-analysis=loop-vectorize file.c ; cost-model trace
 *   gcc   -O3 -fopt-info-vec[-missed]
 * ============================================================================
 */
#include <stddef.h>

/*  simple — c[i] = a[i] + b[i].
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3 -mavx2):
 *      simple:
 *          test rcx, rcx
 *          je   .exit                                ; n == 0
 *          cmp  rcx, 3
 *          jbe  .scalar_small                        ; n ≤ 3 → don't vectorize
 *          ; --- runtime memcheck (alias guard) ---
 *          mov  rax, rdi
 *          sub  rax, rsi
 *          cmp  rax, 128
 *          setb al
 *          mov  r8, rdi
 *          sub  r8, rdx
 *          cmp  r8, 128
 *          setb r8b
 *          or   r8b, al
 *          je   .vec_main
 *      .scalar_small:
 *          xor  eax, eax
 *      .scalar_body:
 *          mov  r10d, dword ptr [rdx + 4*r8]
 *          add  r10d, dword ptr [rsi + 4*r8]
 *          mov  dword ptr [rdi + 4*r8], r10d
 *          inc  r8 ; dec r9 ; jne .scalar_body
 *      .vec_main:
 *          ; unrolled by 4× INTERLEAVE 2 = 8 elements per iter
 *          vmovdqu ymm0, [rsi + 4*rax]
 *          vmovdqu ymm1, [rdx + 4*rax]
 *          vpaddd  ymm0, ymm0, ymm1
 *          vmovdqu [rdi + 4*rax], ymm0
 *          ; ... 3 more interleaved groups ...
 *          add  rax, 32
 *          ...
 *      .scalar_tail:
 *          ; n % 32 leftovers
 *      .exit:
 *          ret
 *
 *  WHY: This is the canonical recipe. Without `restrict` we need a
 *  runtime alias check, hence the four-way fork. With AVX2 the inner
 *  loop processes 8 ints per iteration; with 4× interleave it does 32.
 */
void simple(int *c, const int *a, const int *b, size_t n) {
    for (size_t i = 0; i < n; i++) c[i] = a[i] + b[i];
}

/*  simple_restrict — restrict eliminates the memcheck and the fallback.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL: identical body to above but the runtime alias check and the
 *  scalar fallback are gone. Smaller code; one fewer compare per call.
 */
void simple_restrict(int * restrict c,
                     const int * restrict a,
                     const int * restrict b,
                     size_t n) {
    for (size_t i = 0; i < n; i++) c[i] = a[i] + b[i];
}

/*  float_scale — float scaling needs care with NaN / signed zero.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3 -mavx2):
 *      float_scale:
 *          ...
 *      .vec:
 *          vbroadcastss ymm0, xmm0            ; broadcast scale into 8 lanes
 *          vmulps  ymm1, ymm0, [rdi]          ; 8-wide multiply
 *          vmovups [rdi], ymm1
 *          ...
 *
 *  WHY THIS WORKS at default flags: `x * scale` is not order-dependent
 *  (each lane is independent). The vector form preserves per-element
 *  IEEE-754 semantics exactly. CONTRAST with reductions, where the
 *  parallel partial-sum tree changes the rounding order — that requires
 *  `-ffast-math`.
 */
void float_scale(float *a, float scale, size_t n) {
    for (size_t i = 0; i < n; i++) a[i] *= scale;
}
