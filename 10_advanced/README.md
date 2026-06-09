# 10 — Advanced Optimization Topics

These are the topics that ship-quality compilers, browsers, kernels, and
high-performance language runtimes get their last 5-30% from.

## Map

| #  | Subject                                | What you'll learn                            |
| -- | -------------------------------------- | -------------------------------------------- |
| 01 | Profile-Guided Optimization (PGO)      | Why static heuristics under-fit real code    |
| 02 | AutoFDO / Sampled PGO                  | PGO from production sampling profiles        |
| 03 | BOLT / Propeller / ld layout           | Post-link binary optimization                |
| 04 | Polyhedral / Polly / Graphite          | Affine loop nest restructuring               |
| 05 | Super-optimization (Souper, Alive2)    | Provably-correct peephole synthesis          |
| 06 | Auto-vectorization frameworks (VPlan)  | LLVM's new vectorization architecture        |
| 07 | Instrumentation & sanitizers           | The dark mirror of optimization              |
| 08 | LLVM IR attributes you should know     | A practical reference                        |
| 09 | Compiler-driven security mitigations   | CFI, SafeStack, RetGuard, BTI, PAC           |

## 01 · Profile-Guided Optimization

```
   First build  (instrumented)              Second build (optimized)
   ────────────────────────────             ────────────────────────
        gcc -fprofile-generate                gcc -fprofile-use
                  │                                    │
                  ▼                                    ▼
              ./a.out                              optimized binary
              (writes .gcda)                       (reads .gcda)
                  │                                    ▲
                  └────────────────────────────────────┘
                           pass profile to second build
```

What PGO buys you:

- **Block placement**: hot path is straight-line, cold path is in
  `.text.unlikely`.
- **Inlining**: hot, small callees are aggressively inlined; cold ones
  are not.
- **Loop unroll**: hot loops get unrolled more, cold loops not at all.
- **Switch lowering**: hot cases get the fast path (e.g. fall-through
  rather than table lookup).
- **Function ordering**: hot functions cluster together in the binary →
  better I-cache.
- **Builtin expect**: replaced by actual measured probabilities.

Typical speedup: 5-25% on real applications. For interpreters and
parser-heavy code, sometimes >40%.

### Clang/LLVM workflow

```bash
clang -O3 -fprofile-generate=/tmp/pgo -o app app.c
./app sample_input              # writes /tmp/pgo/default*.profraw
llvm-profdata merge -output=/tmp/pgo/default.profdata /tmp/pgo
clang -O3 -fprofile-use=/tmp/pgo/default.profdata -o app app.c
```

### GCC workflow

```bash
gcc -O3 -fprofile-generate -o app app.c
./app sample_input              # writes *.gcda next to the source
gcc -O3 -fprofile-use -o app app.c
```

## 02 · AutoFDO / Sampled PGO

Instrumentation slows the program 2–5×, so it's painful to run on real
production traffic. **Sample-based PGO** uses `perf` or `LBR` data:

```bash
# Linux: collect a perf profile while the binary runs in production
perf record -e cycles:u -j any,u -o app.perf -- ./app

# Convert to LLVM's format
create_llvm_prof --binary=./app --profile=app.perf --out=app.prof

# Recompile with the sampled profile
clang -O3 -fprofile-sample-use=app.prof -o app.opt app.c
```

GCC's equivalent is `-fauto-profile=app.gcov`.

## 03 · BOLT — post-link optimization

Even after PGO, the linker has placed functions and basic blocks. BOLT
takes the linked binary plus a profile and rewrites the layout
without recompiling:

```bash
perf record -e cycles:u -j any,u -- ./app
perf2bolt -p perf.data -o perf.fdata ./app
llvm-bolt ./app -o ./app.bolted -data=perf.fdata \
    -reorder-blocks=ext-tsp -reorder-functions=hfsort+ \
    -split-functions -split-all-cold -split-eh -dyno-stats
```

Typical extra speedup: 5-15% **on top of** PGO.

The mechanics:

```
       .text
       ┌──────────────┐               .text
       │ hot funcs    │               ┌──────────────┐
       │ mixed with   │ BOLT          │ ALL hot      │
       │ cold ones    │ ───────────►  │ funcs first  │
       │ (heap layout)│               │ ─────────────│
       │              │               │ cold funcs   │
       └──────────────┘               │ (rarely      │
                                      │ touched →    │
                                      │ no I-cache)  │
                                      └──────────────┘
```

Propeller (also from Google) is the per-block sibling of BOLT, integrated
into the linker via thin LTO–style summaries.

## 04 · Polyhedral compilation (Polly, Graphite)

