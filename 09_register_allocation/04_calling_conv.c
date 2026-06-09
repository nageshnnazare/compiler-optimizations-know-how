/* 04_calling_conv.c — interaction of calling convention with allocation
 *
 * A value live across a call must be in a callee-saved register (which the
 * compiler may need to spill on entry/exit anyway) or be spilled to the
 * stack. The fewer values you keep alive across calls, the less the
 * function pays in spills.
 *
 *   ┌──────────────────────────────────────────────────────┐
 *   │ x86-64 SysV:                                         │
 *   │   args:        RDI RSI RDX RCX R8 R9 + XMM0..7       │
 *   │   ret:         RAX (+RDX), XMM0 (+XMM1)              │
 *   │   caller-save: RAX RCX RDX RSI RDI R8 R9 R10 R11     │
 *   │   callee-save: RBX RBP R12 R13 R14 R15               │
 *   └──────────────────────────────────────────────────────┘
 */
extern int side(int);

/*  across_call — `t` is live across the call.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      across_call:
 *          push  rbx                  ; SAVE callee-saved RBX (we're using it)
 *          mov   ebx, edi             ; spill x into RBX (callee-saved)
 *          call  side                 ; side may clobber RDI etc.
 *          add   eax, ebx             ; eax = side(x) + x
 *          add   eax, 7               ; eax = side(x) + x + 7
 *          pop   rbx                  ; restore
 *          ret
 *
 *  WHY: `x` is needed AFTER the call to compute `t = x + 7`. The
 *  caller-saved RDI gets clobbered by the call; the allocator picks a
 *  callee-saved register (RBX) for x. The cost: one push/pop pair to
 *  preserve RBX for our own caller.
 */
int across_call(int x) {
    int t = x + 7;
    int u = side(x);
    return t + u;
}

/*  after_call — same code, different source order — same asm!
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      after_call:
 *          push  rbx
 *          mov   ebx, edi
 *          call  side
 *          add   eax, ebx
 *          add   eax, 7
 *          pop   rbx
 *          ret
 *
 *  IDENTICAL to across_call. WHY: the optimizer reorders ops freely
 *  in SSA. `t = x + 7` doesn't depend on the call's result, so it can
 *  be reordered before OR after the call. The allocator picks the
 *  reordering with the SHORTEST live ranges; here the *same* layout
 *  wins.
 *
 *  HOW TO ACTUALLY REDUCE SPILLS: make `x` not need to live across the
 *  call. For example, pass `x` to side() as part of its return value,
 *  or restructure the algorithm so the post-call computation doesn't
 *  need `x` directly.
 */
int after_call(int x) {
    int u = side(x);
    int t = x + 7;
    return t + u;
}
