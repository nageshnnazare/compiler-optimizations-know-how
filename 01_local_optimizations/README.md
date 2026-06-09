# 01 — Local Optimizations

> "Local" = within a **single basic block**, or at most within a single
> function with simple analysis. These are the cheapest passes and the ones
> you must recognize on sight when reading assembly.

## Map of this chapter

| #  | Example                              | Optimization                                   |
| -- | ------------------------------------ | ---------------------------------------------- |
| 01 | `01_const_fold.c`                    | Constant folding                               |
| 02 | `02_const_propagation.c`             | Constant / copy propagation                    |
| 03 | `03_cse.c`                           | Common Subexpression Elimination (CSE)         |
| 04 | `04_dead_code.c`                     | Dead Code Elimination (DCE / ADCE)             |
| 05 | `05_algebraic_simpl.c`               | Algebraic simplification                       |
| 06 | `06_strength_reduction.c`            | Strength reduction (×, ÷, %, mod-pow2)         |
| 07 | `07_peephole.c`                      | Peephole / instruction combining               |
| 08 | `08_branch_simpl.c`                  | Branch simplification & if-merging             |
| 09 | `09_bit_tricks.c`                    | Bit-level idiom recognition                    |
| 10 | `10_load_combine.c`                  | Adjacent load/store combining                  |

## How "local" is local?

```
                    ┌─────────────────────────┐
                    │   single basic block    │   ← classical "local" opts
                    └────────────┬────────────┘
                                 │ widen scope
                    ┌────────────▼────────────┐
                    │ extended basic block    │   ← LLVM's InstCombine fits here:
                    │ (BB plus single-pred    │     it walks up def chains across
                    │ predecessors)           │     trivially-safe boundaries
                    └────────────┬────────────┘
                                 │
                    ┌────────────▼────────────┐
                    │ whole function (CFG)    │   ← "global" optimization, chapter 04+
                    └─────────────────────────┘
```

## 01 · Constant folding

**Definition.** Evaluate any expression whose operands are *known constants*
at compile time. The compiler replaces the expression with its constant
result.

```
   y = 3 * 4 + 1;             ───►   y = 13;
   if (sizeof(int) > 0) ...   ───►   if (true) ...
```

### Diagram — what the optimizer sees

```
   BEFORE                              AFTER
   ───────                            ───────
        +                              13
       / \                            (a constant node;
      *   1                            the whole tree is gone)
     / \
    3   4
```

In SSA / LLVM IR:

```llvm
; before
%t = mul i32 3, 4
%y = add i32 %t, 1
; after the InstSimplify pass
%y = i32 13
```

### Code

See `01_const_fold.c`.

```bash
make 01_const_fold.asm
grep -E '^\s*(mov|imul|add)' 01_const_fold.asm
```

You will see literally `mov eax, 13` at `-O1` and above.

### Where it lives

- **LLVM**: `InstSimplify`, `ConstantFold`, `SCCP` (sparse conditional constant
  propagation does folding + propagation together).
- **GCC**: `fold-const.cc`, the GIMPLE pass `forwprop` (forward propagation).

### Pitfalls

- **Floating-point** folding requires `-ffast-math` for results that change
  the rounding behaviour (e.g. reassociating `(a + b) + c`).
- **Volatile** operands are never folded.
- Folding of `1 << 64` (UB) is allowed but typically suppressed under
  `-fsanitize=undefined`.

---

## 02 · Constant & Copy Propagation

**Constant propagation** replaces uses of a name by its known constant value.
**Copy propagation** replaces uses of a copy `b = a` by `a` itself. SSA makes
both trivial because each name has exactly one definition.

```
   BEFORE                       AFTER constant prop      AFTER copy prop
   ───────                      ──────────────────       ──────────────────
   a = 5                        a = 5                    a = 5
   b = a + 2                    b = 7                    b = 7
   c = b * 3                    c = 21                   c = 21
   d = c                        d = 21                   d = 21   (or omitted)
```

### Diagram — SSA makes it a single hash-table walk

```
   def %a = 5
   use %a in %b = add %a, 2     ─► substitute, fold → %b = 7
   use %b in %c = mul %b, 3     ─► substitute, fold → %c = 21
```

In LLVM, this is performed by **SCCP** (Sparse Conditional Constant
Propagation) and **InstCombine**. SCCP is *conditional* because it tracks
which edges in the CFG are *reachable*; a branch that proves to be never-taken
is removed.

### Where it lives

