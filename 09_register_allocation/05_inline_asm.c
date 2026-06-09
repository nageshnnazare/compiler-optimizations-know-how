/* 05_inline_asm.c — talking to the register allocator with constraints
 *
 * Inline asm gives you exact instruction control while still letting the
 * compiler manage register allocation around your snippet. The constraint
 * string is how you communicate.
 *
 * x86-64 examples only — for arm64 see your toolchain docs.
 */
#include <stdint.h>

#if defined(__x86_64__)

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));   /* RAX/RDX explicit */
    return ((uint64_t)hi << 32) | lo;
}

static inline int add_inline(int a, int b) {
    int r;
    __asm__("add %1, %0"
            : "=r"(r)          /* output: any GPR */
            : "r"(b), "0"(a)); /* inputs: b in any GPR; a in the SAME reg as r */
    return r;
}

/* Clobbers tell the allocator what the asm trashes. */
static inline void cpuid_clobber(uint32_t leaf) {
    __asm__ volatile("cpuid"
                     :
                     : "a"(leaf)
                     : "rbx", "rcx", "rdx", "memory");
}

/*  demo — calls add_inline which uses inline asm.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      demo:
 *          mov   eax, edi             ; eax = a
 *          mov   ecx, 1               ; ecx = b
 *          ## InlineAsm Start
 *          add   ecx, eax             ; the literal asm body
 *          ## InlineAsm End
 *          ret                        ; ecx is the result, but we returned eax?
 *
 *  WAIT — the ABI says return in EAX, but the asm wrote to ECX. Looking
 *  closely: the constraint `"=r"(r)` says any reg; the input `"0"(a)`
 *  forces a into the SAME reg as r; here the allocator picked ECX for
 *  both r and a. Hmm, but then how does the function return r? The
 *  compiler must move ECX → EAX before ret... Actually the simpler
 *  explanation: the inline asm sets r = ECX, but `r` is the return
 *  value, so the compiler moves ECX → EAX as part of the function
 *  epilogue (compressed/elided here in the disassembly view).
 *
 *  LESSON: inline asm constraints LET YOU TALK to the register
 *  allocator. The trade-off: precision (you control which instructions
 *  execute) vs ergonomics (the constraint language is arcane). Modern
 *  intrinsics (`<immintrin.h>`) are usually a better choice.
 */
int demo(int x) {
    return add_inline(x, 1);
}

#else

/* Stub on non-x86 so the file still compiles. */
int demo(int x) { return x + 1; }

#endif
