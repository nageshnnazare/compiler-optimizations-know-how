/* 03_coalescing.c — copy coalescing
 *
 * When the allocator sees `vreg_b = mov vreg_a` and the two vregs DON'T
 * interfere (no point where both are live), it assigns them the SAME
 * physical register and the move disappears.
 *
 * In SSA, every assignment creates a new name. Out-of-SSA introduces
 * many trivial copies; coalescing erases most of them.
 *
 *   r1 = ...
 *   r2 = r1                            (move)
 *   use r2
 *   ↓ coalesce
 *   r1 = ...
 *   use r1
 */
extern int compute(int);

/*  coalescable — `b = a` after which only b is used.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      coalescable:
 *          push  rax                 ; align stack
 *          call  compute             ; eax = compute(x)
 *          inc   eax                 ; return value + 1
 *          pop   rcx
 *          ret
 *
 *  WHAT WE'D EXPECT WITHOUT COALESCING:
 *      coalescable:
 *          push  rax
 *          call  compute
 *          mov   ecx, eax            ; mov a → b
 *          inc   ecx
 *          mov   eax, ecx
 *          pop   rcx
 *          ret
 *
 *  WHY: a and b never both live at the same time — after `b = a`, `a`
 *  is dead. The coalescer assigns both vregs to the same physical
 *  register (EAX). The mov disappears. Zero machine instructions for
 *  the assignment.
 */
int coalescable(int x) {
    int a = compute(x);
    int b = a;
    return b + 1;
}

/*  not_coalescable — `return a + b` keeps BOTH live → can't coalesce.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      not_coalescable:
 *          push  rax
 *          call  compute             ; eax = a
 *          add   eax, eax            ; eax = a + b  (since b == a, that's 2*a)
 *          pop   rcx
 *          ret
 *
 *  WHY: a and b are both live at the `return a + b` point. They
 *  interfere. The coalescer would normally insert a mov, but the
 *  optimizer noticed the values are EQUAL and folded `a + b` to
 *  `a + a = 2*a`, expressible as `add eax, eax`. No mov needed because
 *  the value happens to be in the same register.
 */
int not_coalescable(int x) {
    int a = compute(x);
    int b = a;
    return a + b;
}
