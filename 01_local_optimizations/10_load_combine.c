/* 10_load_combine.c — LOAD/STORE COMBINING & MEMSET/MEMCPY RECOGNITION
 * ============================================================================
 *
 * Several passes participate
 * --------------------------
 *   – LLVM Load/Store Vectorizer: fuse adjacent narrow loads/stores into
 *     one wide load/store.
 *   – LoopIdiomRecognize: turn zero-fill / copy loops into memset / memcpy.
 *   – MemCpyOpt: collapse chains of memcpy/memset; eliminate redundant
 *     copies through stack temporaries.
 *   – Backend store-merging (GCC: `store-merging` pass).
 *
 * Why it matters
 * --------------
 *   Loops of small stores have ENORMOUS overhead from the loop control
 *   and per-element work. Replacing them with vectorized stores or a
 *   tuned `memset`/`memcpy` is a 4-16× speedup on real workloads.
 * ============================================================================
 */
#include <stdint.h>
#include <string.h>

/*  word_from_bytes — four bytes shifted/OR'd → one 32-bit load.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3, x86-64 little-endian):
 *      word_from_bytes:
 *          mov  eax, dword ptr [rdi]      ; ONE 32-bit load
 *          ret
 *
 *  WHAT THE NAÏVE COMPILER WOULD EMIT:
 *      movzx eax, byte ptr [rdi]
 *      movzx ecx, byte ptr [rdi + 1]
 *      shl   ecx, 8
 *      or    eax, ecx
 *      movzx ecx, byte ptr [rdi + 2]
 *      shl   ecx, 16
 *      or    eax, ecx
 *      movzx ecx, byte ptr [rdi + 3]
 *      shl   ecx, 24
 *      or    eax, ecx
 *      ret
 *
 *  WHY: LLVM's load-combine pass spots the disjoint-byte assembly pattern
 *  and replaces it with one wide load. The legality conditions are:
 *    (a) bytes are contiguous in memory
 *    (b) target supports unaligned 32-bit loads (true on x86)
 *    (c) endianness is known (LE → no bswap, BE → wide load + bswap)
 */
uint32_t word_from_bytes(const uint8_t *p) {
    return  (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/*  qword_from_bytes — same idea with a loop, eight bytes → 64-bit load.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      qword_from_bytes:
 *          mov  rax, qword ptr [rdi]      ; ONE 64-bit load
 *          ret
 *
 *  WHY: Loop is fully unrolled (8 iterations, constant trip count), the
 *  same load-combine pass then collapses the 8 byte loads into one
 *  64-bit load.
 */
uint64_t qword_from_bytes(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (i * 8);
    return v;
}

/*  zero_fill — for-loop of 64 zero stores → 4×16-byte vector stores.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3, x86-64 with SSE2):
 *      zero_fill:
 *          xorps   xmm0, xmm0                       ; xmm0 = 0
 *          movups  xmmword ptr [rdi + 48], xmm0
 *          movups  xmmword ptr [rdi + 32], xmm0
 *          movups  xmmword ptr [rdi + 16], xmm0
 *          movups  xmmword ptr [rdi      ], xmm0
 *          ret
 *
 *  WHAT YOU MIGHT EXPECT: a `call memset` instead. With AVX-512 we'd
 *  see one ZMM store instead.
 *
 *  WHY: The body fits in a tiny constant number of vector ops, so the
 *  compiler unrolls it inline rather than calling memset. The choice
 *  depends on `--param max-store-merging-store-size` and the equivalent
 *  cost model in LLVM. For larger n the compiler defers to memset.
 */
void zero_fill(uint8_t *buf) {
    for (int i = 0; i < 64; i++) buf[i] = 0;
}

/*  fill_const — constant non-zero fill.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      .LCPI3_0:
 *          .byte 0xAB, 0xAB, ..., 0xAB        ; 16-byte rodata constant
 *      fill_const:
 *          movaps  xmm0, xmmword ptr [rip + LCPI3_0]
 *          movups  xmmword ptr [rdi + 16], xmm0
 *          movups  xmmword ptr [rdi      ], xmm0
 *          ret
 *
 *  WHY: 0xAB is splatted across 16 bytes ONCE in read-only data, then
 *  used as the source for two 16-byte unaligned stores. Compared to a
 *  loop of 32 `mov byte`, this saves ~30 µops.
 */
void fill_const(uint8_t *buf) {
    for (int i = 0; i < 32; i++) buf[i] = 0xAB;
}

/*  four_stores — four 16-bit constant stores fused into one 64-bit store.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      four_stores:
 *          movabs  rax, 0xDEF09ABC56781234   ; 64-bit immediate
 *          mov     qword ptr [rdi], rax       ; ONE 64-bit store
 *          ret
 *
 *  WHY: Adjacent stores of compile-time constants get *merged* by the
 *  store-merging pass into a single wider store of the combined
 *  constant. The 4 × 16-bit halves become one 64-bit literal.
 */
void four_stores(uint16_t *p) {
    p[0] = 0x1234;
    p[1] = 0x5678;
    p[2] = 0x9ABC;
    p[3] = 0xDEF0;
}

/*  copy_loop — small fixed-count copy → fully unrolled OR replaced with
 *              memcpy depending on size and target.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3) for the 64-byte version: a chain of 8-byte loads/stores
 *  (or vector ops with AVX). For larger n you typically see `call memcpy`.
 *
 *  WHY: The cost model considers (n, alignment, target). At n=64 the
 *  compiler decides inline lowering is faster than the memcpy call
 *  overhead. At n=1024 it calls memcpy. The exact threshold is
 *  --param memcpy-loop-unroll-iterations in GCC.
 */
void copy_loop(uint8_t *dst, const uint8_t *src) {
    for (int i = 0; i < 64; i++) dst[i] = src[i];
}
