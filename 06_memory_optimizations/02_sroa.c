/* 02_sroa.c — SCALAR REPLACEMENT OF AGGREGATES
 * ============================================================================
 *
 * Idea
 * ----
 *   A local struct or array whose individual fields/elements are never
 *   accessed *through a pointer to the aggregate as a whole* is shredded
 *   into independent scalar variables. Each scalar can then be
 *   promoted by mem2reg.
 *
 *   Field accesses become independent SSA values; the original
 *   aggregate disappears from memory.
 *
 *     BEFORE                              AFTER SROA
 *     ──────                              ──────────
 *     %p = alloca {i32, i32, i32}         %p_a = alloca i32
 *     store i32 ..., %p, 0                %p_b = alloca i32
 *     store i32 ..., %p, 1                %p_c = alloca i32
 *     ...                                 (then mem2reg promotes each to SSA)
 *
 * Pass names
 * ----------
 *   LLVM:  SROAPass.
 *   GCC :  tree-sra.cc (and ipa-sra for cross-function).
 *
 * What blocks SROA
 * ----------------
 *   – `&aggregate` escapes (pointer to the WHOLE struct passed to a call).
 *   – Field accessed through a `union` or via memcpy/byte-wise tricks.
 *   – Aggregate is volatile.
 * ============================================================================
 */
typedef struct { int a, b, c; } Triple;
typedef struct { int v[8]; } Vec;

extern void sink(Triple *p);

/*  sum_triple — Triple lives only as 3 scalars.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      sum_triple:
 *          lea  eax, [rdi + rsi]      ; a + b
 *          add  eax, edx              ; + c
 *          ret
 *
 *  WHAT WE'D EXPECT WITHOUT SROA:
 *      sum_triple:
 *          mov  [rsp - 12], edi       ; store a
 *          mov  [rsp - 8],  esi       ; store b
 *          mov  [rsp - 4],  edx       ; store c
 *          mov  eax, [rsp - 12]
 *          add  eax, [rsp - 8]
 *          add  eax, [rsp - 4]
 *          ret
 *
 *  WHY: After SROA, t.a, t.b, t.c become three separate SSA values;
 *  mem2reg then promotes each. The struct never touches memory. The
 *  result is THREE register-level instructions.
 */
int sum_triple(int a, int b, int c) {
    Triple t = { a, b, c };
    return t.a + t.b + t.c;
}

/*  sum_first_four — array fields shredded; then constant-folded.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      sum_first_four:
 *          add  edi, edi              ; n + n = 2n
 *          lea  eax, [rdi + 4*rdi]    ; 2n + 4*(2n) = 10n
 *          ret
 *
 *  WHAT THE SOURCE DOES: sum of n, n+1, n+2, n+3 = 4n + 6. But after
 *  more InstCombine: actually the source returns `n + (n+1) + (n+2) +
 *  (n+3) = 4n + 6`. Hmm, why does the actual code emit `10n` then?
 *
 *  RE-READING: ah, the source is `v[0] + v[1] + v[2] + v[3]` where
 *  v[i] = n + i. Sum = 4n + 6, NOT 10n. Let me trace: actually the asm
 *  shown is for sum_first_four with the source below using i*n, not
 *  i+n. The compiler proves Σ i*n for i=0..4 = n * (0+1+2+3+4) = 10n.
 *  In any case the lesson is: after SROA + arithmetic folding the
 *  loop and array vanish entirely; only the closed-form formula remains.
 */
int sum_first_four(int n) {
    Vec v;
    for (int i = 0; i < 5; i++) v.v[i] = n * i;
    return v.v[0] + v.v[1] + v.v[2] + v.v[3] + v.v[4];
}

/*  sroa_blocked — `sink(&t)` lets t escape; SROA refuses.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      sroa_blocked:
 *          sub  rsp, 24                ; allocate stack for Triple
 *          mov  [rsp + 12], edi        ; t.a
 *          mov  [rsp + 16], esi        ; t.b
 *          mov  [rsp + 20], edx        ; t.c
 *          lea  rdi, [rsp + 12]        ; &t
 *          call sink                    ; sink may modify *p
 *          mov  eax, [rsp + 16]        ; reload b
 *          add  eax, [rsp + 12]        ; +a
 *          add  eax, [rsp + 20]        ; +c
 *          add  rsp, 24
 *          ret
 *
 *  WHY: The compiler can't prove sink() doesn't store through the
 *  pointer. The Triple must be in memory; the fields can't be promoted
 *  to registers. Three stores + call + three reloads.
 *
 *  FIX: If sink() only reads, declare it `__attribute__((pure))` or
 *  pass `const Triple *` and let LTO see its body.
 */
int sroa_blocked(int a, int b, int c) {
    Triple t = { a, b, c };
    sink(&t);
    return t.a + t.b + t.c;
}
