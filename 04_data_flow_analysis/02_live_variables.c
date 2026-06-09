/* 02_live_variables.c — LIVENESS ANALYSIS
 * ============================================================================
 *
 *   BB1: a = 5; b = 7;          ;; OUT(BB1) = {a, b, cond}
 *   BB2: if (cond)              ;; IN(BB2)  ⊇ {cond, a, b}
 *      /         \
 *   BB3:         BB4:
 *   c = a + 1    d = b * 2      ;; IN(BB3) = {a}, IN(BB4) = {b}
 *   ret c        ret d
 *
 *   Backward, MAY analysis:
 *      IN [B] = USE[B] ∪ (OUT[B] \ DEF[B])
 *      OUT[B] = ⋃ IN[S]  over successors S
 *
 * Uses of liveness
 * ----------------
 *   – Register allocation: live ranges drive interference & spilling.
 *   – Dead code elimination: a def whose name is never live is dead.
 *   – Spill placement: spill the variable with the longest live range.
 * ============================================================================
 */

/*  liveness_demo — choose between c=a+1 and d=b*2.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      liveness_demo:
 *          xor   eax, eax
 *          test  edi, edi
 *          sete  al                 ; al = (cond == 0)
 *          lea   eax, [8*rax + 6]   ; cond==0 → 14 (=b*2);  cond!=0 → 6 (=a+1)
 *          ret
 *
 *  WHAT THE NAÏVE CFG WOULD GIVE:
 *      liveness_demo:
 *          test edi, edi
 *          je   .else
 *          mov  eax, 6              ; a + 1 = 5 + 1 = 6
 *          ret
 *      .else:
 *          mov  eax, 14             ; b * 2 = 7 * 2 = 14
 *          ret
 *
 *  WHY THE ACTUAL IS BETTER: SimplifyCFG converts the if/else to a
 *  `select 6, 14`; both constants are encoded in the linear function
 *  `8*x + 6` so that x=0 → 6, x=1 → 14. ONE branchless arithmetic
 *  instruction does the job. The liveness analysis is what allowed
 *  the optimizer to *see* that `a` and `b` weren't live past their
 *  respective branches, so they could be folded to constants and the
 *  branches eliminated.
 */
int liveness_demo(int cond) {
    int a = 5, b = 7, c, d;
    if (cond) {
        c = a + 1;
        return c;       /* on this path, b was DEAD already in BB1 */
    } else {
        d = b * 2;
        return d;       /* on this path, a was DEAD already in BB1 */
    }
}

/*  many_temps — five temporaries; max simultaneously live = 3.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      many_temps:
 *          lea   eax, [rsi + 2*rsi]   ; eax = 3y       (t2)
 *          lea   eax, [rax + 2*rdi]   ; eax = 3y + 2x  (t4 collapsed)
 *          lea   eax, [rax + 4*rdx]   ; eax = + 4z     (t5)
 *          ret
 *
 *  WHAT WE'D EXPECT WITHOUT LIVENESS-AWARE ALLOCATION: five distinct
 *  registers t1..t5 reserved at once → six live values including
 *  args x,y,z. Easily fits in 16 regs but represents wasted state.
 *
 *  WHY: liveness shows that t1 dies after t4 is formed, t2 also dies
 *  there, t3 dies after t5. The allocator coalesces all five SSA names
 *  into ONE physical register (RAX) by reusing it. Three lea
 *  instructions, zero spills, all in the return register.
 */
int many_temps(int x, int y, int z) {
    int t1 = x * 2;
    int t2 = y * 3;
    int t3 = z * 4;
    int t4 = t1 + t2;
    int t5 = t4 + t3;
    return t5;
}
