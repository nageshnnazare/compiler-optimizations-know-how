/* 02_cfg_simplification.c — SIMPLIFYCFG
 * ============================================================================
 *
 * What it does
 * ------------
 *   A grab-bag of CFG cleanups, run many times in the pipeline. The
 *   most common transforms:
 *
 *     1. Eliminate UNREACHABLE blocks.
 *     2. Merge a block with its sole predecessor when the predecessor
 *        has a single successor (collapse trivial linear chains).
 *     3. Eliminate EMPTY blocks (only contain a branch).
 *     4. Convert a 2-arm if/else with identical code in the head into a
 *        single block.
 *     5. SINK common instructions from both arms of a branch to the
 *        merge point.
 *     6. HOIST common instructions from both arms to the dominator.
 *     7. Convert short if/else into `select` (the if-conversion pass
 *        ultimately consumes those).
 *     8. Tail-call merging.
 *
 * Pass names
 * ----------
 *   LLVM: SimplifyCFGPass; many transforms also in InstCombine.
 *   GCC : cleanup_cfg, recursively called after every other pass.
 * ============================================================================
 */
extern int side(void);

/*  empty_blocks — chain of empty blocks collapses to one straight-line BB.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source: artificial nested-empty-if pattern.
 *
 *  ACTUAL (-O3):
 *      empty_blocks:
 *          lea  eax, [rdi + 1]
 *          ret
 *
 *  WHY: After mem2reg + InstCombine, every branch's condition becomes
 *  a constant or a redundant test. SimplifyCFG collapses the entire
 *  chain into a single basic block: `return x + 1;`.
 */
int empty_blocks(int x) {
    int r = x;
    if (1) {
        if (1) {
            if (1) {
                r = x + 1;
            }
        }
    }
    return r;
}

/*  sink_common — both arms call side() then add 1; sink them.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      sink_common:
 *          push rax
 *          call side               ; ONE call (sunk from BOTH arms)
 *          inc  eax                ; +1 (the common postfix)
 *          pop  rcx
 *          ret
 *
 *  WHAT WE'D EXPECT WITHOUT SINKING:
 *      if (cond) { call side; r = side()+1; }
 *      else      { call side; r = side()+1; }
 *      A naïve compile would emit two distinct call sites.
 *
 *  WHY: SimplifyCFG observes both arms end with the same `r = side()+1`
 *  instructions, removes them from each arm, and inserts them after
 *  the join. The if's condition becomes irrelevant and the branch is
 *  removed entirely.
 */
int sink_common(int cond) {
    int r;
    if (cond) r = side() + 1;
    else      r = side() + 1;
    return r;
}

/*  hoist_common — both arms call side(); hoist it BEFORE the branch.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      hoist_common:
 *          push rbx
 *          mov  ebx, edi
 *          call side               ; ONE call, hoisted ABOVE the branch
 *          xor  ecx, ecx
 *          cmp  ebx, 1
 *          setae cl                ; cl = (cond != 0)
 *          lea  eax, [rax + 2*rcx] ; side() + 2*(cond ? 1 : 0)
 *          dec  eax                ; subtract 1
 *          pop  rbx
 *          ret
 *
 *  WHY: GVNHoist (a sibling of SimplifyCFG) sees both arms compute
 *  `side()`. Hoisting executes side() exactly once. The remaining
 *  if/else `+1 vs -1` is converted to a branchless `2*(cond?1:0) - 1`.
 */
int hoist_common(int cond) {
    int r;
    if (cond) r = side() + 1;
    else      r = side() - 1;
    return r;
}
