# 13 — Practical Workflow & Pitfalls

The previous chapters told you what the optimizer does. This chapter tells
you how to **work with it day-to-day** — how to inspect, measure, and avoid
the booby-traps.

## 1. The 5-step optimization recipe

```
   1. MEASURE   – never optimize without a profile.
                  perf, Instruments, VTune, samply, AMD uProf.

   2. ATTRIBUTE – which function/loop is hot?
                  cycles, branch misses, cache misses, port utilization.

   3. INSPECT   – read the generated asm AND the IR.
                  godbolt.org, clang -S, opt -S, gcc -S, objdump.

   4. CHANGE    – source code, attributes, flags, algorithm.
                  Don't change the compiler invocation without rebuilding from clean.

   5. RE-MEASURE – confirm the change actually helped.
                   Beware regression in OTHER benchmarks.
```

## 2. Reading assembly: a quick primer

```bash
# Quickest "what does this compile to?"
clang -O3 -S -fverbose-asm file.c -o -
gcc   -O3 -S -fverbose-asm file.c -o -

# Intel syntax (easier than AT&T)
clang -O3 -S -masm=intel file.c -o -

# Compiler Explorer locally
# https://github.com/compiler-explorer/compiler-explorer
docker run --rm -p 10240:10240 godboltprivate/compiler-explorer:latest
```

For studying single functions, prefer the **godbolt.org** workflow: split
view, side-by-side compiler comparison, color-mapped source-to-asm lines.

### Reading checklist

```
   1. How many instructions per loop iteration?
   2. Are loads/stores aligned (movdqa vs movdqu, ldr vs ldur)?
   3. Are they vectorized (xmm/ymm/zmm registers, v0..v31 on arm64)?
   4. How many branches? Where do they go?
   5. Are calls present that shouldn't be?
   6. Spills? (loads/stores to [rsp+N], sp+N).
   7. Multiplies & divides where shifts would do?
```

## 3. The benchmarking hygiene checklist

```
   ✓ Pin to a single CPU (Linux: `taskset -c 2 ./bench`).
   ✓ Disable Turbo (or fix the frequency).
   ✓ Disable HT-sibling noise (avoid that pair's other thread).
   ✓ Pre-warm the cache once.
   ✓ Use a clock with known overhead (clock_gettime(CLOCK_MONOTONIC_RAW)).
   ✓ Run for *enough* iterations that timer noise < 1%.
   ✓ Report MIN, MEDIAN, and STDDEV (not just one number).
   ✓ Repeat across boots if you care about cache layout effects.
```

A common gotcha:

```c
volatile int sink;
for (int i = 0; i < N; i++) sink = f(i);   /* prevents DCE of f */
```

vs.

```c
__attribute__((noinline)) int probe(int x) { return f(x); }
```

The `volatile sink` trick is what Google Benchmark and Catch2 use under
the hood (`benchmark::DoNotOptimize`).

## 4. Pitfalls — the top 12 in production code

### (1) Implicit memory loads through aliasing pointers

```c
void f(int *a, int *b, int n) {
    for (int i = 0; i < n; i++) a[i] = b[0] + i;
}
```

Without `restrict` the compiler reloads `b[0]` each iteration. Fix with
`restrict` or hand-hoist:

```c
void f(int * restrict a, const int * restrict b, int n) { ... }
```

### (2) Volatile in hot loops

```c
volatile int counter;
for (int i = 0; i < N; i++) counter += do_thing(i);
```

`volatile` forces every read and write — no CSE, no LICM, no vectorization.
Only use it for memory-mapped IO and signal handlers.

### (3) Wrong type-punning

```c
float f = *(float *)&my_int;   /* UB by strict aliasing; may miscompile */
```

Use:

```c
float f;
memcpy(&f, &my_int, sizeof f);
```

### (4) Indirect calls without devirtualization

A struct of function pointers initialized at run-time means every call
through it is a load + indirect branch. If the value is *known at
compile time*, mark it `static const` or use templates / `if constexpr`.

### (5) `int` index variables for large arrays

On x86-64, an `int` index gets sign-extended on every memory access, which
the optimizer occasionally cannot prove away. Use `size_t` (or
`ptrdiff_t`) for indices.

### (6) Reading the IR at -O0 and concluding "the compiler is dumb"

Always look at `-O2`/`-O3` output. -O0 keeps every alloca and every
variable in memory so it can be debugged.

### (7) Building with `-O3 -ffast-math` in shared libraries

