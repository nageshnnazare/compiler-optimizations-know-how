/* 09_load_widening.c — LOAD / STORE WIDENING
 * ============================================================================
 *
 * Idea
 * ----
 *   Adjacent narrow memory operations are fused into one wide one.
 *
 *     4 × byte load with shift-OR pattern → 32-bit load (+ optional bswap)
 *     4 × short store of variables       → 64-bit store of packed values
 *     4 × scalar add of adjacent arrays  → 1 SIMD add (SLP vectorizer)
 *
 * Pass names
 * ----------
 *   LLVM:  LoadCombinePass (in some versions), SLPVectorizerPass,
 *          BitcastVecToVector tricks in InstCombine.
 *   GCC :  store-merging.cc; the vectorizer's "BB SLP" mode.
 *
 * Why this matters
 * ----------------
 *   One 32-bit load is 1 µop; four 8-bit loads are 4 µops with three
 *   shift+OR ops in between (so 8 µops). Even cache-line-aware code can
 *   be a 4× win when this pattern fires.
 * ============================================================================
 */
#include <stdint.h>

/*  four_bytes — 4 narrow loads + shifts collapse to ONE 32-bit load.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3, little-endian x86):
 *      four_bytes:
 *          mov  eax, dword ptr [rdi]      ; ONE 32-bit load
 *          ret
 *
 *  WHY: LoadCombine recognises the byte-load-with-shift-OR pattern and
 *  rewrites it to one wide load. Requires:
 *    – bytes are contiguous & properly ordered for the endianness
 *    – the target supports unaligned wide loads
 *  On BE targets you'd see a wide load + bswap.
 */
uint32_t four_bytes(const uint8_t *p) {
    return  (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/*  four_stores — 4 × 16-bit stores of variables; NOT widened here.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      four_stores:
 *          mov   word ptr [rdi],     si    ; 4 × 16-bit stores
 *          mov   word ptr [rdi + 2], dx
 *          mov   word ptr [rdi + 4], cx
 *          mov   word ptr [rdi + 6], r8w
 *          ret
 *
 *  WHAT WE MIGHT EXPECT (store merging at ITS BEST):
 *      four_stores:
 *          pinsrw  xmm0, esi, 0
 *          pinsrw  xmm0, edx, 1
 *          pinsrw  xmm0, ecx, 2
 *          pinsrw  xmm0, r8d, 3
 *          movq    qword ptr [rdi], xmm0
 *
 *  WHY NOT MERGED: with VARIABLE values, packing them into a 64-bit
 *  register costs 4 shift/or or pinsr ops anyway; the store-merging
 *  cost model decides 4 × `mov word` is cheaper. When the values are
 *  CONSTANTS, the merger does fire — see 01_local_optimizations
 *  /10_load_combine.c `four_stores` for that case.
 */
void four_stores(uint16_t *p, uint16_t a, uint16_t b, uint16_t c, uint16_t d) {
    p[0] = a; p[1] = b; p[2] = c; p[3] = d;
}

/*  slp_friendly — 4 independent scalar adds; SLP should vectorize.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      slp_friendly:
 *          movss   xmm0, dword ptr [rsi]      ; scalar load b[0]
 *          addss   xmm0, dword ptr [rdx]      ; scalar add c[0]
 *          movss   dword ptr [rdi], xmm0      ; scalar store a[0]
 *          movss   xmm0, dword ptr [rsi + 4]
 *          addss   xmm0, dword ptr [rdx + 4]
 *          movss   dword ptr [rdi + 4], xmm0
 *          ; ... same for [+8] and [+12]
 *          ret
 *
 *  WAIT — we expected 4 floats to be loaded with one `movups`, added
 *  with `addps`, stored with one `movups`. Why scalar?
 *
 *  WHY (probably): the compiler can't prove a, b, c don't alias. With
 *  -O3 -fno-strict-aliasing or restrict-qualifying the arguments, the
 *  SLP vectorizer would fire and emit:
 *      slp_friendly:
 *          movups  xmm0, [rsi]                ; load b[0..3]
 *          movups  xmm1, [rdx]                ; load c[0..3]
 *          addps   xmm0, xmm1                 ; SIMD add
 *          movups  [rdi], xmm0                ; store a[0..3]
 *          ret
 *  Try recompiling with the restrict version below to see this.
 */
void slp_friendly(float *a, const float *b, const float *c) {
    a[0] = b[0] + c[0];
    a[1] = b[1] + c[1];
    a[2] = b[2] + c[2];
    a[3] = b[3] + c[3];
}

/*  slp_restrict — restrict makes SLP fire.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3, with restrict):
 *      slp_restrict:
 *          movups  xmm0, [rsi]
 *          movups  xmm1, [rdx]
 *          addps   xmm0, xmm1
 *          movups  [rdi], xmm0
 *          ret
 *
 *  4 SCALAR INSTRUCTIONS → 1 LOAD + 1 ADD + 1 STORE (each 128-bit).
 *  ONE-SHOT 4× speedup on the loads/stores + add.
 */
void slp_restrict(float * restrict a, const float * restrict b, const float * restrict c) {
    a[0] = b[0] + c[0];
    a[1] = b[1] + c[1];
    a[2] = b[2] + c[2];
    a[3] = b[3] + c[3];
}
