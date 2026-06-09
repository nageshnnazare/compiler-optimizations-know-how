# 00 — Fundamentals

Before you can recognize an optimization, you need a shared vocabulary for the
**stages** a compiler goes through and the **shapes** the program takes inside
the compiler. This chapter is a short, picture-heavy tour of the pieces every
later chapter assumes.

> Read this once, then refer back when a later chapter mentions "the CFG",
> "phi nodes", "GIMPLE", or "MIR".

## 1. The compiler pipeline at 30 000 ft

```
                                  source.c
                                     │
                                     ▼
                ┌────────────────────────────────────┐
                │  FRONTEND                          │
                │  ─ lex   → tokens                  │
                │  ─ parse → AST                     │
                │  ─ sema  → typed AST               │
                └────────────────┬───────────────────┘
                                 │ lowering
                                 ▼
                ┌────────────────────────────────────┐
                │  MID-END  ("the optimizer")        │
                │   high-level IR  (GIMPLE / LLVM IR)│
                │   ┌──────────────────────────┐     │
                │   │ analyses  ◄──┐           │     │
                │   │              │  pass     │     │
                │   │ transforms ──┘  manager  │     │
                │   └──────────────────────────┘     │
                └────────────────┬───────────────────┘
                                 │ instruction selection
                                 ▼
                ┌────────────────────────────────────┐
                │  BACKEND                           │
                │   low-level IR (RTL / MIR)         │
                │   ─ instr select                   │
                │   ─ register alloc                 │
                │   ─ scheduler                      │
                │   ─ emit asm/object                │
                └────────────────┬───────────────────┘
                                 ▼
                              object.o
```

**GCC and LLVM both follow this shape**, but use different names:

| Concept                | GCC                       | LLVM / Clang                                  |
| ---------------------- | ------------------------- | --------------------------------------------- |
| Frontend AST           | `tree` nodes              | Clang AST (`clang/AST/*`)                     |
| Mid-end IR             | **GIMPLE** (SSA on trees) | **LLVM IR** (SSA, typed, RISC-like)           |
| Backend IR             | **RTL** (S-expressions)   | **MIR** + Selection DAG                       |
| Pass manager           | Plugins + pass list       | `PassManager` (new PM is now default)         |
| Dump a pass            | `-fdump-tree-<name>`      | `-mllvm -print-after=<name>` or `opt -print-after=<name>` |
| Info on what fired     | `-fopt-info`              | `-Rpass=loop-vectorize`, `-Rpass-missed=…`    |

## 2. The four shapes data takes inside the optimizer

### 2.1 AST (Abstract Syntax Tree)

```c
   x = a + b * c;
```

```
         =
        / \
       x   +
          / \
         a   *
            / \
           b   c
```

Used by the **frontend** for type checking and template instantiation. The
optimizer rarely touches the AST.

### 2.2 Three-address code (TAC)

```
t1 = b * c
t2 = a + t1
x  = t2
```

Every statement does **at most one operation**. This is the historical
"sweet spot" for analysis. Modern IRs (GIMPLE, LLVM IR) are essentially typed
TAC.

### 2.3 Basic block + CFG

A **basic block** is a maximal straight-line sequence with **one entry** and
**one exit**. The CFG is the directed graph of basic blocks.

```c
int f(int x) {
    int y = x + 1;          // BB1
    if (y > 0) {            // BB1
        y = y * 2;          // BB2
    } else {                // BB1
        y = -y;             // BB3
    }
    return y;               // BB4
}
```

```
            ┌────────────┐
            │ BB1        │
            │ y = x + 1  │
            │ br y>0     │
            └─┬────────┬─┘
              │ true   │ false
              ▼        ▼
        ┌────────┐  ┌────────┐
        │ BB2    │  │ BB3    │
        │ y = 2y │  │ y = -y │
        └───┬────┘  └───┬────┘
            └─────┬─────┘
                  ▼
            ┌──────────┐
            │ BB4      │
            │ return y │
            └──────────┘
```

### 2.4 SSA (Static Single Assignment)

In SSA, **every variable is assigned exactly once**. At join points we insert
a synthetic **φ-node** ("phi") to merge incoming values.

```
            ┌─────────────────┐
            │ BB1             │
            │ y1 = x + 1      │
            │ br y1>0         │
            └──┬───────────┬──┘
               ▼           ▼
        ┌────────────┐ ┌────────────┐
        │ BB2        │ │ BB3        │
        │ y2 = y1*2  │ │ y3 = -y1   │
        └─────┬──────┘ └──────┬─────┘
              └───────┬───────┘
                      ▼
              ┌──────────────────────────┐
              │ BB4                      │
              │ y4 = φ(y2 from BB2,      │
              │        y3 from BB3)      │
              │ return y4                │
              └──────────────────────────┘
```

Why SSA matters:

- **Use-def chains** are trivial — each name has exactly one definition.
- **Dead code** becomes "uses ∅".
- Many optimizations (constant propagation, GVN, LICM, register allocation)
  become almost embarrassingly simple to express.

See [`07_ssa_form/`](../07_ssa_form/) for SSA construction and out-of-SSA.

## 3. Where each compiler's IR sits

### LLVM IR (textual `.ll`)

```llvm
define i32 @f(i32 %x) {
entry:
  %y1 = add nsw i32 %x, 1
  %cmp = icmp sgt i32 %y1, 0
  br i1 %cmp, label %if.then, label %if.else

if.then:
  %y2 = shl nsw i32 %y1, 1     ; y * 2 became a shift
  br label %if.end

if.else:
  %y3 = sub nsw i32 0, %y1     ; -y
  br label %if.end

if.end:
  %y4 = phi i32 [ %y2, %if.then ], [ %y3, %if.else ]
  ret i32 %y4
}
```