`-ffast-math` sets a process-wide MXCSR/FPCR mode flag in some libc
startup. If your DSO is loaded into a process that needs IEEE conformance
(numpy, OpenBLAS), bad things happen. Prefer per-function
`#pragma GCC optimize("fast-math")` if you only need it in your code.

### (8) `__attribute__((always_inline))` on huge functions

You force inlining → blow up the call site → cache thrash. Use only for
small (<10-line) helpers.

### (9) `register` keyword

Has had no effect since C++17 / C11 deprecated it. Doesn't matter — drop
it; the allocator will do the right thing.

### (10) `-Os` cargo-culted

`-Os` can be slower than `-O2` on CPU-bound code. It IS often faster on
I-cache-bound code (web servers, browsers). Profile.

### (11) Compiling at -O2 in one TU and -O0 in another

The optimizer is allowed to inline across TUs only when bodies are
visible (header / LTO). A -O0 caller in TU-B will call a -O2 callee in
TU-A through the ABI, which means the callee's optimizations DO apply,
but the caller around it is dumb.

### (12) Profile mismatch (PGO)

If the input you used to build the profile doesn't resemble real workload
(e.g. training on the regression tests), PGO can make things SLOWER. Use
real inputs.

## 5. Quick reference: tools per task

| Task                                | Tools                                                  |
| ----------------------------------- | ------------------------------------------------------ |
| Read asm side-by-side               | godbolt.org, `diff_opt.sh`                             |
| Read LLVM IR                        | `clang -S -emit-llvm`, `opt -S`, `dump_llvm_ir.sh`     |
| Read GCC GIMPLE/RTL                 | `-fdump-tree-all -fdump-rtl-all`, `dump_gcc_tree.sh`   |
| Discover missed optimizations       | `-Rpass-missed=…` (Clang), `-fopt-info-…-missed` (GCC) |
| Time a microbenchmark               | Google Benchmark, `hyperfine`, `perf stat`             |
| Profile a binary                    | `perf record/report`, `samply`, `Instruments`, VTune   |
| Cache analysis                      | `perf c2c`, `valgrind --tool=cachegrind`               |
| Find UB                             | `-fsanitize=undefined,address,memory,thread`           |
| Verify correctness of an opt        | Alive2 ([alive2.llvm.org](https://alive2.llvm.org))    |
| Find new opt opportunities          | Souper, valgrind/callgrind                             |

## 6. The "Reading Order for Mastery" recap

1. Re-read [`00_fundamentals/`](../00_fundamentals/).
2. Walk through the IR/asm for every example in `01_local_optimizations/`
   until you can predict the output by inspection.
3. Do the same for `02_loop_optimizations/` while staring at
   `-Rpass=loop-vectorize` and `-fopt-info-vec` output.
4. Reproduce the LTO demo in `03_interprocedural/07_lto.c`. Convince
   yourself you understand why the un-LTO version has a call but the
   LTO version doesn't.
5. Re-derive the dominance frontier of the CFG in
   `04_data_flow_analysis/05_dominators.c` on paper. Compare with the
   compiler's φ placement in the IR.
6. Build the PGO demo in `10_advanced/01_pgo_demo.c`. Confirm with
   `objdump -d` that the cold path moved to `.text.unlikely`.
7. Read [`11_gcc_specific/`](../11_gcc_specific/) and
   [`12_llvm_specific/`](../12_llvm_specific/) cover-to-cover.
8. Apply the recipe in this chapter to *your* code base.

## 7. Books to read (chronological, by depth)

- **Cooper & Torczon — *Engineering a Compiler*** — gentle and modern.
- **Muchnick — *Advanced Compiler Design & Implementation*** — the
  textbook every optimizer engineer keeps within reach.
- **Allen & Kennedy — *Optimizing Compilers for Modern Architectures***
  — loops, dependence, vectorization. Still the reference.
- **Hennessy & Patterson — *Computer Architecture: A Quantitative Approach***
  — for the hardware side the optimizer is targeting.
- **Aho, Lam, Sethi & Ullman — *Compilers: Principles, Techniques & Tools***
  — the dragon book; the historical foundation.

## 8. Online resources

- godbolt.org — Compiler Explorer (the most-used compiler interaction
  tool on Earth).
- llvm.org/docs/Passes.html — current LLVM pass reference.
- gcc.gnu.org/onlinedocs/gccint/ — GCC internals.
- Agner Fog's CPU manuals — for the hardware-side cost model.
- The LLVM Weekly newsletter (llvmweekly.org) — what is changing in LLVM
  right now.

➡ End of guide. Go forth and `-O3`.
