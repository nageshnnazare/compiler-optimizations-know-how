/* 06_attributes.c — FUNCTION ATTRIBUTES THE OPTIMIZER CARES ABOUT
 * ============================================================================
 *
 * Attributes are the user's way of HANDING THE OPTIMIZER information it
 * can't derive on its own. They unlock entire categories of transforms.
 *
 * Most useful attributes
 * ----------------------
 *   __attribute__((const))      No memory access, no state, deterministic
 *                               in args. CALLS CAN BE CSE'd. (LLVM:
 *                               readnone willreturn)
 *
 *   __attribute__((pure))       Reads memory but doesn't write. Calls CSE
 *                               across non-writing code. (LLVM: readonly)
 *
 *   __attribute__((noreturn))   Doesn't return. Optimizer knows code
 *                               after the call is unreachable; saves
 *                               prologue/epilogue.
 *
 *   __attribute__((nonnull))    Listed pointer args must be non-null.
 *                               Null checks on those args are deleted.
 *
 *   __attribute__((malloc))     Returns a unique pointer. AA returns
 *                               NoAlias against any other live pointer.
 *
 *   __attribute__((returns_nonnull))    Result is non-null.
 *
 *   __attribute__((cold))       Function is rarely called. Affects
 *                               register allocation (use callee-saveds),
 *                               placement (cold section).
 *
 *   __attribute__((hot))        Function is often called. Affects
 *                               inlining heuristic and placement.
 *
 *   __attribute__((flatten))    Inline all calls inside this function
 *                               aggressively, regardless of cost.
 *
 *   __attribute__((target("X"))) Compile this function with extra ISA
 *                               extensions enabled.
 * ============================================================================
 */
#include <stdint.h>

__attribute__((const)) extern int const_helper(int x);
__attribute__((pure))  extern int pure_helper(int x);

/*  demo_const — `const` lets the call CSE.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source: int a = const_helper(x); int b = const_helper(x); return a+b;
 *
 *  ACTUAL (-O3):
 *      demo_const:
 *          push  rax                      ; align stack for the call
 *          call  const_helper             ; ONE call
 *          add   eax, eax                 ; a + b = 2a
 *          pop   rcx
 *          ret
 *
 *  WHAT WE'D GET WITHOUT `const`:
 *      demo_const:
 *          push  rbx
 *          mov   ebx, edi                 ; save x
 *          call  const_helper             ; first call
 *          mov   ebp, eax                 ; save a
 *          mov   edi, ebx                 ; restore x
 *          call  const_helper             ; SECOND call
 *          add   eax, ebp
 *          pop   rbx
 *          ret
 *
 *  WHY: `const` ≡ LLVM `readnone willreturn`. The optimizer knows the
 *  call is a pure function of its args — same arg, same result, no side
 *  effects. CSE eliminates the second call.
 */
int demo_const(int x) {
    int a = const_helper(x);
    int b = const_helper(x);
    return a + b;
}

/*  demo_pure — `pure` is weaker; a write in between blocks CSE.
 *  ──────────────────────────────────────────────────────────────────────────
 *  Source: int a = pure_helper(x); *flag = 0; int b = pure_helper(x); return a+b;
 *
 *  ACTUAL (-O3):
 *      demo_pure:
 *          ...
 *          call  pure_helper              ; first call
 *          mov   r14d, eax                ; save a
 *          mov   dword ptr [rbx], 0       ; *flag = 0  ← WRITE
 *          ...
 *          call  pure_helper              ; SECOND call (the write killed CSE)
 *          add   eax, r14d                ; a + b
 *          ret
 *
 *  WHY: `pure` ≡ LLVM `readonly`. The helper READS memory; the store
 *  to *flag may have changed something the helper would observe. The
 *  optimizer conservatively re-executes the second call.
 *
 *  To get CSE despite the write: use `const` (helper reads NOTHING) or
 *  add a `restrict` qualifier that proves *flag is in a region the
 *  helper cannot reach.
 */
int demo_pure(int x, int *flag) {
    int a = pure_helper(x);
    *flag = 0;
    int b = pure_helper(x);
    return a + b;
}

/*  demo_noreturn — die() never returns.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      demo_noreturn:
 *          test  edi, edi
 *          js    .die_path                ; if x < 0 → die
 *          inc   edi
 *          mov   eax, edi
 *          ret
 *      .die_path:
 *          push  rax
 *          call  die                      ; NO ret after; saves bytes
 *
 *  WHY: `noreturn` tells the optimizer the call doesn't come back. The
 *  trailing `ret` is omitted (`die` will not unwind). The push for
 *  stack alignment is the only setup; no return-value handling.
 */
__attribute__((noreturn)) extern void die(void);

int demo_noreturn(int x) {
    if (x < 0) die();
    return x + 1;
}

/*  demo_nonnull — null check on a `nonnull` parameter is dropped.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      demo_nonnull:
 *          push  rbx
 *          mov   rbx, rdi
 *          call  use_ptr                  ; passes p, knows p != null
 *          add   eax, dword ptr [rbx]     ; deref p WITHOUT a null check
 *          pop   rbx
 *          ret
 *
 *  WITHOUT nonnull, the compiler would have to keep a `if (!p) return …;`
 *  guard, or at minimum tolerate that `use_ptr(p)` is a no-op when p
 *  is null and the subsequent deref is UB.
 *
 *  WHY: `nonnull` ≡ LLVM `nonnull` parameter attribute on use_ptr's
 *  first arg. Calling with null is UB; the compiler assumes p != null
 *  for the rest of the function, deleting any test against null.
 */
__attribute__((nonnull(1))) extern int use_ptr(const int *p);

int demo_nonnull(int *p) {
    return use_ptr(p) + *p;
}

#if defined(__x86_64__) || defined(_M_X64)
/*  demo_avx2 — only emitted on x86_64; uses AVX2 even at default ISA.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3 -march=x86-64-v2): the inner loop uses VPADDD ymm
 *  instructions because of the `target("avx2")` attribute — even though
 *  the rest of the TU compiles for baseline x86-64-v2 (SSE only).
 *
 *  Use this to opt-in to wider SIMD on a per-function basis without
 *  raising the global ISA level (which would crash on older CPUs that
 *  call ANY function in the TU).
 */
__attribute__((target("avx2")))
void demo_avx2(int * restrict a, const int * restrict b, int n) {
    for (int i = 0; i < n; i++) a[i] = a[i] + b[i];
}
#endif