Properties:

- **Typed** (`i32`, `i64`, `float`, `ptr`).
- **SSA-by-construction** at the function level.
- **RISC-like**: load/store separated from arithmetic.
- Optimizer-friendly attributes: `nsw` (no signed wrap), `nuw`, `inbounds`,
  `noalias`, `dereferenceable(N)`.

Generate it:

```bash
clang -O0 -Xclang -disable-O0-optnone -S -emit-llvm file.c -o file.ll
clang -O3                              -S -emit-llvm file.c -o file.O3.ll
```

### GCC GIMPLE (textual `.gimple`)

```c
f (int x)
{
  int y;
  int _1;

  <bb 2> [local count: 1073741824]:
  y_4 = x_3(D) + 1;
  if (y_4 > 0)
    goto <bb 3>; [50.00%]
  else
    goto <bb 4>; [50.00%]

  <bb 3> [local count: 536870912]:
  y_5 = y_4 * 2;
  goto <bb 5>; [100.00%]

  <bb 4> [local count: 536870912]:
  y_6 = -y_4;

  <bb 5> [local count: 1073741824]:
  # y_2 = PHI <y_5(3), y_6(4)>
  return y_2;
}
```

Generate it:

```bash
gcc -O2 -fdump-tree-ssa-raw -fdump-tree-optimized file.c -o file
ls file.c.*ssa*  file.c.*optimized*
```

### GCC RTL (S-expressions, post-instruction-selection)

```
(insn 6 5 7 2 (set (reg/v:SI 88 [ y ])
        (plus:SI (reg:SI 89 [ x ])
                 (const_int 1 [0x1])))
        (nil))
```

You will see RTL mostly in **register-allocation** and **scheduling** dumps.

## 4. The optimizer is a pipeline of *analyses* and *transforms*

```
        ┌──────────┐    ┌──────────┐    ┌──────────┐
   IR ─►│ analysis │ ─► │ analysis │ ─► │ analysis │ ─► (cached results)
        │  pass A  │    │  pass B  │    │  pass C  │
        └──────────┘    └──────────┘    └──────────┘
              │              │               │
              ▼              ▼               ▼
        ┌──────────────────────────────────────────────┐
        │           transform pass                     │
        │ (uses results above; mutates IR)             │
        └────────────────────┬─────────────────────────┘
                             │ invalidates some analyses
                             ▼
                       fresh analyses, more transforms…
```

A **transform** mutates the IR. An **analysis** is *pure*: it computes
information (dominators, loop nests, alias sets) that transforms consume.

Key analyses (see [`04_data_flow_analysis/`](../04_data_flow_analysis/)):

- **Dominator tree** — node A *dominates* node B if every path from entry to
  B passes through A. Required by SSA construction, LICM, GVN.
- **Loop info** — natural loops, nesting, preheader, latch, exits.
- **Alias analysis (AA)** — "can these two pointers refer to the same memory?"
- **Scalar evolution (SCEV)** — closed-form descriptions of how a value changes
  per loop iteration.
- **Liveness** — which variables are still needed after a given point.

## 5. Optimization levels at a glance

```
          ┌── Debuggability ─────────────────────────────────►
          │
          │  -O0   identity-translate; nothing folded, vars in memory
          │  -Og   like -O0 but with cheap, non-destructive passes
          │  -O1   local clean-ups; CSE, jump-thread, simple inline
          │  -O2   loop opts, vectorization, IPO, almost everything safe
          │  -O3   adds aggressive inlining, vectorizer cost-model relax
          │  -Os   like -O2 but size-cost-model > speed
          │  -Oz   like -Os but more aggressive about size (clang only)
          │  -Ofast = -O3 + -ffast-math (breaks IEEE!)
          │
          ▼ Speed / Code-quality ────────────────────────────►
```

Both compilers ship a *fixed list* of passes per `-O` level. You can dump it:

```bash
# GCC: which passes are scheduled at -O2?
gcc -Q -O2 --help=optimizers | head -40

# LLVM: pipeline description
llvm-as < /dev/null | opt -O2 -disable-output -debug-pass-manager 2>&1 | head -60
```

## 6. A worked end-to-end example

`./examples/pipeline_demo.c` is intentionally small enough to fit on a slide.
Walk through the four shapes:

```bash
make -C 00_fundamentals/examples all
```

You will see:

1. The AST (printed via `clang -Xclang -ast-dump`).
2. The unoptimized LLVM IR (`pipeline_demo.O0.ll`).
3. The optimized LLVM IR (`pipeline_demo.O3.ll`).
4. The final x86-64 assembly (`pipeline_demo.O3.s`).

## 7. Mental model — "the optimizer is a fixed-point loop"

```
                  ┌──────────────────┐
                  │   while changed: │
                  │       run passes │
                  └────────┬─────────┘
                           │
                           ▼
                  ┌──────────────────┐
                  │ canonical form?  │   each pass tries to canonicalize
                  └────────┬─────────┘   so that downstream passes recognize
                           │             patterns more easily
                           ▼
                  ┌──────────────────┐
                  │ profitable?      │   cost model (size vs speed vs both)
                  └────────┬─────────┘
                           │
                           ▼
                  ┌──────────────────┐
                  │ legal?           │   semantics-preserving under language rules
                  └──────────────────┘
```

Every optimization in the rest of this guide can be slotted into these three
questions:

1. **What canonical form does this transformation produce?**
2. **What is the cost model that decides to apply it?**
3. **What legality conditions (aliasing, exceptions, FP, volatility) must hold?**

If you can answer those three for a pass, you understand it.

## Next

➡ Continue to [`01_local_optimizations/`](../01_local_optimizations/).