Treats the iteration space of an affine loop nest as a polyhedron and
applies transformations that can be proven to preserve dependencies:

```
   for (i=0;i<N;i++)
     for (j=0;j<N;j++)
        A[i][j] = B[i][j] + C[i][j];
```

The polyhedron is `{(i,j) | 0 ≤ i < N ∧ 0 ≤ j < N}`. Useful
transformations:

- **Loop interchange** (chapter 02).
- **Tiling** (chapter 02).
- **Skewing** to expose parallelism.
- **Fusion** of multiple loops with compatible polyhedra.
- **OpenMP / OpenACC parallelization**: emit `#pragma omp parallel for`.

Tools:

- **Polly** for LLVM. Out-of-tree but in many distros: `clang -O3 -mllvm -polly`.
- **Graphite** in GCC: `-floop-nest-optimize`, `-fgraphite-identity`.

Often disabled by default because the algorithms are O(n³)+ on nest depth
and rarely fire on real C code (constraints too tight).

## 05 · Super-optimization

> Synthesize an instruction sequence that is provably equivalent to a given
> one, but cheaper.

Examples:

- **Souper** (LLVM IR): finds "missed optimizations" by running an SMT
  solver against known-good rewrites.
- **Alive2** (LLVM IR): verifies that an optimization is *correct* by
  symbolic execution.
- **STOKE** (x86 asm): stochastic super-optimizer; replaces a function with
  a synthesized sequence that passes a million random tests.

These tools are how the GCC and LLVM optimizer teams find new peephole
rules and how they catch optimizer bugs.

## 06 · VPlan — LLVM's modern vectorizer

```
       Loop                    Cost                Code
       analyzer  ───────►      model    ────►     gen
       (build VPlan)          (pick VF,           (emit
                                interleave)        IR)
```

VPlan is a "vectorization plan" — a data structure that holds *one or more*
plausible vectorizations of the same loop. The cost model picks among them.

This is what lets the same loop be vectorized differently per target ISA
without each backend duplicating the analysis.

## 07 · Sanitizers as compiler instrumentation

The same machinery that supports profile collection also supports run-time
checks:

| Sanitizer        | Catches                                | Overhead     |
| ---------------- | -------------------------------------- | ------------ |
| `-fsanitize=address` (ASan)  | OOB heap/stack/global access, UAF, leaks | 2-3× memory, 2× CPU |
| `-fsanitize=memory`  (MSan)  | use of uninitialized values         | 3× CPU       |
| `-fsanitize=thread`  (TSan)  | data races                          | 5-15× CPU    |
| `-fsanitize=undefined` (UBSan) | UB (oob shifts, signed wrap, etc.) | <5%         |
| `-fsanitize=leak`            | memory leaks (a subset of ASan)     | tiny        |
| `-fsanitize=cfi`             | control-flow integrity              | <5%         |

UBSan is cheap enough to ship in production for some uses; the others are
debug-only.

## 08 · LLVM IR attributes to know

Function-level:

```
nounwind            does not throw
readnone            doesn't access memory at all (≈ __attribute__((const)))
readonly            only reads memory   (≈ __attribute__((pure)))
writeonly           only writes
argmemonly          memory accesses are confined to its pointer arguments
norecurse           does not call itself transitively
noreturn            never returns       (e.g. abort, exit)
willreturn          always returns (used for purity reasoning)
mustprogress        execution must make forward progress
optnone             never optimize (used at -O0)
alwaysinline        always inline at call sites
naked               omit prologue/epilogue (advanced)
```

Parameter-level:

```
noalias             this pointer does not alias any other (=> restrict)
nocapture           callee does not retain the pointer past the call
nonnull             never null
dereferenceable(N)  at least N bytes are readable
readonly            callee only reads through this pointer
writeonly           callee only writes through it
returned            returned == this argument
```

Knowing what attribute corresponds to which C/C++ idiom is half the battle
when looking at LLVM IR.

## 09 · Security mitigations the compiler emits

Modern toolchains can insert run-time checks and indirect-branch hardening:

```
-fstack-protector-strong       canary on functions with arrays / >8B alloca
-fstack-clash-protection       prevent stack-clash by touching each page
-fcf-protection=full           IBT/ENDBR on indirect branches; SHSTK shadow stack
-mbranch-protection=standard   ARM64: BTI + PAC for return addresses
-fno-plt                       call through GOT directly; PIE-friendly
-fpic / -fPIC                  position-independent code
-fhardened (Clang)             ship-quality bundle of the above
```

➡ Next: [`11_gcc_specific/`](../11_gcc_specific/).
