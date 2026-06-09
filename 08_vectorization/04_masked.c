/* 04_masked.c — MASKED VECTORIZATION (conditionals inside loop bodies)
 * ============================================================================
 *
 * Idea
 * ----
 *   `if (cond[i]) ...` inside a vectorized loop becomes a PREDICATED
 *   SIMD operation. A mask register selects which lanes are "alive";
 *   inactive lanes are bypassed.
 *
 *   AVX-512 / SVE / RISC-V V: native masked load/store/op instructions.
 *   AVX2:        vpmaskmovd / vmaskmovps (masked store), or
 *                a "load-blend-store" pair (read old, blend, write).
 *
 *   The mask is built from the per-lane comparison: `vpcmpgtd ymm_mask,
 *   ymm_a, ymm_zero` produces -1 in lanes where a > 0, 0 elsewhere.
 *
 * Why masking matters
 * -------------------
 *   Without it, a loop with `if` couldn't be vectorized (different
 *   lanes would do different things). Masking lets ALL lanes execute,
 *   but only the right ones COMMIT — preserving correctness while
 *   keeping the full SIMD throughput on the common-true case.
 * ============================================================================
 */
#include <stddef.h>

/*  clip_to_zero — set negative elements to 0.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3 -mavx2):
 *      .vec:
 *          vmovdqu  ymm1, [rcx + 4*rdx - 96]    ; load 8 ints
 *          vpmaskmovd [rcx + 4*rdx - 96], ymm1, ymm0  ; mask-store with mask=ymm1
 *          ; ... repeated for 4 unrolled chunks ...
 *
 *  WHY: For "set negative to 0", the result is `max(a[i], 0)`. The
 *  compiler computes both versions (a[i] and 0) and uses a mask
 *  (a[i] < 0) to pick. `vpmaskmovd` writes only the lanes where the
 *  mask is set (here: where the original was negative, the lane gets
 *  zero). Beautiful.
 *
 *  With AVX-512 you'd see `vpmaxsd` directly — even simpler.
 */
void clip_to_zero(int *a, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] < 0) a[i] = 0;
    }
}

/*  scale_positive — multiply only positive elements.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3 -mavx2): the loop:
 *      .vec:
 *          vmovups ymm1, [rdi]                  ; load 8 floats
 *          vbroadcastss ymm2, xmm0               ; scale (broadcast scalar)
 *          vmulps  ymm3, ymm1, ymm2              ; SIMD multiply
 *          vcmpgtps ymm_mask, ymm1, ymm_zero     ; mask = (a[i] > 0)
 *          vblendvps ymm_out, ymm1, ymm3, ymm_mask  ; blend: use scaled if mask
 *          vmovups [rdi], ymm_out                ; store
 *
 *  Both `a[i]` and `a[i]*s` are computed for all 8 lanes; the blend
 *  selects which to write back. No branch in the inner loop.
 */
void scale_positive(float *a, size_t n, float s) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] > 0.0f) a[i] *= s;
    }
}

/*  split — write to one of two arrays based on sign.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3 -mavx2): two masked stores per iteration:
 *      vmovups       ymm0, [rdi + 4*rax]            ; load in[i:i+8]
 *      vcmpgtps      ymm_mask_pos, ymm0, ymm_zero    ; mask = (in > 0)
 *      vmaskmovps    [rsi + 4*rax], ymm_mask_pos, ymm0 ; pos[i] = in[i] if mask
 *      vmaskmovps    [rdx + 4*rax], ymm_neg_mask, ymm0 ; neg[i] = in[i] otherwise
 *
 *  WHY: Two output arrays + a per-lane condition. AVX2's masked store
 *  is the right tool. Each lane's data goes to exactly one of the two
 *  arrays.
 */
void split(const float *in, float *pos, float *neg, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (in[i] > 0.0f) pos[i] = in[i];
        else              neg[i] = in[i];
    }
}
