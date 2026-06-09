/* 06_fission.c — LOOP FISSION (distribution)
 * ============================================================================
 *
 * Idea
 * ----
 *   The dual of fusion. Split ONE loop with multiple statements into
 *   MULTIPLE loops, each with a single statement. Useful when:
 *     – one statement vectorizes and another doesn't → split out the
 *       vectorizable half;
 *     – the working set busts cache but each half fits;
 *     – you want to parallelize or offload just the heavy half.
 *
 * Pass names
 * ----------
 *   LLVM:  LoopDistributePass (in the default pipeline at -O2/3).
 *   GCC :  tree-loop-distribution.cc.
 * ============================================================================
 */
#include <stddef.h>
#include <stdint.h>

/*  mixed_loop — first statement vectorizable, second serial reduction.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source:
 *      for (size_t i = 0; i < n; i++) {
 *          a[i] = b[i] + 1;                      // vectorizable
 *          hash = (hash * 31u) ^ (uint32_t)a[i]; // serial chain on hash
 *      }
 *
 *  IDEAL TRANSFORM (loop fission):
 *      for (size_t i = 0; i < n; i++) a[i] = b[i] + 1;      // vectorize
 *      for (size_t i = 0; i < n; i++) hash = hash*31 ^ a[i]; // serial
 *
 *  ACTUAL (-O3): Clang does NOT distribute by default here. The asm
 *  shows ONE unrolled-by-2 loop where the multiply chain on `hash` is
 *  the critical path:
 *      mov  r9d, [rsi + 4*rcx]
 *      inc  r9d                          ; b[i]+1
 *      mov  [rdi + 4*rcx], r9d            ; store a[i]
 *      mov  r10d, eax                     ; hash
 *      shl  r10d, 5                       ; hash * 32
 *      sub  r10d, eax                     ; hash * 31
 *      xor  r10d, r9d                     ; XOR with a[i]
 *      ... (next iteration interleaved)
 *
 *  WHY no fission: LLVM's LoopDistribute requires *no* dependence
 *  between the proposed sub-loops; here `hash` is read in iteration i
 *  and written in iteration i (then read again in iteration i+1), which
 *  the analyser conservatively rejects.
 *
 *  How to coerce it: manually split the loops; or hide the reduction
 *  behind `#pragma clang loop distribute(enable)`.
 */
uint32_t mixed_loop(int *a, const int *b, size_t n) {
    uint32_t hash = 0xdeadbeef;
    for (size_t i = 0; i < n; i++) {
        a[i] = b[i] + 1;
        hash = (hash * 31u) ^ (uint32_t)a[i];
    }
    return hash;
}
