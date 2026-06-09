# 03 — Interprocedural Optimizations (IPO)

Local and loop optimizations look at one function at a time. **IPO** crosses
function boundaries. The single most impactful interprocedural optimization
is **inlining** — almost every other IPO derives its power from being able to
*see across* a call.

## Map

| #  | Example                       | Optimization                                    |
| -- | ----------------------------- | ----------------------------------------------- |
| 01 | `01_inlining.c`               | Inlining (cost model, heuristics)               |
| 02 | `02_ipsccp.c`                 | Interprocedural Sparse Conditional CP           |
| 03 | `03_arg_promotion.c`          | Argument promotion (by-ref → by-value)          |
| 04 | `04_devirtualization.c`       | Devirtualization                                |
| 05 | `05_tail_call.c`              | Tail-call elimination                           |
| 06 | `06_attributes.c`             | Function attributes that unlock IPO             |
| 07 | `07_lto.c` + `07_lto_mod.c`   | Link-Time Optimization, ThinLTO                 |

## Why IPO matters

```
                  ┌─────────────────────────────────┐
                  │  Without IPO every TU is a wall │
                  └────────────┬────────────────────┘
                               │
        ┌──────────────────────┴───────────────────────┐
        ▼                                              ▼
   ┌──────────┐                                  ┌──────────┐
   │  caller  │       opaque call                │  callee  │
   │          │ ────────────────────────────────►│          │
   │  knows:  │                                  │  knows:  │
   │  args    │      nothing about callee        │  body    │
   │  return  │                                  │          │
   └──────────┘                                  └──────────┘
```

With IPO (typically via inlining or LTO):

```
   ┌─────────────────────────────────────────────────┐
   │  optimizer sees BOTH sides of the call          │
   │  – constants in args become known inside body   │
   │  – body's reads/writes inform caller's AA       │
   │  – the call itself often disappears             │
   └─────────────────────────────────────────────────┘
```

## 01 · Inlining

> "Replace a call with a *copy* of the callee's body."

```
   BEFORE                                  AFTER INLINE
   ──────                                  ────────────
   int square(int x) { return x*x; }       (the call site contains)

   int caller(int n) {                     int caller(int n) {
       return square(n + 1);                   int t = n + 1;
   }                                            return t * t;
                                            }
```

### Why inlining is the king

It enables almost every other optimization:

```
   inline ─┬─► constant propagation across call
           ├─► dead-arg elimination
           ├─► escape analysis (callee's locals are visible)
           ├─► alias analysis (no more "opaque call" wall)
           ├─► loop opts on the inlined body
           └─► tail-call combine with caller's epilogue
```

### Cost model (both compilers)

Each candidate gets a *cost* in pseudo-instructions and a *benefit* (constant
folding triggered, branches removed). Inline iff `benefit − cost ≥ threshold`.

Typical knobs:

| Knob                                        | GCC                                  | Clang/LLVM                              |
| ------------------------------------------- | ------------------------------------ | ---------------------------------------- |
| Force inline                                | `__attribute__((always_inline))`     | `__attribute__((always_inline))`         |
| Forbid inline                               | `__attribute__((noinline))`          | `__attribute__((noinline))`              |
| Adjust threshold                            | `--param inline-unit-growth=…`       | `-mllvm -inline-threshold=…`             |
| Force inline of `inline` keyword            | `-finline-functions`                 | always at `-O2+`                         |
| Show decisions                              | `-fopt-info-inline`                  | `-Rpass=inline`, `-Rpass-missed=inline`  |

### What inhibits inlining

