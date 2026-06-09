# 11 — GCC Specifics

A practical reference for the GCC pipeline: flags, dumps, plugins, and the
order in which passes run.

## 1. The optimization levels

```
   ┌─────┬─────────────────────────────────────────────────────────────────┐
   │ -O0 │ no optimization. allocas everywhere. easiest to debug.          │
   │ -Og │ -O1 minus passes that hurt debug info quality. shipping debug.  │
   │ -O1 │ basic cleanups: cse, dce, jump-thread, simple inlining.         │
   │ -O2 │ + loop opts, vectorization, IPO, almost everything safe.        │
   │ -O3 │ + aggressive inlining, vectorizer relax cost, partial unroll.   │
   │ -Os │ size-cost-model > speed. like -O2 minus unrolling.              │
   │ -Ofast │ -O3 + -ffast-math (breaks IEEE!).                            │
   └─────┴─────────────────────────────────────────────────────────────────┘
```

Dump the list of passes scheduled at any level:

```bash
gcc -Q -O2 --help=optimizers
gcc -Q -O3 --help=optimizers | head -60
```

## 2. The two IRs

```
   front-end ──► GENERIC ──► GIMPLE ──► RTL ──► asm
                  │           │          │
              language-       SSA-ed     register
              independent     three-     transfer
              trees           address    language
                              code       (≈ asm)
```

- **GENERIC** = language-independent tree from the front-end. Briefly held.
- **GIMPLE** = three-address SSA tree-level IR. ALL the mid-end passes
  work here.
- **RTL** = backend's instruction-selected, near-asm IR. Register
  allocation and scheduling happen here.

## 3. Dumping every pass

```bash
gcc -O3 \
    -fdump-tree-all-graph \
    -fdump-rtl-all \
    -fdump-ipa-all \
    -fopt-info-all \
    -c file.c -o /dev/null
ls file.c.*
```

You'll get one file per pass (e.g. `file.c.157t.optimized`,
`file.c.245r.expand`).

The `-fopt-info-*` flags emit human-readable remarks during compilation:

| Flag                              | What it reports                          |
| --------------------------------- | ---------------------------------------- |
| `-fopt-info-vec`                  | which loops vectorized                   |
| `-fopt-info-vec-missed`           | which did NOT vectorize and why          |
| `-fopt-info-loop`                 | loop transformations applied             |
| `-fopt-info-inline`               | inlining decisions                       |
| `-fopt-info-all`                  | everything                               |
| `-fopt-info-all-optall`           | with full details                        |
| `-fopt-info-…=stderr`             | to stderr (default)                      |
| `-fopt-info-…=file`               | to a file                                |

## 4. Per-pass enable / disable

Almost every transformation can be toggled individually:

```bash
gcc -O2 -fno-inline -c file.c       # disable inlining at -O2
gcc -O2 -ftree-loop-vectorize       # explicit (already on at -O2)
gcc -O2 -fno-tree-loop-vectorize    # disable the loop vectorizer
gcc -O3 -fno-unroll-loops           # at -O3, but no unrolling
gcc -O2 -fno-tree-pre               # disable PRE
gcc -O3 -ftree-loop-distribution    # explicit
```

The full list:

```bash
gcc --help=optimizers | less
gcc --help=params     | less
```

## 5. Tunable parameters

`--param` knobs influence cost models without changing pass selection:

```
   --param max-inline-insns-single=N      smaller → less inlining
   --param max-inline-insns-auto=N        for inline-keyword functions
   --param inline-unit-growth=PCT         max % growth from inlining
   --param max-unroll-times=N             cap on unroll factor
   --param max-unrolled-insns=N           cap on unrolled body size
   --param vect-max-version-for-alias-checks=N
   --param graphite-max-bbs-per-function=N  Graphite's complexity bound
```

## 6. Function-attribute reference

