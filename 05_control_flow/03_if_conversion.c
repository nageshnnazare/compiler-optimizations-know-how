/* 03_if_conversion.c — IF-CONVERSION (branch → select / cmov)
 * ============================================================================
 *
 * Idea
 * ----
 *   A short if/else with side-effect-free arms is converted to a
 *   `select`-style instruction that executes both arms and picks the
 *   right value. Eliminates the branch (no mispredict penalty) at the
 *   cost of computing both sides.
 *
 *     C                          IR                       x86
 *     ----                       -----                    ----
 *     r = c ? a : b;             %r = select %c, a, b     cmp/cmov
 *     if (c) r = a; else r = b;
 *
 * When the compiler does it
 * -------------------------
 *   – Both arms are speculatable (no traps, no side effects).
 *   – The arms are CHEAP (a few instructions).
 *   – The branch is unpredictable (compiler usually can't tell;
 *     PGO data clarifies).
 *
 * Architecture support
 * --------------------
 *   x86  — CMOVcc family.
 *   ARM  — CSEL, CINC, CSET, CNEG (and predicated 32-bit ARM).
 * ============================================================================
 */
extern int may_trap(int x);

/*  max_branchless — canonical CMOV idiom.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      max_branchless:
 *          mov   eax, esi          ; eax = b
 *          cmp   edi, esi          ; flags from a - b
 *          cmovg eax, edi          ; if a > b → eax = a
 *          ret
 *
 *  WHAT WE'D EXPECT WITHOUT IF-CONVERSION:
 *      max_branchless:
 *          cmp  edi, esi
 *          jle  .else
 *          mov  eax, edi
 *          ret
 *      .else:
 *          mov  eax, esi
 *          ret
 *
 *  WHY: max is the textbook `select`. Three machine instructions, no
 *  branch → no possible misprediction. On modern Intel/AMD, cmov is
 *  one µop, ~1 cycle latency, no port pressure on the branch unit.
 */
int max_branchless(int a, int b) {
    return (a > b) ? a : b;
}

/*  select_constant — both results are constants (3 and 7).
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      select_constant:
 *          xor   eax, eax
 *          test  edi, edi
 *          setne al                 ; al = (x != 0) ? 1 : 0
 *          lea   eax, [4*rax + 3]   ; 4*al + 3 → 3 (no) or 7 (yes)
 *          ret
 *
 *  WHY: When both results are constants in arithmetic progression
 *  (3, 7 = 3 + 4*1), the selector emits a 0/1 indicator (setne) +
 *  affine combination (`lea [4*rax + 3]`) instead of a CMOV. Same
 *  branch-free shape, even fewer dependencies.
 */
int select_constant(int x) {
    return x ? 7 : 3;
}

/*  has_side_effects — `may_trap` cannot be speculated.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      has_side_effects:
 *          test edi, edi
 *          je   .ret_zero
 *          mov  edi, esi
 *          jmp  may_trap            ; TAILCALL (still BRANCHED, not selected)
 *      .ret_zero:
 *          xor  eax, eax
 *          ret
 *
 *  WHAT WE MIGHT NAIVELY WANT: `r = cond ? may_trap(b) : 0;` → a select
 *  on the result of may_trap. But that would require CALLING may_trap
 *  unconditionally — observable side effect in the cond==0 case. If
 *  may_trap divides by zero when b==0, we'd crash where the original
 *  source wouldn't. So the branch must STAY.
 */
int has_side_effects(int cond, int b) {
    if (cond) return may_trap(b);
    return 0;
}

/*  cond_inside_loop — if-conversion inside a loop body.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source:
 *      for (i=0;i<n;i++) a[i] = (a[i] > 0) ? a[i] * 2 : 0;
 *
 *  ACTUAL (-O3): the loop VECTORIZES because the inner `if` was
 *  replaced by a `select`, which SIMD-instruction-selects to a `pcmpgtd`
 *  (compare-greater-than packed) producing a 0/-1 mask, then a `pand`
 *  with `a[i] * 2`. NO branch inside the inner loop.
 *
 *  Without if-conversion the SIMD code would have to do per-lane
 *  predicated execution (mask-and-mask) anyway — they're equivalent.
 *  But without if-conversion the *scalar* fallback would have a branch
 *  per element, slowing the non-vector path dramatically.
 */
void cond_inside_loop(int *a, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] > 0)
            a[i] = a[i] * 2;
        else
            a[i] = 0;
    }
}
