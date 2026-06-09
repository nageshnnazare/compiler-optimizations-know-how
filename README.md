# Compiler Optimizations — The Complete Field Guide

A one-stop, hands-on, deeply illustrated reference for **GCC** and **LLVM/Clang**
optimization passes. Every example follows the same pattern:

1. **Intuition** — what problem is the pass trying to solve?
2. **ASCII diagrams** — CFG, dataflow lattice, IR before/after.
3. **C source** you can compile and inspect.
4. **NAIVE asm** — the unoptimized version (what a "literal" compiler would emit).
5. **ACTUAL asm** — the real `clang -O3` output in NASM / Intel syntax, annotated
   line by line. All asm comments were generated from real compiler output via
   `scripts/x86_asm.sh` so they match what you'll see on your machine.
6. **WHY** — the legality conditions and cost-model reasoning that justify (or
   block) the transform.
7. **Flags** to enable, disable, dump, and tune the pass in both GCC and Clang.
8. **Pitfalls** — when the pass does nothing, or worse, hurts performance.

**Generate the x86 asm for any example yourself**:

```bash
./scripts/x86_asm.sh 01_local_optimizations/06_strength_reduction.c -O3
./scripts/x86_asm.sh 01_local_optimizations/06_strength_reduction.c -O0   # the naïve version
./scripts/diff_opt.sh 01_local_optimizations/06_strength_reduction.c -O0 -O3
```

> Mantra: *"You can't optimize what you don't measure, and you can't measure what
> you don't understand."* Every optimization in this guide is something a CPU
> architect, kernel developer, HFT engineer or game developer can be expected
> to recognize at the assembly level.

---

## How to use this guide

```
                            ┌────────────────────────┐
                            │   00_fundamentals      │   Start HERE
                            │ pipeline, IR, SSA, CFG │
                            └──────────┬─────────────┘
                                       │
        ┌──────────────────────────────┼──────────────────────────────┐
        ▼                              ▼                              ▼
 ┌──────────────┐             ┌────────────────┐             ┌────────────────┐
 │ 01 local     │             │ 04 dataflow    │             │ 07 SSA form    │
 │ peephole/CSE │◄────────────┤ analyses       ├────────────►│ phi, dom front │
 └──────┬───────┘             └────────┬───────┘             └────────┬───────┘
        │                              │                              │
        ▼                              ▼                              ▼
 ┌──────────────┐             ┌────────────────┐             ┌────────────────┐
 │ 02 loops     │             │ 05 control flow│             │ 06 memory      │
 │ LICM, vec.   │             │ jump thread    │             │ AA, mem2reg    │
 └──────┬───────┘             └────────┬───────┘             └────────┬───────┘
        │                              │                              │
        └──────────────┬───────────────┴──────────────┬───────────────┘
                       ▼                              ▼
              ┌────────────────┐             ┌────────────────┐
              │ 03 inter-proc  │             │ 08 vectorize   │
              │ inline, LTO    │             │ loop+SLP, mask │
              └────────┬───────┘             └────────┬───────┘
                       └──────────────┬───────────────┘
                                      ▼
                              ┌────────────────┐
                              │ 09 reg alloc   │
                              │ coloring, spill│
                              └────────┬───────┘
                                       ▼
                              ┌────────────────┐
                              │ 10 advanced    │
                              │ PGO, BOLT, ... │
                              └────────┬───────┘
                                       ▼
                  ┌────────────────────┴────────────────────┐
                  ▼                                         ▼
         ┌────────────────┐                        ┌────────────────┐
         │ 11 GCC         │                        │ 12 LLVM        │
         │ flags & passes │                        │ opt & PM       │
         └────────┬───────┘                        └────────┬───────┘
                  └──────────────────┬──────────────────────┘
                                     ▼
                           ┌──────────────────┐
                           │  13 practical    │
                           │ godbolt, perf,   │
                           │ benchmarking     │
                           └──────────────────┘
```

## Chapter index

