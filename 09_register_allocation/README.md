# 09 — Register Allocation

After mid-end optimizations the IR is still in **SSA** with a potentially
unbounded number of names. The **backend** must map those names to a
fixed set of **physical registers** (e.g. 16 GPRs on x86-64, 32 on ARM64),
spilling to the stack whatever doesn't fit. This is the *register
allocator*.

## Map

| #  | Example                              | Topic                                  |
| -- | ------------------------------------ | -------------------------------------- |
| 01 | `01_live_ranges.c`                   | Live ranges, interference graph        |
| 02 | `02_pressure.c`                      | Register pressure & spills             |
| 03 | `03_coalescing.c`                    | Copy coalescing                        |
| 04 | `04_calling_conv.c`                  | ABI: caller- vs callee-saved registers |
| 05 | `05_inline_asm.c`                    | Inline asm constraints                 |

## Vocabulary

- **Virtual register (vreg)**: an SSA name in machine IR. Unlimited.
- **Physical register (preg)**: an actual ISA register. Fixed.
- **Live range of vreg v**: the set of program points where v is live.
- **Interference**: vregs v and w interfere if their live ranges overlap;
  they then must be assigned to different pregs.
- **Spill**: store a vreg to a stack slot because no preg is free at some
  point in its live range.
- **Reload**: load it back when next needed.
- **Coalesce**: merge two vregs related by a `mov` into one, so the `mov`
  disappears.

## The picture

```
        Program point        a   b   c   d   e
        ─────────────────────────────────────────
        a = …                ●
        b = …                ●   ●
        c = …                ●   ●   ●
        d = …                ●   ●   ●   ●
                                          ▲
                       at this point 4 vregs are live → need ≥ 4 pregs
        e = …                            ●
        use a                ●            ●
        use b                    ●        ●

        Live ranges:  a [1..6], b [2..7], c [3..4], d [4..4], e [5..7]

        Interference graph (edges where ranges overlap):
                        a───b
                        │   │
                        c   e
                        │   │
                        d   …
```

The allocator's job: **k-color** this graph with k = number of available
physical registers, spilling as little as possible if it can't.

## Two algorithm families

### Graph-coloring (GCC, classic)

```
   1. Compute interference graph G.
   2. Repeat:
         find node n with < k neighbours; push on stack; remove n from G.
         if none: pick a spill candidate (heuristic); push; remove.
   3. Pop stack: assign each node a color not used by its (remaining) neighbours.
   4. If anything was spilled: insert spill/reload code, redo from step 1.
```

Pros: high-quality allocation.
Cons: O(N²) worst case; compilation slower than linear-scan.

### Linear-scan (LLVM `RegAllocFast` / Greedy)

```
   1. Order vregs by live-range start.
   2. Walk in order:
         expire any ranges that have ended; their pregs go back to the pool.
         allocate a free preg; if none, spill the vreg with the farthest-out
         end point (or the cheapest reload site).
```

Pros: linear time, simple.
Cons: occasional poor decisions for very long-lived vregs.

LLVM's *Greedy* allocator combines linear-scan with iterated splitting +
"hints" from coalescing → top-tier code quality at near-linear cost.

## What you (the programmer) can do

1. **Keep live ranges short.** Don't compute something far before its use.
2. **Avoid deep expression trees** with many simultaneously-live temporaries.
3. **Hint the calling convention** with `__attribute__((regcall))` /
   `[[clang::regcall]]` on x86 if you want more registers used for
   parameters (and have an LTO build).
4. **Minimize call-clobbered values around calls.** A value live across a
   call must be in a callee-saved register or spilled.
5. **Use `register` only for inline asm constraints** (the keyword no longer
   has meaning for allocation since C++17).

## Calling conventions cheat sheet (x86-64 SysV)

```
   Arguments:    RDI, RSI, RDX, RCX, R8, R9       (integer / pointer)
                 XMM0..XMM7                       (float)
   Return:       RAX (+RDX for 128-bit), XMM0 (+XMM1 for FP)
   Caller-saved: RAX RCX RDX RSI RDI R8 R9 R10 R11    (call clobbers them)
   Callee-saved: RBX RBP R12 R13 R14 R15               (caller relies on them)
   Stack ptr:    RSP                                   (must be 16-aligned at call)
   Frame ptr:    RBP (optional; -fomit-frame-pointer drops it)
```

A value used *across* a call must be in a callee-saved register OR spilled
to the stack. That's why heavy use of helper calls inside a hot loop is
expensive: it forces spills.

ARM64 (AArch64 AAPCS):

```
   Arguments:    X0..X7    (integer/ptr)
                 V0..V7    (FP/SIMD)
   Return:       X0 (+X1), V0 (+V1)
   Caller-saved: X0..X17 (and V0..V7, V16..V31)
   Callee-saved: X19..X28, V8..V15
   Stack ptr:    SP        (must be 16-aligned)
   Frame ptr:    X29
   Link reg:     X30
```

## Inline asm — talking to the allocator

The compiler treats inline asm as an opaque instruction whose register
constraints you specify:

```c
int add(int a, int b) {
    int r;
    asm("add %1, %0"
        : "=r"(r)          // output: any GPR
        : "r"(b), "0"(a)   // inputs: b in any GPR; a in same reg as r
        );
    return r;
}
```

Common constraint letters (x86):

| Constraint | Meaning                                |
| ---------- | -------------------------------------- |
| `r`        | any general-purpose register           |
| `=r`       | output, any GPR                        |
| `+r`       | in-out, any GPR                        |
| `m`        | memory operand                         |
| `i`        | immediate constant                     |
| `0`-`9`    | "same register as operand N"          |
| `a`,`b`,`c`,`d` | RAX/RBX/RCX/RDX specifically       |
| `S`,`D`    | RSI / RDI                              |
| `Q`        | a/b/c/d (byte addressable)             |

Use *clobber* lists to tell the allocator which registers your asm
trashes:

```c
asm volatile("cpuid" :: : "rax", "rbx", "rcx", "rdx", "memory");
```

## Dumping allocator decisions

```bash
# LLVM: print after register allocation
clang -O3 -mllvm -print-after=greedy -S 02_pressure.c -o /dev/null 2>&1 \
  | sed -n '/After greedy/,/After/p'

# GCC: dump RTL after RA
gcc -O3 -fdump-rtl-ira-graph -c 02_pressure.c -o /dev/null
ls 02_pressure.c.*ira*

# See spills (LLVM):
clang -O3 -Rpass=regalloc -S 02_pressure.c -o /dev/null
```

➡ Next: [`10_advanced/`](../10_advanced/).
