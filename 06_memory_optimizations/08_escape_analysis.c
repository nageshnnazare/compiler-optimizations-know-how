/* 08_escape_analysis.c — ESCAPE ANALYSIS
 * ============================================================================
 *
 * Idea
 * ----
 *   A heap allocation that DOES NOT ESCAPE the function (its address
 *   never reaches a global, a return value, or a callee that could
 *   store it) can be:
 *     – converted to a STACK alloca, or
 *     – fully scalarized by SROA/mem2reg.
 *
 *   No malloc/free traffic; no GC pressure (in managed langs); fewer
 *   cache misses.
 *
 * Pass names
 * ----------
 *   LLVM:  CapturePass, MemoryBuiltins; partial heap-to-stack via the
 *          `HeapToStack`/`StackSafety` passes (experimental).
 *   GCC :  ipa-modref + tree-stdarg + alloca lowering.
 *
 * Why C/C++ benefit less than Java/Go
 * ------------------------------------
 *   In C/C++, malloc/free is explicit — programmers usually already
 *   stack-allocate when they can. The optimizer rarely needs to do it
 *   automatically. But escape analysis still drives:
 *     – `noalias` deductions (a non-escaping malloc result aliases
 *       nothing)
 *     – DSE & GVN can act through pointers known not to escape.
 * ============================================================================
 */
#include <stdlib.h>

/*  noescape — `p` is a local, address used only to write & read inside.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      noescape:
 *          lea  eax, [rdi + rsi]    ; (a + b); NO malloc, NO free, NO memory
 *          ret
 *
 *  WHAT WE'D EXPECT WITHOUT ESCAPE ANALYSIS:
 *      noescape:
 *          push  rbx; push r14; push r15
 *          mov   r14d, esi
 *          mov   r15d, edi
 *          mov   edi, 8
 *          call  malloc            ; heap allocation
 *          mov   rbx, rax
 *          mov   dword ptr [rax],     r15d
 *          mov   dword ptr [rax + 4], r14d
 *          mov   eax, dword ptr [rbx]
 *          add   eax, dword ptr [rbx + 4]
 *          mov   rdi, rbx
 *          call  free
 *          ...
 *
 *  WHY: The optimizer proves `p` never leaves the function — no globals
 *  read it, no calls capture it, the return is a scalar. The malloc is
 *  replaced by an alloca; SROA scalarizes the two ints; mem2reg
 *  eliminates the alloca entirely; free() becomes a no-op. The
 *  resulting function is the sum of two args in one lea.
 *
 *  This particular optimization (malloc → stack) requires LLVM's
 *  Attributor + LICM-with-builtins; off by default in some builds but
 *  modern Clang at -O3 does it for sizeof ≤ ~64 bytes.
 */
int noescape(int a, int b) {
    int *p = (int *)malloc(2 * sizeof(int));
    p[0] = a;
    p[1] = b;
    int s = p[0] + p[1];
    free(p);
    return s;
}

/*  escape_return — pointer ESCAPES via return → malloc stays.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      escape_return:
 *          push  rbx
 *          mov   ebx, edi
 *          mov   edi, 4
 *          call  malloc                ; the malloc STAYS
 *          mov   dword ptr [rax], ebx  ; *p = v
 *          pop   rbx
 *          ret                          ; caller gets the pointer
 *
 *  WHY: the address ESCAPES the function (it's the return value); the
 *  caller may store/read/free it. The compiler must heap-allocate.
 */
int *escape_return(int v) {
    int *p = (int *)malloc(sizeof(int));
    *p = v;
    return p;
}

/*  escape_callback — pointer escapes via a function call.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      escape_callback:
 *          push  rbx
 *          mov   ebx, edi
 *          mov   edi, 4
 *          call  malloc
 *          mov   dword ptr [rax], ebx
 *          mov   rdi, rax
 *          pop   rbx
 *          jmp   register_callback     ; TAILCALL passing the pointer
 *
 *  WHY: register_callback() might keep the pointer alive past the end
 *  of THIS function. The compiler can't prove otherwise → must
 *  heap-allocate.
 *
 *  If register_callback had `__attribute__((nocapture))` on its first
 *  arg, escape analysis would prove the pointer doesn't outlive the
 *  call, and malloc could be replaced by an alloca.
 */
extern void register_callback(int *p);

void escape_callback(int v) {
    int *p = (int *)malloc(sizeof(int));
    *p = v;
    register_callback(p);
}
