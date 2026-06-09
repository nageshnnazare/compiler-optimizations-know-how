/* 01_jump_threading.c — JUMP THREADING
 * ============================================================================
 *
 * Idea
 * ----
 *   A path of the form  cond_X → block_Y → cond_X  where the second
 *   evaluation of cond_X is *fully determined* by the first can be
 *   "threaded": rewrite Y's exit edge to point directly to the known-
 *   correct successor, bypassing the redundant compare.
 *
 *       Before:                              After threading:
 *
 *       if (cond) {                          if (cond) {
 *         x = 1;                                x = 1;
 *       } else {                                // go straight to       
 *         x = 0;                                // goto_label below     
 *       }                                       goto goto_label;
 *       if (cond)         <-- redundant       } else {
 *         ...                                   x = 0;
 *       else                                    //same 
 *         ...                                   goto goto_label_else;
 *                                            }
 *
 * Pass names
 * ----------
 *   LLVM:  JumpThreadingPass; CorrelatedValuePropagationPass.
 *   GCC :  tree-ssa-threadedge.cc.
 * ============================================================================
 */

extern int A_path(int);
extern int B_path(int);

/*  threadable — chained compares with the SAME operand.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source:    if (x > 10) {... } if (x > 10) {... }    (the second is implied)
 *  Effectively, if (x > 10) → A_path else → B_path, with an intermediate
 *  redundant compare threaded away.
 *
 *  ACTUAL (-O3):
 *      threadable:
 *          cmp  edi, 11
 *          jl   B_path             ; TAILCALL on x < 11
 *          jmp  A_path             ; TAILCALL on x >= 11 (i.e. x > 10)
 *
 *  WHAT WE'D EXPECT WITHOUT THREADING:
 *      threadable:
 *          cmp  edi, 10
 *          jle  .else
 *          mov  ebx, 1
 *          jmp  .check
 *      .else:
 *          xor  ebx, ebx
 *      .check:
 *          test ebx, ebx
 *          je   .else_call
 *          jmp  A_path
 *      .else_call:
 *          jmp  B_path
 *
 *  WHY: After SSA, both `if (cond)` blocks share the same φ value at
 *  the second test (it's exactly `cond` itself, unchanged). Jump
 *  Threading uses Lazy Value Information (LVI) to prove the second
 *  test's outcome from the first, then redirects edges to skip the
 *  intermediate block entirely. Pair this with TCE and the function
 *  becomes a 3-instruction dispatch.
 */
int threadable(int x) {
    int cond;
    if (x > 10) cond = 1;
    else        cond = 0;

    if (cond) return A_path(x);
    else      return B_path(x);
}

/*  threadable_phi — value of `r` known on each predecessor.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      threadable_phi:
 *          lea  ecx, [2*rsi + 1]      ; ecx = 2y + 1   (the "x > 0" arm)
 *          xor  eax, eax              ; eax = 0       (the other arm)
 *          test edi, edi
 *          cmovg eax, ecx             ; if x > 0 → use 2y+1
 *          ret
 *
 *  Without threading, the two adds in the body of the second-if would
 *  be reached via different incoming φ values; LVI proves the second-if
 *  condition matches the first and threads them, then SimplifyCFG turns
 *  the chain into a single CMOV.
 */
int threadable_phi(int x, int y) {
    int r;
    if (x > 0) r = y + 1;
    else       r = -1;

    if (r >= 0) return r + y;     /* on x>0 path: (y+1)+y = 2y+1 */
    else        return 0;          /* on x≤0 path: r=-1 → return 0 */
}