```c
__attribute__((const))
__attribute__((pure))
__attribute__((noreturn))
__attribute__((malloc))
__attribute__((nonnull(1, 2)))
__attribute__((returns_nonnull))
__attribute__((nothrow))
__attribute__((cold))
__attribute__((hot))
__attribute__((always_inline))
__attribute__((noinline))
__attribute__((flatten))                /* inline everything THIS function calls */
__attribute__((target("avx2")))         /* compile this fn for AVX2 */
__attribute__((target_clones("default,avx2,avx512f"))) /* multi-version + IFUNC */
__attribute__((aligned(64)))
__attribute__((packed))
__attribute__((section("name")))
__attribute__((visibility("hidden")))
__attribute__((no_instrument_function))
__attribute__((optimize("Ofast")))      /* per-function -Ofast */
__attribute__((optimize("no-loop-vectorize")))
```

## 7. Pragmas

```c
#pragma GCC optimize("O3,fast-math")        /* enable for this file */
#pragma GCC optimize("O0")                   /* disable for rest of file */
#pragma GCC target("avx2")                   /* enable AVX2 here */
#pragma GCC push_options
#pragma GCC pop_options

#pragma GCC unroll N                         /* unroll the next loop N times */
#pragma GCC ivdep                            /* no loop-carried mem deps */
#pragma GCC novector                         /* don't vectorize */

#pragma GCC poison some_identifier           /* fail if this is used below */
```

## 8. Built-ins worth knowing

```c
__builtin_expect(x, 0)        // branch hint (likely / unlikely)
__builtin_expect_with_probability(x, 0, 0.01)
__builtin_unreachable()       // "control cannot reach here"
__builtin_assume_aligned(p, 64)
__builtin_constant_p(x)       // "is x a compile-time constant?"
__builtin_choose_expr(cond, x, y) // compile-time ternary
__builtin_types_compatible_p(T1, T2)
__builtin_prefetch(p, rw, locality)
__builtin_clz, __builtin_ctz, __builtin_popcount, __builtin_parity
__builtin_clzll, __builtin_ctzll, __builtin_popcountll
__builtin_bswap{16,32,64}
__builtin_add_overflow, __builtin_mul_overflow, __builtin_sub_overflow
```

`__builtin_unreachable` is criminally underused: it lets the optimizer
drop entire branches once it knows they can't happen.

```c
void index_into(int *a, unsigned i) {
    if (i >= 16) __builtin_unreachable();   /* range proof */
    a[i] = 0;                                /* code shrinks accordingly */
}
```

## 9. GCC plugin hook (for the curious)

You can write a pass-plugin in C++:

```bash
g++ -shared -fPIC -o myplugin.so myplugin.cc \
    -I$(gcc -print-file-name=plugin)/include
gcc -fplugin=./myplugin.so -O2 -c file.c
```

The plugin registers callbacks on pass events (e.g. after CCP, after
vectorizer) and can examine or modify GIMPLE / RTL.

## 10. Decoding common pass names in dumps

When you read `file.c.040t.einline` or similar:

```
   prefix                                  pass family
   ─────────────                           ─────────────
   001t.gimple        Initial GIMPLE       (front-end output)
   020t.cfg           CFG construction
   021t.ssa           SSA construction
   050t.einline       Early inlining
   080t.fre1          Full Redundancy Elimination (CSE)
   088t.dce           Dead Code Elimination
   100t.ivcanon       Induction-variable canonicalization
   110t.vrp           Value-Range Propagation
   130t.pre           Partial Redundancy Elimination
   135t.cse           Common Subexpression Elimination
   150t.dom           Dominator-based CSE
   200t.loop          Loop optimizations bundle
   210t.vect          Loop vectorizer
   245r.expand        GIMPLE → RTL
   263r.cse2          RTL CSE
   270r.combine       RTL combiner (peephole on steroids)
   310r.ira           Integrated Register Allocator
   320r.sched         Instruction scheduling
   355r.peephole2     Final peephole
   360r.final         Emit assembly
```

➡ Next: [`12_llvm_specific/`](../12_llvm_specific/).