- Recursion (only partial inlining of recursive calls).
- Function pointers / indirect calls without devirtualization.
- `noinline`, `volatile` arguments.
- ABI mismatches (variadic, `setjmp`, exceptions in C++).
- Cross-TU calls without LTO (the body just isn't visible).

### Picture: how inlining propagates constants

```
   call site: square(n+1)
                  │
                  ▼ inline
                  │
        ┌──────────────────────┐
        │ int t = n + 1;       │
        │ int r = t * t;       │
        │ return r;            │
        └──────────────────────┘

   If the caller knows n is a constant (say 5), the inlined body becomes
        int t = 6;
        int r = 36;
        return 36;                  ← whole call collapses to a literal
```

## 02 · Interprocedural Sparse Conditional CP (IPSCCP)

SCCP across the whole call graph. If every call to `f(x)` passes `x = 0`,
the optimizer specializes `f` for `x = 0`.

```
   ┌────────────┐  call f(0)  ┌──────────────┐
   │  caller A  │ ───────────►│              │
   └────────────┘             │   f(x)       │   ◄─── IPSCCP discovers
   ┌────────────┐  call f(0)  │   if (x) …   │       x always == 0,
   │  caller B  │ ───────────►│   else …     │       eliminates the
   └────────────┘             └──────────────┘       `if` branch.
```

LLVM: `IPSCCPPass`. GCC: `tree-ipa-ccp` plus `tree-ipa-prop`.

## 03 · Argument promotion

A function that takes a `T *` and only ever dereferences it (no escape, no
stores) can be rewritten to take `T` by value. Result: one less indirection,
the parameter ends up in a register.

```
   BEFORE                                  AFTER
   ──────                                  ─────
   int hot(const int *p) {                 int hot(int v) {
       return *p + 1;                          return v + 1;
   }                                       }
   ...                                     ...
   int r = hot(&x);                        int r = hot(x);
```

LLVM: `ArgumentPromotionPass`. GCC: `ipa-sra` (Scalar Replacement of
Aggregates inter-procedurally).

## 04 · Devirtualization

The C++ equivalent: a virtual call resolved at compile time.

```
   B *p = new D();
   p->foo();                  ───►   D::foo(p);
                                       │
                                       ▼
                                  inline if small enough
```

Conditions:

- The compiler can prove (or assume, with `-fstrict-vtable-pointers`,
  `final`, anonymous namespaces, `-fwhole-program`) that the dynamic type
  is `D`.
- LTO greatly increases the surface where this fires.

LLVM: `WholeProgramDevirtPass` (with LTO). GCC: `-fdevirtualize`,
`-fdevirtualize-speculatively`, `ipa-devirt`.

## 05 · Tail-call elimination (TCE)

If a call is in *tail position*, the caller's frame can be **reused** by
the callee. Recursive calls become loops.

```
   factorial(n, acc) {                   factorial:
     if (n == 0) return acc;             1: cmp n, 0
     return factorial(n-1, acc*n);       2: je  done
   }                                     3: mul acc, n
                                          4: sub n,  1
                                          5: jmp 1             ← TCE: no call!
                                          6: done: mov ret, acc
```

LLVM: `TailCallElimPass`. GCC: `-foptimize-sibling-calls` (on by default).
Both compilers also do **sibling-call** elimination across functions with
compatible signatures.

Caveats:

- Caller and callee ABI must allow it (matching argument shapes; no
  stack-passed `va_args`; no destructors in scope in C++).
- The call must be the *last* thing the caller does *and* its return value
  must flow directly to caller's return.

## 06 · Function attributes that unlock IPO

```c
__attribute__((const))      // No memory reads, no side effects. Strongest.
__attribute__((pure))       // Reads memory but no side effects.
__attribute__((noreturn))   // Lets caller drop the epilogue (e.g. for exit()).
__attribute__((malloc))     // Result aliases nothing the caller can see.
__attribute__((nonnull(1))) // Tells AA that argument 1 isn't null.
__attribute__((returns_nonnull))
__attribute__((cold))       // Demote to .text.cold; reorder branches against it.
__attribute__((hot))        // Promote; preserve I-cache neighbours.
__attribute__((always_inline))
__attribute__((noinline))
__attribute__((flatten))    // Recursively inline everything called.
__attribute__((target("avx2"))) // Compile this function for a specific ISA.
```

LLVM IR equivalents on the function: `readnone`, `readonly`, `writeonly`,
`noreturn`, `nonnull`, `noalias`, `dereferenceable(N)`, `cold`, `hot`.

## 07 · Link-Time Optimization (LTO) and ThinLTO

Without LTO, each translation unit is optimized independently and the linker
just shuffles bytes. With LTO, the optimizer **runs again** at link time
with the *entire program* visible.

```
   ┌────────────┐    ┌────────────┐    ┌────────────┐
   │   a.c      │    │   b.c      │    │   c.c      │
   │   ─►a.o    │    │   ─►b.o    │    │   ─►c.o    │  ← bytecode/IR with -flto
   └──────┬─────┘    └──────┬─────┘    └──────┬─────┘
          └─────────────┬────────────────────┘
                        ▼
              ┌─────────────────────┐
              │ linker merges IR,   │
              │ optimizer runs      │   ◄── whole-program view enables
              │ AGAIN on the whole  │       cross-TU inlining, dead-fn
              │ program             │       elimination, devirtualization
              └──────────┬──────────┘
                         ▼
                       a.out
```

### Full LTO vs ThinLTO

| Aspect              | Full LTO                                  | ThinLTO                                |
| ------------------- | ----------------------------------------- | -------------------------------------- |
| Memory at link      | Linear in program size; can be huge       | Bounded; per-TU + summary              |
| Link wall-clock     | Long, single-threaded link step           | Parallel across TUs                    |
| Distributed build   | Not friendly                              | Cache-friendly, distributable          |
| Quality of opts     | Best                                      | Within ~95% of full LTO                |
| Flags (Clang)       | `-flto`                                   | `-flto=thin`                           |
| Flags (GCC)         | `-flto`                                   | `-flto=jobserver` for parallelism      |

### What LTO actually unlocks

- **Cross-TU inlining** (huge for templated C++ code).
- **Devirtualization** with whole-program assumption.
- **Dead-symbol elimination** at function/global granularity.
- **ConstMerge** of identical constants across TUs.
- **Layout** of `.text` for I-cache (see chapter 10).

### Hello-world demo

```
    07_lto.c        ─── defines  int add(int,int)
    07_lto_mod.c    ─── uses     add() and ALWAYS passes (1,2)

    Without LTO: separate .o; the call to add stays.
    With LTO:    add() is inlined into the caller and the constants
                 propagate. Final code: mov eax, 3 ; ret.
```

Run:

```bash
clang -O2 -flto -c 07_lto.c     -o 07_lto.o
clang -O2 -flto -c 07_lto_mod.c -o 07_lto_mod.o
clang -O2 -flto 07_lto.o 07_lto_mod.o -o 07_lto.out
objdump -d 07_lto.out | sed -n '/<callee>:/,/^$/p'
```

```bash
gcc -O2 -flto -c 07_lto.c     -o 07_lto.o
gcc -O2 -flto -c 07_lto_mod.c -o 07_lto_mod.o
gcc -O2 -flto 07_lto.o 07_lto_mod.o -o 07_lto.out
```

### Pitfalls

- Static libraries must be built with the same LTO bytecode/IR version.
- `__attribute__((used))` may be needed to keep symbols the linker
  can't see referenced (e.g. resolved by `dlsym`).
- Debug-info quality with LTO is improving but still imperfect.

➡ Next: [`04_data_flow_analysis/`](../04_data_flow_analysis/).
