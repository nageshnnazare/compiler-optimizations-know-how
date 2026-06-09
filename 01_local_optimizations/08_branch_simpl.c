/* 08_branch_simpl.c — BRANCH SIMPLIFICATION & SIMPLIFY-CFG
 * ============================================================================
 *
 * What it is
 * ----------
 *   A grab-bag of CFG-shaping passes that collapse trivial branches,
 *   merge identical successors, and convert short if/else chains to
 *   branchless `select` / `cmov` sequences.
 *
 *   Trivial branches it eliminates:
 *
 *     if (1)  X; else Y;            ───►    X;
 *     if (0)  X; else Y;            ───►    Y;
 *     if (x) return 1; return 0;    ───►    return !!x;
 *
 *   Branch-to-select conversion:
 *
 *     if (x > 0) r = a; else r = b; ───►    r = (x > 0) ? a : b;
 *
 *   Tail merging:
 *
 *     two BBs ending in the same code ───► single shared BB
 *
 * Pass names
 * ----------
 *   LLVM: SimplifyCFGPass, JumpThreadingPass.
 *   GCC : cleanup_cfg (called repeatedly), tree-ssa-threadedge.cc.
 * ============================================================================
 */

/*  cond_constant — `if (1) X else Y` collapses to just X.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      cond_constant:
 *          lea   eax, [rdi + 1]     ; eax = x + 1; the else-branch is GONE.
 *          ret
 *
 *  WHY: SimplifyCFG sees an `if` whose condition is the constant `1`.
 *  The false-edge is unreachable; the false-block is unreferenced and
 *  deleted; the condition itself is removed; the then-block is spliced
 *  into the parent. Three passes; net effect: straight-line code.
 */
int cond_constant(int x) {
    if (1) return x + 1;
    return x - 1;
}

/*  bool_to_int — `if (x) return 1; return 0` becomes `return x != 0`.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      bool_to_int:
 *          xor   eax, eax
 *          test  edi, edi
 *          setne al
 *          ret
 *
 *  WHY: The two-block diamond is recognized as the canonical "convert
 *  arbitrary nonzero to 1" pattern. The CFG flattens to straight-line
 *  code: test, then SETcc into the low byte of EAX. (The xor earlier
 *  cleans the upper bytes so the result is exactly 0 or 1.)
 */
int bool_to_int(int x) {
    if (x) return 1;
    return 0;
}

/*  select_constant — branch turns into a cmov.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      select_constant:
 *          xor   eax, eax
 *          test  edi, edi
 *          setg  al                 ; al = (x > 0) ? 1 : 0
 *          lea   eax, [4*rax + 3]   ; if al=1 → 7; if al=0 → 3
 *          ret
 *
 *  WHAT THE NAÏVE COMPILER WOULD EMIT (jcc / mov pair):
 *      cmp  edi, 0
 *      jle  .else
 *      mov  eax, 7
 *      jmp  .end
 *      .else:
 *      mov  eax, 3
 *      .end:
 *      ret
 *
 *  WHY: SimplifyCFG converts the `if/else` into LLVM `select`. Codegen
 *  then notices both selected values are linear in a 0/1 indicator
 *  (7 = 4*1+3, 3 = 4*0+3) and emits one `lea` instead of a `cmov`.
 *  The classic branchless-from-condition trick.
 */
int select_constant(int x) {
    int r;
    if (x > 0) r = 7;
    else       r = 3;
    return r;
}

/*  sink_call — both arms call g(); compiler emits just one call.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      sink_call:
 *          jmp  g@PLT               ; TAIL CALL — no `call` or `ret` at all.
 *
 *  WHY: SimplifyCFG sinks the identical `return g()` from both arms into
 *  one BB. Now the only thing the function does is `return g()`, which
 *  is a perfect tail call → TCE turns `call g; ret` into `jmp g`.
 *  Whole-function code shrinks to ONE instruction.
 */
extern int g(void);
int sink_call(int p) {
    if (p) return g();
    else   return g();
}

/*  chained — two-step boolean threaded into one compare.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source equivalent:    return (x > 10) ? 100 : -100;
 *
 *  ACTUAL (-O3):
 *      chained:
 *          cmp  edi, 11             ; x >= 11 ⇔ x > 10
 *          mov  ecx, -100
 *          mov  eax, 100
 *          cmovl eax, ecx           ; if x < 11 → eax = -100; else stay 100
 *          ret
 *
 *  WHY: Jump-threading observes that the path
 *    x>10 → t=1 → if(t)→ ret 100
 *  always uses t=1 and the path
 *    x≤10 → t=0 → if(t)→ ret -100
 *  always uses t=0. Each redundant compare is removed and the two
 *  branches collapse into one CMOV.
 */
int chained(int x) {
    int t;
    if (x > 10) t = 1; else t = 0;
    if (t)       return  100;
    else         return -100;
}
