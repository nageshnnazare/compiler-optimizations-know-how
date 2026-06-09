/* 04_devirtualization.c — DEVIRTUALIZATION
 * ============================================================================
 *
 * Idea
 * ----
 *   Replace an indirect call (`call qword ptr [rax]`) with a direct call
 *   to the function the pointer is *known* to point to. Once direct, the
 *   call can be inlined.
 *
 *   In C++ this is a `virtual` call resolved at compile time. In C it's
 *   any "vtable" pattern: a struct of function pointers initialized with
 *   `static const` values.
 *
 * Pass names
 * ----------
 *   LLVM:  WholeProgramDevirtPass (with LTO), GlobalsAA.
 *   GCC :  ipa-devirt, -fdevirtualize-speculatively.
 *
 * Requirements
 * ------------
 *   – The compiler must prove the called function. This works when:
 *     (a) the vtable field is `const` AND its containing object's
 *         initializer is visible, OR
 *     (b) PGO data narrows the target to one likely callee (speculative
 *         devirt: emit a fast direct call + fallback indirect).
 *   – LTO multiplies the surface area where (a) succeeds.
 * ============================================================================
 */
#include <stddef.h>

typedef struct Op {
    void (*go)(int *);
} Op;

static void real_go(int *a) { *a += 1; }
__attribute__((unused)) static void noop_go(int *a) { (void)a; }

static const Op G = { .go = real_go };

void run(const Op *op, int *a) {
    op->go(a);
}

/*  caller — uses the known constant Op G with a real_go function pointer.
 *  ──────────────────────────────────────────────────────────────────────────
 *  WHAT WE'D EXPECT WITHOUT DEVIRT:
 *      caller:
 *          lea   rdi, [rip + G]
 *          call  run
 *      run:
 *          mov   rax, [rdi]               ; load op->go
 *          mov   rdi, rsi
 *          jmp   qword ptr [rax]          ; INDIRECT call
 *      real_go:
 *          inc   dword ptr [rdi]
 *          ret
 *
 *  ACTUAL (-O3):
 *      caller:
 *          inc   dword ptr [rdi]          ; THE ENTIRE FUNCTION
 *          ret
 *
 *  WHY (three optimizations cascading):
 *    1. DEVIRTUALIZATION: G.go is a const initialiser pointing to
 *       real_go. The compiler sees the constant and replaces the
 *       indirect `op->go(a)` in `run` with the direct call `real_go(a)`.
 *    2. INLINING of real_go into run: body is one statement → trivial.
 *    3. INLINING of run into caller, with G dropped because nothing
 *       accesses it post-inline.
 *
 *  End result: a single `inc dword ptr [rdi]` instruction. The
 *  trampoline-like function-pointer dispatch costs ZERO machine
 *  instructions.
 */
void caller(int *a) {
    run(&G, a);
}

/*  caller_dyn — op is an opaque pointer.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      caller_dyn:
 *          mov   rax, rdi
 *          mov   rdi, rsi
 *          jmp   qword ptr [rax]          ; tail-INDIRECT call
 *
 *  WHY: The compiler doesn't know which Op `op` points to. No devirt
 *  possible (would need PGO or whole-program analysis to know it's
 *  always &G in practice). The call survives — but at least TCE turned
 *  `call ... ; ret` into `jmp ...`, saving the call frame.
 *
 *  TO MAKE THIS DEVIRTUALIZE: build with -flto so the linker can see
 *  all callers and prove that op is always &G. Or speculatively devirt
 *  using PGO data: `if (op == &G) real_go(a); else op->go(a);` which
 *  the branch predictor handles well.
 */
void caller_dyn(const Op *op, int *a) {
    run(op, a);
}
