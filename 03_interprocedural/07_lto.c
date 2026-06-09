/* 07_lto.c — LINK-TIME OPTIMIZATION (LTO / ThinLTO)
 * ============================================================================
 *
 *   `add` is defined here and called from 07_lto_mod.c. With or without
 *   LTO the source is identical; only the build pipeline changes.
 *
 * What LTO does
 * -------------
 *   1. Each `cc -flto -c file.c` emits *LLVM bitcode* (or GCC GIMPLE)
 *      into the .o file instead of native code.
 *   2. At link time, the linker calls back into the compiler ("LTO
 *      plugin") with all bitcode merged.
 *   3. The optimizer then runs whole-program: inlining, devirtualization,
 *      IPSCCP, IPA-SRA, dead-function removal, etc.
 *   4. Native code is emitted ONCE at the end.
 *
 *   ThinLTO splits the merged bitcode by function, runs per-function
 *   summary analyses, then schedules backend compilation in parallel.
 *   Quality close to full LTO at a fraction of the link time.
 *
 * What changes for THIS function
 * ------------------------------
 *   Without LTO:
 *     add() is compiled to its own little function:
 *       add:
 *           lea  eax, [rdi + rsi]
 *           ret
 *     callee_no_lto() must emit a real call:
 *       callee_no_lto:
 *           mov  edi, 1
 *           mov  esi, 2
 *           jmp  add            ; (TCE turns call+ret into jmp)
 *
 *   With LTO:
 *     The linker sees both TUs in bitcode. The inliner sees add() is a
 *     tiny pure leaf function. It inlines add(1, 2) into callee_no_lto(),
 *     which then constant-folds to:
 *       callee_no_lto:
 *           mov  eax, 3
 *           ret
 *     and `add` itself may be deleted from the final binary as unused.
 *
 * Build & inspect
 * ---------------
 *   Without LTO:
 *     cc -O2 -c 07_lto.c     -o 07_lto.o
 *     cc -O2 -c 07_lto_mod.c -o 07_lto_mod.o
 *     cc -O2    07_lto.o 07_lto_mod.o -o 07_lto.out
 *     objdump -d --disassemble=callee_no_lto 07_lto.out | head -10
 *       ; expect: tail-call (jmp) to add survives.
 *
 *   With LTO:
 *     cc -O2 -flto -c 07_lto.c     -o 07_lto.o
 *     cc -O2 -flto -c 07_lto_mod.c -o 07_lto_mod.o
 *     cc -O2 -flto 07_lto.o 07_lto_mod.o -o 07_lto.out
 *     objdump -d --disassemble=callee_no_lto 07_lto.out | head -10
 *       ; expect: `mov eax, 3 ; ret`. add() inlined, constants folded,
 *       ; original add symbol DCE'd.
 *
 *   With ThinLTO:
 *     cc -O2 -flto=thin -c 07_lto.c     -o 07_lto.o
 *     cc -O2 -flto=thin -c 07_lto_mod.c -o 07_lto_mod.o
 *     cc -O2 -flto=thin 07_lto.o 07_lto_mod.o -o 07_lto.out
 *
 * Why every serious shipping binary uses LTO
 * ------------------------------------------
 *   – Inlining across TU boundaries (especially for getters / small
 *     wrappers in libraries).
 *   – Whole-program devirtualization (C++ virtual calls become direct).
 *   – Dead function elimination across libraries.
 *   – Cross-TU IPSCCP / arg promotion.
 *   – Better register allocation when callees are visible.
 * ============================================================================
 */
int add(int a, int b) {
    return a + b;
}