- **LLVM**: `SCCPPass`, `IPSCCPPass` (interprocedural), `InstCombinePass`.
- **GCC**: `tree-ssa-ccp.cc` (conditional constant propagation),
  `tree-ssa-copy.cc` (copy propagation).

---

## 03 · Common Subexpression Elimination (CSE)

If the same expression `e1` is computed twice and *neither operand changed*
between the two evaluations, compute it once and reuse the result.

```
   BEFORE                                 AFTER
   ───────                                ─────
   t1 = a*b + 5;                          tmp = a*b + 5;
   ...                                    t1  = tmp;
   t2 = a*b + 5;                          t2  = tmp;
```

### Diagram — value numbering

CSE is one instance of **value numbering**. Every value gets a number;
expressions that hash to the same number are interchangeable.

```
   value table
   ┌───────────────┬────┐
   │ const 5       │ v1 │
   │ load a        │ v2 │
   │ load b        │ v3 │
   │ mul v2 v3     │ v4 │
   │ add v4 v1     │ v5 │   ← any later expression hashing to (add v4 v1)
   └───────────────┴────┘     is replaced by v5.
```

### Where it lives

- **LLVM**: `EarlyCSE`, then global `GVN` (Global Value Numbering) which is
  CSE across the whole CFG.
- **GCC**: `tree-ssa-pre.cc` (Partial Redundancy Elimination subsumes CSE),
  `tree-ssa-dom.cc` (dominator-based CSE).

### Pitfalls

- A function call between the two uses with **unknown side effects** kills
  the redundancy. Marking functions `__attribute__((pure))` / `const` (GCC)
  or `readnone` / `readonly` (LLVM) tells the compiler the call is safe to
  elide.

---

## 04 · Dead Code Elimination

If an instruction's result is **never used** *and* the instruction has no
side effects, remove it. **Aggressive DCE (ADCE)** is the dual: assume every
instruction is dead, then mark live anything that affects program output, and
delete the rest.

```
   BEFORE                            AFTER
   ───────                          ───────
   a = expensive_thing();           // gone
   b = compute();                   b = compute();
   return b;                        return b;
```

### Diagram — liveness sweep

```
            ┌────────┐
            │ ret b  │ ◄── live (program output)
            └────┬───┘
                 │ uses b
                 ▼
            ┌────────┐
            │ b = ...│ ◄── live
            └────────┘
            ┌─────────────────────┐
            │ a = expensive_thing │ ◄── nothing uses it; no side effects
            │                     │     → DEAD, removed.
            └─────────────────────┘
```

### Where it lives

- **LLVM**: `DCEPass`, `ADCEPass`, `BDCEPass` (bit-tracking DCE for masked-out
  bits).
- **GCC**: `tree-ssa-dce.cc`.

### Pitfalls

- An apparently-dead store can be **observable** through `volatile`, `atomic`,
  signal handlers, longjmp, or `setjmp`. The compiler errs on the side of
  keeping it.

---

## 05 · Algebraic simplification

Replace expressions by mathematically-equivalent but cheaper forms.

```
   x + 0          ───► x
   x * 1          ───► x
   x * 0          ───► 0          (for integers; FP needs careful)
   x - x          ───► 0          (UB-aware for FP NaN)
   x & 0          ───► 0
   x | 0          ───► x
   x ^ x          ───► 0
   (x << a) << b  ───► x << (a+b) (under nsw/nuw)
   !!x            ───► x ≠ 0      (boolean idiom)
```

### Pitfalls — IEEE-754 is not algebra

For `float`/`double` the compiler will *not* perform `x + 0 → x` unless you
opt in:

```
   x + (-0.0)   is x        (always, by IEEE)
   x + 0.0      is x        ONLY if you can prove x is not −0.0
                            (because (−0.0) + 0.0 == +0.0)
```

That's why aggressive FP simplification needs `-ffast-math` (GCC/Clang) or
`-ffinite-math-only`, `-fno-signed-zeros`, etc.

---

## 06 · Strength reduction

Replace **expensive** operations by **cheaper** equivalent ones.

```
   x * 2               ───►  x << 1
   x * 8               ───►  x << 3
   x / 2  (unsigned)   ───►  x >> 1
   x % 8  (unsigned)   ───►  x & 7
   x * 9               ───►  (x << 3) + x        (lea on x86: lea eax,[rax*8+rax])
   x * 10              ───►  (x << 3) + (x << 1)
   x * 7               ───►  (x << 3) - x
```

