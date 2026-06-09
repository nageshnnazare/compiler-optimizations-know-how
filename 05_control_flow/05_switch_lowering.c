/* 05_switch_lowering.c — SWITCH LOWERING STRATEGIES
 * ============================================================================
 *
 *   case-set shape                       lowered to
 *   ────────────────────────             ─────────────────────────
 *   dense contiguous (0..N)              jump table (one indirect jmp)
 *   sparse, few cases                    chain of cmp/je
 *   sparse, many cases                   binary search of compares
 *   constants → constants (1-1)          rodata lookup table
 *   bit-set membership (e.g. ASCII)      a 64-bit constant + `bt` test
 *
 * Pass names
 * ----------
 *   LLVM:  SwitchLowering in SelectionDAG; LowerSwitchPass for the LCG
 *          (older backends).
 *   GCC :  expand_switch.cc selects between jump table, decision tree,
 *          bit-test, and lookup.
 *
 * Knobs (LLVM)
 * ------------
 *   -mllvm -min-jump-table-entries=N      default 4 — below this, use compares
 *   -mllvm -switch-peel-threshold=P       PGO-guided likely-case extraction
 * ============================================================================
 */
extern int op0(int);
extern int op1(int);
extern int op2(int);
extern int op3(int);

/*  dense — 4 contiguous cases → JUMP TABLE.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      dense:
 *          cmp   edi, 3                  ; bounds check: op > 3?
 *          ja    .default                ; yes → return -1
 *          mov   eax, edi
 *          lea   rcx, [rip + LJTI0_0]    ; address of jump table
 *          movsxd rax, dword ptr [rcx + 4*rax]   ; signed 32-bit offset
 *          add   rax, rcx                ; absolute target
 *          jmp   rax                     ; INDIRECT jump
 *      .case0:  mov edi, esi ; jmp op0  ; TAILCALL
 *      .case1:  mov edi, esi ; jmp op1
 *      .case2:  mov edi, esi ; jmp op2
 *      .case3:  mov edi, esi ; jmp op3
 *      .default: mov eax, -1 ; ret
 *
 *  LJTI0_0:
 *      .long .case0 - LJTI0_0
 *      .long .case1 - LJTI0_0
 *      .long .case2 - LJTI0_0
 *      .long .case3 - LJTI0_0
 *
 *  WHY: 4 contiguous cases → table is small (16 bytes), one indirect
 *  jump, branch-target-predictor handles it. Each case tail-calls its
 *  handler. Total per-case path: 4 cheap insns + 1 indirect jmp.
 *
 *  COMPARED TO IF/ELSE CHAIN: would be up to 4 sequential cmp/jcc, with
 *  3 mispredicts on cold paths.
 */
int dense(int op, int x) {
    switch (op) {
    case 0: return op0(x);
    case 1: return op1(x);
    case 2: return op2(x);
    case 3: return op3(x);
    default: return -1;
    }
}

/*  sparse — 5 cases spread over 1..999 → BINARY SEARCH of compares.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3, simplified):
 *      sparse:
 *          cmp  edi, 41                  ; midpoint compare
 *          jle  .lo_half                 ;   1 or 17 ?
 *          cmp  edi, 42                  ;
 *          je   .ret_33                  ;   == 42 → 33
 *          cmp  edi, 100                 ;
 *          je   .ret_44                  ;   == 100 → 44
 *          cmp  edi, 999
 *          je   .ret_55                  ;   == 999 → 55
 *          jmp  .default
 *      .lo_half:
 *          cmp  edi, 1   ; je .ret_11
 *          cmp  edi, 17  ; je .ret_22
 *          jmp  .default
 *
 *  WHY: A jump table would be 999 × 4 bytes = ~4 KB of mostly-default
 *  entries. The compiler prefers a binary decision tree: O(log K)
 *  compares, no memory load, predictable on hot paths.
 */
int sparse(int code) {
    switch (code) {
    case   1: return 11;
    case  17: return 22;
    case  42: return 33;
    case 100: return 44;
    case 999: return 55;
    default:  return  0;
    }
}

/*  constant_switch — every case returns a constant.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): often a `lea` based on (x * 10 + 10) for x in 0..3 with
 *  bounds checking. Or a 4-entry rodata lookup table. The exact choice
 *  depends on whether the constants form an arithmetic progression
 *  (here yes: 10, 20, 30, 40 = 10*(x+1)).
 *
 *  Lookup-table form:
 *      table:  .long 10, 20, 30, 40
 *      constant_switch:
 *          cmp   edi, 3
 *          ja    .default
 *          mov   eax, [table + 4*edi]
 *          ret
 *      .default:
 *          mov   eax, -1
 *          ret
 *
 *  Arithmetic form (LLVM often picks this):
 *      constant_switch:
 *          cmp   edi, 3
 *          ja    .default
 *          lea   eax, [10*(rdi + 1)]     ; pseudo; really 10 + edi * 10
 *          ret
 */
int constant_switch(int x) {
    switch (x) {
    case 0: return 10;
    case 1: return 20;
    case 2: return 30;
    case 3: return 40;
    default: return -1;
    }
}

/*  is_punctuation — bit-set membership of ASCII characters.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3): the compiler often emits a 64-bit bitmask with one bit
 *  per relevant character, then a `bt`-style test:
 *      is_punctuation:
 *          ; bitmap = bits set at positions for {.,;:!?}
 *          mov  rax, 0x0080000A04000000   ; example mask
 *          cmp  edi, 63                   ; if c > 63 → ret 0
 *          ja   .ret_0
 *          bt   rax, edi                  ; test bit c in rax
 *          setb al                        ; al = (bit set)
 *          movzx eax, al
 *          ret
 *
 *  Six compares replaced by one 64-bit constant + a single bit test.
 */
int is_punctuation(char c) {
    switch (c) {
    case '.': case ',': case ';':
    case ':': case '!': case '?':
        return 1;
    default:
        return 0;
    }
}