| #   | Folder                          | What you'll learn                                                          |
| --- | ------------------------------- | -------------------------------------------------------------------------- |
| 00  | `00_fundamentals/`              | Compiler pipeline, three-address code, SSA, CFG, basic blocks, DAG.        |
| 01  | `01_local_optimizations/`       | Constant folding, copy/constant propagation, CSE, DCE, algebraic, strength reduction, peephole. |
| 02  | `02_loop_optimizations/`        | LICM, unrolling, peeling, fusion, fission, interchange, tiling, unswitching, IV simplification. |
| 03  | `03_interprocedural/`           | Inlining, IPSCCP, devirtualization, argument promotion, LTO and ThinLTO.   |
| 04  | `04_data_flow_analysis/`        | Lattices, reaching definitions, liveness, available expressions, dominators.|
| 05  | `05_control_flow/`              | Jump threading, tail-call elimination, CFG simplification, if-conversion.  |
| 06  | `06_memory_optimizations/`      | Alias analysis, mem2reg/SROA, store-to-load forwarding, GVN, DSE, escape.  |
| 07  | `07_ssa_form/`                  | SSA construction (Cytron), dominance frontier, phi placement, out-of-SSA.  |
| 08  | `08_vectorization/`             | Loop vectorizer, SLP, masking, alignment, reductions, intrinsics.          |
| 09  | `09_register_allocation/`       | Graph coloring vs linear scan, spilling, coalescing, live ranges.          |
| 10  | `10_advanced/`                  | PGO, AutoFDO, BOLT, Propeller, polyhedral, super-optimization.             |
| 11  | `11_gcc_specific/`              | `-O` levels, `-fopt-info`, dumping passes, plugin hooks.                   |
| 12  | `12_llvm_specific/`             | `opt`, new pass manager, attributes, debugging missed optimizations.       |
| 13  | `13_practical/`                 | Compiler Explorer workflow, `perf`, microbenchmark hygiene, pitfalls.      |

## Building and running

Each subdir has its own `Makefile`. The top-level `scripts/` directory has
helpers for dumping IR, comparing GCC vs Clang output, and producing the
canonical "before / after" assembly:

```bash
cd compiler_optimizations_guide
make -C 01_local_optimizations all          # build everything in chapter 1
make -C 01_local_optimizations 01_const_fold.asm   # dump assembly with -O3
./scripts/diff_opt.sh 01_local_optimizations/01_const_fold.c -O0 -O3
./scripts/dump_llvm_ir.sh 01_local_optimizations/01_const_fold.c
./scripts/dump_gcc_tree.sh 01_local_optimizations/01_const_fold.c
```

## Toolchain expectations

- **GCC**  ≥ 13 (uses `-fdump-tree-…` and `-fopt-info`).
- **Clang/LLVM** ≥ 17 (uses the *new pass manager* and `-mllvm -print-after-all`).
- **objdump**, **perf** (Linux), **dtrace** or **Instruments** (macOS).
- **godbolt.org** for quick exploration; every example here is small enough to
  paste into Compiler Explorer.

## Reading order recommendations

- **Quick refresher** — read `00_fundamentals/`, then jump to the chapter you
  need.
- **Performance engineer onboarding** — 00 → 01 → 02 → 06 → 08 → 10 → 13.
- **Compiler-internals interview prep** — 00 → 04 → 07 → 09 → 06 → 02.
- **Toolchain author / language designer** — 00 → 11 → 12 → 03 → 10.

## A note on style

Every chapter is written so that you can read it linearly, top-to-bottom,
without jumping back. Diagrams are repeated when needed. Examples are short
(under 100 lines), buildable on Linux and macOS, and link to the precise
GCC/LLVM pass that performs the transformation.

> Where assembly is shown, it's **x86-64 SysV** unless otherwise stated, with
> Intel syntax (`-masm=intel`), because that makes register operands easier to
> read. ARM64 equivalents are noted where they differ in interesting ways.
