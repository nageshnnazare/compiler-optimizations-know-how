/* 01_mem2reg.c — PROMOTE ALLOCAS TO SSA REGISTERS
 * ============================================================================
 *
 * The single most important pass in the LLVM mid-end.
 *
 * Without mem2reg
 * ---------------
 *   The front-end emits every local as a *stack slot* (`alloca`). All
 *   reads are `load`, all writes are `store`. The IR looks like assembly
 *   already; no SSA names exist except the pointers and loaded values.
 *
 *     %x_addr = alloca i32
 *     store i32 0,    i32* %x_addr
 *     ...
 *     %v = load i32, i32* %x_addr
 *     store i32 %v_new, i32* %x_addr
 *
 *   Optimizations like CSE and InstCombine can do almost nothing here:
 *   they can't see across loads and stores without a precise memory
 *   model.
 *
 * With mem2reg
 * ------------
 *   PromoteMemoryToRegister identifies allocas that NEVER have their
 *   address taken (never passed by pointer, never escape) and:
 *     1. Deletes the alloca and its loads/stores.
 *     2. Replaces every load with the value the load would observe.
 *     3. Inserts φ-nodes at CFG merges where multiple stores reach.
 *
 *   Now everything is SSA and the rest of the optimizer can do its job.
 *
 * Pass names
 * ----------
 *   LLVM:  PromoteMemoryToRegisterPass (mem2reg).
 *   GCC :  pass_lower_vector → pass_build_ssa.
 *
 * What blocks promotion
 * ---------------------
 *   – `&local` taken (address escapes to a function or another pointer).
 *   – Address used in a `getelementptr` and stored away.
 *   – Atomic / volatile loads/stores on the slot.
 * ============================================================================
 */
extern void take_addr(int *p);

/*  promote_me — local `x` never escapes → fully promoted to SSA.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      promote_me:
 *          mov   eax, esi           ; eax = a    (default)
 *          test  edi, edi           ; cond ?
 *          cmovne eax, edx          ; if cond → eax = b
 *          ret
 *
 *  WHY: After mem2reg the IR is:
 *      define i32 @promote_me(i1 %cond, i32 %a, i32 %b) {
 *          %x = select i1 %cond, i32 %b, i32 %a
 *          ret i32 %x
 *      }
 *  No alloca, no stack slot. The select lowers to a CMOV: a → eax by
 *  default, b on the true-branch.
 *
 *  WITHOUT mem2reg: 4 stack reads/writes per branch, no folding, ugly.
 */
int promote_me(int cond, int a, int b) {
    int x = a;
    if (cond) x = b;
    return x;
}

/*  does_not_promote — `&x` ESCAPES via take_addr → must remain in memory.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      does_not_promote:
 *          push  rax                    ; align stack + 8 bytes scratch
 *          inc   edi                    ; x = i + 1
 *          mov   dword ptr [rsp + 4], edi   ; STORE to stack slot
 *          lea   rdi, [rsp + 4]              ; address of x → first arg
 *          call  take_addr                   ; pass &x
 *          mov   eax, dword ptr [rsp + 4]   ; RELOAD x (may have been written)
 *          pop   rcx
 *          ret
 *
 *  WHY: The compiler can't prove that `take_addr` doesn't modify *p.
 *  The slot must be addressable; mem2reg refuses to promote it. Two
 *  memory ops per call to take_addr survive.
 *
 *  HOW TO AVOID: if you only need to PASS the value (not let the callee
 *  modify it), take it by value or by `const int *`. With LTO and a
 *  visible `take_addr` body that doesn't write, the compiler could
 *  promote — but defensively it doesn't.
 */
int does_not_promote(int i) {
    int x = i + 1;
    take_addr(&x);
    return x;
}
