/* 11_idiom_recognition.c — LOOP IDIOM RECOGNITION
 * ============================================================================
 *
 * Idea
 * ----
 *   The compiler matches a *whole loop* against known patterns and
 *   replaces it with a single library call or hardware instruction.
 *
 *   loop pattern                              replaced by
 *   ────────────────────────────────          ───────────────────────────
 *   for(i=0;i<n;i++) p[i] = 0;                memset(p, 0, n)
 *   for(i=0;i<n;i++) p[i] = c;                memset(p, c, n)        (byte c only)
 *   for(i=0;i<n;i++) d[i] = s[i];             memcpy(d, s, n)
 *   while (*s) s++;                            strlen(s)
 *   for(...) c += (x>>i)&1;                    __builtin_popcount(x)
 *   sum 0..n-1                                 n*(n-1)/2 (closed-form via SCEV)
 *
 * Pass names
 * ----------
 *   LLVM:  LoopIdiomRecognizePass.
 *   GCC :  tree-loop-distribute-patterns.cc + builtins recognition.
 *
 * When it FAILS
 * -------------
 *   – `-ffreestanding` or `-fno-builtin-memset` removes the assumption
 *     that the libc functions exist.
 *   – Loops with non-trivial side effects in the body.
 *   – Loops where the trip-count is not a known SCEV expression.
 * ============================================================================
 */
#include <stddef.h>
#include <stdint.h>

/*  zero_buf — recognised as memset(p, 0, n).
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3, on macOS x86):
 *      zero_buf:
 *          test  rsi, rsi
 *          jne   ___bzero            ; TAIL CALL to libc bzero(p, n)
 *          ret
 *
 *  Linux glibc target instead tail-calls to memset@PLT.
 *
 *  WHAT WE'D EXPECT WITHOUT IDIOM RECOGNITION: a hand-vectorized loop,
 *  fine for small n but never as fast as libc's hand-tuned memset which
 *  uses non-temporal stores past the L3 cache for large n.
 */
void zero_buf(uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) p[i] = 0;
}

/*  fill_buf — recognised as memset(p, c, n).
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      fill_buf:
 *          test  rsi, rsi
 *          je    .ret
 *          mov   rax, rsi             ; argument shuffling for memset signature
 *          movzx esi, dl
 *          mov   rdx, rax
 *          jmp   _memset              ; TAIL CALL
 *      .ret: ret
 *
 *  Note the argument shuffle: source has signature (p, n, c) but memset
 *  wants (p, c, n). The compiler arranges them in the right SysV-ABI
 *  registers and jumps.
 */
void fill_buf(uint8_t *p, size_t n, uint8_t c) {
    for (size_t i = 0; i < n; i++) p[i] = c;
}

/*  copy_buf — recognised as memcpy(d, s, n).
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): NOT lowered to a memcpy call here. The reason is that
 *  the compiler chose to vectorize inline (large enough body, alignment
 *  guards inserted) because the cost model thought inline was cheaper
 *  than the function-call overhead.
 *
 *  If you call this with large n, calling memcpy directly is usually
 *  faster than letting the compiler inline-vectorize, because libc's
 *  memcpy uses rep movsq + non-temporal stores tuned to the L3 size.
 */
void copy_buf(uint8_t *d, const uint8_t *s, size_t n) {
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

/*  mystrlen — recognised as strlen.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): tail-calls libc strlen on most platforms. On baseline
 *  x86-64 you may instead see an inline loop using `pcmpeqb` to scan 16
 *  bytes at a time for the null terminator.
 *
 *  WHY: glibc's strlen uses SSE4.2's `pcmpistri` and is faster than any
 *  byte-at-a-time loop the compiler would inline.
 */
size_t mystrlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

/*  sum_arith — sum 0..n-1 → closed form via SCEV.
 *  ──────────────────────────────────────────────────────────────────────────
 *  EXPECTED (without SCEV): a vectorized loop of `add ecx, ebx; inc ebx`.
 *
 *  ACTUAL (-O3): a few instructions implementing `n*(n-1)/2`:
 *      sum_arith:
 *          test  edi, edi
 *          je    .ret_zero
 *          lea   eax, [rdi - 1]       ; n - 1
 *          imul  rax, rdi              ; n * (n-1)
 *          shr   rax                   ; / 2
 *          ; ... (mov to return reg)
 *
 *  WHY: SCEV gives a closed-form description `value(i) = i` for the
 *  loop counter; the sum becomes `Σi=0..n-1 i = n(n-1)/2`. The compiler
 *  drops the loop entirely. Constant-time vs O(n).
 */
unsigned sum_arith(unsigned n) {
    unsigned s = 0;
    for (unsigned i = 0; i < n; i++) s += i;
    return s;
}
