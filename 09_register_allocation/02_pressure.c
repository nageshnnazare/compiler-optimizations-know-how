/* 02_pressure.c — pushing the register allocator past its limit
 *
 * Compile with -m32 (if available) to make spills more likely.
 *
 *   clang -O3 -m32 -S -fverbose-asm 02_pressure.c | grep -E '\b(spill|reload|stack)'
 *
 * Or count spills in the asm (look for `spill_size:` annotations).
 */
#include <stdint.h>

/*  many_live — 16 ints "live at once" in source, all in XMM regs in asm.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3, x86-64 SSE2):
 *      many_live:
 *          movd     xmm0, edi               ; broadcast x
 *          pmulld   xmm0, xmm0
 *          pshufd   xmm0, xmm0, 0
 *          movdqa   xmm1, [rip + LCPI0_0]   ; constants for v[i] mults
 *          pmulld   xmm1, xmm0
 *          movdqa   xmm2, [rip + LCPI0_1]
 *          pmulld   xmm2, xmm0
 *          ...                              ; closed-form result via SLP
 *          ; (no `mov ... [rsp - ...]` instructions: no spills)
 *          ret
 *
 *  WHY NO SPILLS: the array v[] is fully constant-folded once the
 *  trip count is small and known (16 iterations, x is the only
 *  variable). SLP packs every 4 elements into an XMM. The 16-vreg
 *  pressure becomes 4 XMM regs (16 lanes total). x86-64 has 16 XMMs;
 *  fits with room to spare.
 *
 *  ON x86-32 with only 8 XMMs and 7 GPRs: would spill. Compile with
 *  `clang -O3 -m32 -S` to see the `mov [esp+N]` spill instructions.
 */
int many_live(int x) {
    int v[16];
    for (int i = 0; i < 16; i++) v[i] = x * (i + 1);
    int s = 0;
    for (int i = 0; i < 16; i++) s += v[i] * v[(i+1)%16];
    return s;
}

/*  interleave — two-array loop; inner body uses several scratch regs.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): inner loop body:
 *      .body:
 *          mov   r8d,  [rdi + 4*rcx]      ; a[i]
 *          mov   r9d,  [rsi + 4*rcx]      ; b[i]
 *          mov   r10d, r9d
 *          xor   r10d, r8d                ; a^b
 *          imul  r9d,  r8d                ; a*b
 *          add   r9d,  r10d               ; +
 *          add   eax,  r9d                ; s += ...
 *          inc   rcx
 *          ...
 *
 *  Five vregs live simultaneously (i, sum, a[i], b[i], scratch). All
 *  in physical registers; no spills. On x86-32 this exact loop would
 *  also fit (only 5 GPRs needed beyond i and sum).
 *
 *  Note the allocator picked CALLER-saved scratch (r8, r9, r10, eax)
 *  because the function has no calls in the loop — those scratches
 *  are free to use without saving.
 */
int interleave(const int *a, const int *b, int n) {
    int s = 0;
    for (int i = 0; i < n; i++) s += a[i] * b[i] + (a[i] ^ b[i]);
    return s;
}