For **division by a constant**, both compilers replace `x / c` by a
multiplication by a magic constant and a shift (Granlund & Montgomery 1994).
You'll see code like:

```asm
   ; x / 10, unsigned 32-bit
   mov edx, 3435973837     ; 0xCCCCCCCD = ceil(2^33 / 10)
   imul rax, rdx
   shr  rax, 35
```

### Loop-level strength reduction (chapter 02)

Inside loops, **induction variables** like `arr[i*4]` get strength-reduced to
pointer increments:

```
   for (i=0;i<n;i++) sum += arr[i*4];
                 │
                 ▼
   p = arr;
   for (i=0;i<n;i++) { sum += *p; p += 4; }
```

See `02_loop_optimizations/`.

---

## 07 · Peephole / instruction combining

A peephole optimization looks at a **small window** (2–4 instructions) and
recognizes a better sequence. LLVM's `InstCombine` is the canonical
implementation: a thousand rewrite rules applied to a fixed point.

Examples:

```
   lea  rax, [rdi + 0]                ───►  mov rax, rdi
   add  eax, 1                        ───►  inc eax        (size only, often slower)
   cmp  eax, 0                        ───►  test eax, eax
   xor  eax, eax  /  mov eax, 0       ───►  xor eax, eax   (smaller, breaks dep)
   mov  rcx, rax  /  shr rcx, 32      ───►  mov ecx, eax   (zero-extends already)
```

The most important LLVM peephole rule:

```
   (x & C1) << C2   ─►   (x << C2) & (C1 << C2)   if no overflow
```

This drives many subsequent simplifications.

---

## 08 · Branch simplification

Constant condition or trivially-true / false → remove the branch.

```c
if (1)  X; else Y;   ───►   X;
if (0)  X; else Y;   ───►   Y;
if (p)  return 1;
        return 0;    ───►   return !!p;
```

The cleanup is performed by **SimplifyCFG** in LLVM and `cleanup_cfg` in GCC.
See `08_branch_simpl.c`.

```
   BEFORE                                AFTER
   ───────                              ───────
       BB1                                BB1
        │ if 1                            │
        ├────────►BB2 (X)                 ▼
        └────────►BB3 (Y)                BB2 (X)
                    └─►BB4                 └─►BB4
```

---

## 09 · Bit-level idiom recognition

Both compilers recognize hand-rolled bit tricks and turn them into single
machine instructions:

| Source idiom                     | Lowered to                          |
| -------------------------------- | ----------------------------------- |
| `x & -x`                         | "isolate lowest set bit" (`BLSI`)    |
| `x & (x - 1)`                    | "clear lowest set bit" (`BLSR`)      |
| `(x ^ (x-1)) + 1) >> 1`         | depends on backend                   |
| `popcount(x)` loop               | `POPCNT` (with `-mpopcnt`)           |
| count trailing zeros loop        | `TZCNT` / `BSF`                      |
| `(uint64_t)hi << 32 \| lo`      | single 64-bit load on x86            |
| `byte-swap` pattern              | `BSWAP`                              |
| memcpy-style byte loop           | `REP MOVSB` or vectorized            |

See `09_bit_tricks.c` for source you can compile and inspect.

---

## 10 · Load / store combining

Adjacent loads of `uint8_t a[4]` that are then shifted and OR'd into a
`uint32_t` get combined into a **single 32-bit load** (subject to alignment
and aliasing). This is one of the most under-appreciated transformations
because it removes whole loops.

```c
uint32_t v = a[0] | (a[1] << 8) | (a[2] << 16) | (a[3] << 24);
```

```asm
   ; -O3, x86-64 (assuming sufficient alignment)
   mov   eax, DWORD PTR [rdi]
```

See `10_load_combine.c`.

LLVM passes: `Load/Store Vectorizer`, `MemCpyOpt`.
GCC: `store-merging`, `tree-ssa-strlen.cc`.

---

## Building and exploring

```bash
make all              # build every binary
make asm              # produce .asm files (Intel syntax where supported)
make ll               # produce LLVM IR files
make clean
```

For any one example:

```bash
../scripts/diff_opt.sh 01_const_fold.c -O0 -O3   # side-by-side asm
../scripts/dump_llvm_ir.sh 01_const_fold.c       # IR at O0 and O3
../scripts/dump_gcc_tree.sh 01_const_fold.c      # GIMPLE/RTL dumps + opt-info
```

➡ Next chapter: [`02_loop_optimizations/`](../02_loop_optimizations/).
