/* 01_ssa_intro.c — minimal programs that produce a φ-node
 *
 * `pick` at -O1 keeps a phi. At -O3 the SimplifyCFG pass converts the
 * if/else into a `select` instruction (still SSA, but no phi node).
 *
 * `running_sum` keeps a phi at ANY optimization level, because every loop
 * header in SSA requires one to merge the back-edge.
 *
 * Compile and inspect:
 *   clang -O1 -S -emit-llvm 01_ssa_intro.c -o 01_ssa_intro.O1.ll
 *   clang -O3 -S -emit-llvm 01_ssa_intro.c -o 01_ssa_intro.O3.ll
 *   grep -A2 'phi i32' 01_ssa_intro.O1.ll          ;# 2 phis: pick + sum
 *   grep -A2 'phi i32' 01_ssa_intro.O3.ll          ;# 1 phi: sum only
 */
#include <stddef.h>

int pick(int cond, int a, int b) {
    int x = a;
    if (cond) x = b;
    return x;
    /* AT -O1, the IR ends with:
     *   join: %x = phi i32 [ %a, %entry ], [ %b, %then ]
     *         ret i32 %x
     * AT -O3, SimplifyCFG converts to:
     *   %x = select i1 %cond, i32 %b, i32 %a
     *   ret i32 %x
     */
}

int running_sum(const int *a, size_t n) {
    int s = 0;
    for (size_t i = 0; i < n; i++) s += a[i];
    return s;
    /* The IR always contains:
     *   header: %s = phi i32 [ 0, %entry ], [ %s_next, %latch ]
     *           %i = phi i64 [ 0, %entry ], [ %i_next, %latch ]
     *           ... s_next = s + a[i], i_next = i + 1 ...
     */
}
