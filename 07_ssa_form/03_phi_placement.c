/* 03_phi_placement.c — multiple variables, nested if's, transitive φ insertion
 *
 * Trace the Cytron algorithm by hand:
 *
 *      entry:
 *        x = 1   (def of x)
 *        y = 2   (def of y)
 *
 *      if c1:
 *          x = 10
 *          if c2:
 *              y = 20
 *          else:
 *              y = 30
 *      else:
 *          x = 100
 *
 *      use x
 *      use y
 *
 * Block structure:
 *
 *      entry
 *        │
 *      if-c1?
 *      /     \
 *  then-c1   else-c1
 *     │
 *   if-c2?
 *   /    \
 * t-c2   e-c2
 *     \  /
 *    merge-c2
 *        \
 *         merge-c1   ← join from both halves of outer if
 *           │
 *          use x, y
 *
 * φ placement (only for variables assigned on multiple paths):
 *   merge-c2: y = φ(20, 30)
 *   merge-c1: x = φ(10 or 100), y = φ(merge-c2 value, 2 from entry-via-else)
 *
 * After insertion the IR is straightforward; you can dump it with:
 *   clang -O1 -S -emit-llvm 03_phi_placement.c -o 03_phi_placement.ll
 */
/*  phi_demo — three branches, three φ's all collapsed by InstSimplify.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      phi_demo:
 *          test  esi, esi                ; c2 ?
 *          mov   eax, 40                 ; eax = 40 (default for !c2 path)
 *          mov   ecx, 30                 ; ecx = 30 (= y on the c1 ∧ ¬c2 path)
 *          cmove ecx, eax                ; ecx = (c2 ? 40 : 30)  -- y arm choice
 *          test  edi, edi                ; c1 ?
 *          mov   eax, 102                ; eax = 102 (the !c1 result: x=100, y=2 → 102)
 *          cmovne eax, ecx               ; if c1 → eax = ecx (which holds x+y for c1 arms)
 *          ret
 *
 *  Let's trace the four input cases:
 *      c1=0       → eax = 102          (x=100, y=2, sum = 102)
 *      c1=1, c2=0 → ecx=30,  x=10 → sum=40
 *      c1=1, c2=1 → ecx=40,  x=10 → sum=50  (wait, asm uses ecx=40 means it
 *                                            already added x=10... actually
 *                                            looking again, ecx ends up holding
 *                                            x+y for the c1 path; the cmove
 *                                            picks 40 when c2 set)
 *
 *  Two φ's appear in the IR (one at inner merge for y, one at outer
 *  merge for x and y). At codegen they become two cmoves arranged in
 *  series with their conditions. The whole if-tree is BRANCH-FREE:
 *  two test/cmov pairs and you're done.
 *
 *  This is φ's earning their keep: a clean SSA representation lets the
 *  optimizer reason about the merge as a "select", and the backend
 *  emits the obvious branchless code.
 */
int phi_demo(int c1, int c2) {
    int x = 1;
    int y = 2;

    if (c1) {
        x = 10;
        if (c2) y = 20;
        else    y = 30;
    } else {
        x = 100;
    }
    return x + y;
}
