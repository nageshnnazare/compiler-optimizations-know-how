/* 07_memcpy_opt.c — MEMCPY OPTIMIZATION
 * ============================================================================
 *
 * What MemCpyOpt does
 * -------------------
 *   1. Manual byte-copy loops → memcpy(dst, src, n).
 *   2. memcpy(dst, tmp, n) following memcpy(tmp, src, n) → memcpy(dst, src, n)
 *      (the tmp eliminated if its only uses are the two memcpy ops).
 *   3. `*p = X; *p = Y` chains of structure copies → single memcpy.
 *   4. memset(buf, 0, N) immediately followed by selective stores → keep
 *      just the stores if the zeros are overwritten.
 *
 * Pass names
 * ----------
 *   LLVM:  MemCpyOptimizerPass.
 *   GCC :  forwprop + tree-loop-distribute-patterns.
 * ============================================================================
 */
#include <string.h>

/*  manual_copy — for-loop char copy → memcpy / inline-vectorized.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): the compiler does NOT call memcpy here; instead it
 *  inlines a vectorized copy (with runtime alias check), because the
 *  cost model decides inline beats the libc-call overhead at this size.
 *
 *      manual_copy:
 *          test  edx, edx
 *          jle   .exit
 *          ...                       ; runtime alias check
 *          ; vectorized 32-byte loop:
 *      .vec_body:
 *          movups xmm0, [rsi + rdx]
 *          movups xmm1, [rsi + rdx + 16]
 *          movups [rdi + rdx],      xmm0
 *          movups [rdi + rdx + 16], xmm1
 *          add   rdx, 32
 *          ...
 *      .scalar_tail:
 *          movzx eax, byte ptr [rsi + r8]
 *          mov   [rdi + r8], al
 *          ...
 *
 *  WHY: the source's `for-byte` copy is recognised as a memcpy IDIOM
 *  (LoopIdiomRecognize). Then the inliner-of-builtins chooses inline-
 *  expansion vs libc call based on the trip count, alignment, and
 *  target CPU. For unknown but possibly-large n, you'd see a call to
 *  memcpy; here the compiler bet on a small-to-medium n with the
 *  vectorized inline.
 */
void manual_copy(char *dst, const char *src, int n) {
    for (int i = 0; i < n; i++) dst[i] = src[i];
}

/*  temp_buffer — memcpy through a 64-byte stack temporary.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source:
 *      char tmp[64];
 *      memcpy(tmp, src, 64);
 *      memcpy(dst, tmp, 64);
 *
 *  IDEAL: memcpy(dst, src, 64); — the temporary vanishes.
 *  ACTUAL (-O3): Clang does the right thing: emits a single 64-byte
 *  copy directly from src to dst (4 × 16-byte vector loads + stores,
 *  no use of the stack tmp).
 *
 *  WHY: MemCpyOpt sees the temp is written ONCE by memcpy and READ ONCE
 *  by memcpy. It rewrites the chain to memcpy(dst, src, 64) directly,
 *  then deletes the now-unused alloca. Saves 64 bytes of stack and one
 *  copy.
 */
void temp_buffer(char *dst, const char *src) {
    char tmp[64];
    memcpy(tmp, src, 64);
    memcpy(dst, tmp, 64);
}

/*  struct_copy — `*dst = *src` for a large struct.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): inlined 64-byte copy (4 × 16-byte vector mov), or
 *  call to memcpy depending on size and target. For sizeof(Big) = 64
 *  the inline path wins.
 *
 *  WHY: C's structure assignment is defined to copy all bytes. The
 *  frontend lowers it to a memcpy with the exact size; MemCpyOpt then
 *  decides between calling libc and inlining a vectorized copy.
 */
typedef struct { int v[16]; } Big;

void struct_copy(Big *dst, const Big *src) {
    *dst = *src;
}

/*  chain — memset followed by memcpy that overwrites the start.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source: memset(p, 0, 100); memcpy(p, "hello", 5);
 *
 *  ACTUAL (-O3): two distinct operations remain — the leading 5 zero
 *  bytes of the memset are technically dead (overwritten), but the
 *  cost of *partially* removing them (changing the memset's size and
 *  offset) is higher than the benefit. The optimizer leaves the
 *  memset as-is and emits the 5-byte string copy after.
 *
 *  If the source were `memset(p, 0, 5); memcpy(p, "hello", 5);` you'd
 *  see the memset disappear entirely.
 */
void chain(char *p) {
    memset(p, 0, 100);
    memcpy(p, "hello", 5);
}
