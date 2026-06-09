/* 08_branch_folding.c — COMMON TAIL FOLDING (cross-jumping)
 * ============================================================================
 *
 * Idea
 * ----
 *   Two CFG paths that end with IDENTICAL machine code can share that
 *   tail: each path's last few instructions are deleted, and a jump
 *   to the common tail is inserted instead. The savings are code size
 *   (tail emitted once) and often better instruction-cache behaviour.
 *
 *   BEFORE                           AFTER FOLDING
 *   ──────                           ─────────────
 *   if (cond) {                       if (cond) v = a;
 *       v = a;                        else      v = b;
 *       cleanup();                    cleanup();
 *       return v;                     return v;
 *   } else {
 *       v = b;
 *       cleanup();
 *       return v;
 *   }
 *
 * Pass names
 * ----------
 *   LLVM:  BranchFoldingPass (machine-IR), SimplifyCFG (LLVM IR).
 *   GCC :  cross-jumping in cfgcleanup.cc.
 * ============================================================================
 */
extern void cleanup(void);

/*  fold_me — both arms end with `cleanup(); return v;` → folded.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      fold_me:
 *          push  rbp
 *          push  r14
 *          push  rbx
 *          mov   ebx, edx               ; save b
 *          mov   ebp, esi               ; save a
 *          mov   r14d, edi              ; save cond
 *          call  cleanup                 ; ONE call to cleanup
 *          test  r14d, r14d
 *          cmove ebp, ebx               ; if cond == 0 → eax = b
 *          mov   eax, ebp
 *          pop   rbx
 *          pop   r14
 *          pop   rbp
 *          ret                          ; ONE epilogue
 *
 *  WHAT WE'D EXPECT WITHOUT FOLDING:
 *      fold_me:
 *          ; ... save callee-saveds, etc ...
 *          test  edi, edi
 *          je    .else
 *          ; THEN arm:
 *          call  cleanup
 *          mov   eax, esi               ; return a
 *          ; ... pop and ret ...
 *      .else:
 *          ; ELSE arm:
 *          call  cleanup
 *          mov   eax, edx               ; return b
 *          ; ... pop and ret (DUPLICATED) ...
 *
 *  WHY: The two arms have identical tails (`call cleanup ; ret`). The
 *  branch-folder hoists the `cleanup()` ABOVE the branch, then
 *  collapses the if/else (which now only chooses between two values)
 *  into a CMOV. From two call+ret pairs to ONE call + ONE branchless
 *  pick + ONE ret.
 *
 *  Note the order of operations: callee-saved registers must be saved
 *  BEFORE the call (cleanup() may clobber caller-saveds), so a, b, cond
 *  end up in RBP, RBX, R14. After cleanup() returns, the CMOV picks
 *  the result based on cond, and a single epilogue restores them.
 */
int fold_me(int cond, int a, int b) {
    int v;
    if (cond) {
        v = a;
        cleanup();
        return v;
    } else {
        v = b;
        cleanup();
        return v;
    }
}
