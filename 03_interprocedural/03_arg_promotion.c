/* 03_arg_promotion.c — ARGUMENT PROMOTION (by-ref → by-val)
 * ============================================================================
 *
 * Idea
 * ----
 *   A static function that takes a `T *p`, only DEREFERENCES it (no
 *   stores, no further escape), and the address `&x` only flows to it
 *   → the function can be rewritten to take `T` by value.
 *
 *   The caller does a load on its side and passes the value in a register.
 *   The callee never touches memory for this parameter.
 *
 *   Often combined with INLINING — the load disappears too.
 *
 * Pass names
 * ----------
 *   LLVM:  ArgumentPromotionPass.
 *   GCC :  ipa-sra (SRA inter-procedurally).
 *
 * Why bother
 * ----------
 *   A pointer parameter needs a load on every use; a value parameter is
 *   already in a register. For small leaf functions called millions of
 *   times (think hash combiners, comparators), this is a multi-percent
 *   speedup with zero source change.
 * ============================================================================
 */

static int read1(const int *p) {
    return *p + 1;
}

/*  caller — &x flows into read1; x is never stored to memory at all.
 *  ──────────────────────────────────────────────────────────────────────────
 *  WHAT WE'D EXPECT NAIVELY (no inlining, no argprom):
 *      caller:
 *          mov   dword ptr [rsp - 4], edi   ; spill x
 *          lea   rdi, [rsp - 4]              ; pass &x
 *          call  read1
 *          ret
 *      read1:
 *          mov   eax, dword ptr [rdi]        ; *p
 *          inc   eax
 *          ret
 *
 *  ACTUAL (-O3):
 *      caller:
 *          lea   eax, [rdi + 1]              ; x + 1; NO call, NO store
 *          ret
 *
 *  WHY:
 *    1. Argument promotion sees `&x` only flows to read1; read1 only
 *       loads, never stores or escapes the pointer → rewrite read1 to
 *       take `int` by value.
 *    2. Inlining: read1's body is 2 instructions, well under threshold;
 *       inline into caller.
 *    3. Mem2reg + InstCombine collapse to `lea`.
 *
 *  Net: from 1 call + 1 store + 1 load + 1 add → 1 lea.
 */
typedef struct { int a; int b; } Pair;

static int sum_pair(const Pair *p) {
    return p->a + p->b;
}

int caller(int x) {
    return read1(&x);
}

/*  caller_pair — struct address promotion (per-field scalarization).
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      caller_pair:
 *          lea   eax, [rdi + rsi]            ; a + b
 *          ret
 *
 *  IDEAL: NO Pair anywhere in memory.
 *  WHAT WE GOT: exactly that. The temporary struct `p` was scalarized
 *  by SROA on the caller side, the function rewritten to take (a, b),
 *  inlined, and the body `a + b` lowered to a single lea.
 *
 *  This is the IPA equivalent of SROA: the struct is shredded at the
 *  function boundary, allowing both sides to operate on scalars.
 */
int caller_pair(int a, int b) {
    Pair p = { a, b };
    return sum_pair(&p);
}
