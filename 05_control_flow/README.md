# 05 — Control-Flow Optimizations

CFG-shaping passes restructure branches, merges, and calls so that the
backend has the smallest, fastest, and most predictable control flow
possible. They almost always run *together with* SimplifyCFG to clean up
trivial edges.

## Map

| #  | Example                            | Optimization                              |
| -- | ---------------------------------- | ----------------------------------------- |
| 01 | `01_jump_threading.c`              | Jump threading                            |
| 02 | `02_cfg_simplification.c`          | CFG simplification (SimplifyCFG)          |
| 03 | `03_if_conversion.c`               | If-conversion (branch → select/cmov)      |
| 04 | `04_tail_call.c`                   | Tail-call elimination (control-flow side) |
| 05 | `05_switch_lowering.c`             | Switch lowering: jump table, binary search, range tests |
| 06 | `06_block_placement.c`             | Block placement / chain layout            |
| 07 | `07_loop_rotation.c`               | Loop rotation                             |
| 08 | `08_branch_folding.c`              | Branch folding / cross-jumping            |

## Why control-flow matters

```
   Modern OoO core:
   ┌────────────────────────────────────────┐
   │ branch predictor                       │
   │   ↓ predicted target                   │
   │ fetch / decode (16 bytes/cycle)        │
   │   ↓ µops                               │
   │ rename / dispatch                      │
   │   ↓                                    │
   │ schedule / execute                     │
   │   ↓                                    │
   │ retire                                 │
   └────────────────────────────────────────┘

   A mispredict costs ~15-20 cycles. Inserting a branch where a
   conditional-move (`cmov`) would do is one of the most common
   sources of "obvious-in-retrospect" speedups.
```

## 01 · Jump threading

> "If the *condition* of a branch is determined by a chain we've already
> seen, redirect the edge directly to the eventual target."

```
   BEFORE                            AFTER
   ──────                            ──────
       B1: if (x>10) goto B2          B1: if (x>10) goto B4   ← thread
                     else goto B3                else goto B5
       B2: t = 1                      (B2 still exists for any
       B3: t = 0                       other entries; or it is
       B4: if (t)  goto B5             also removed if no one else
              else goto B6             reaches it.)
       B5: A
       B6: B
```

After jump threading, the second branch evaporates because *along the path*
B1→B2→B4 the value of `t` is *known* to be 1.

LLVM pass: `JumpThreadingPass`. GCC pass: `tree-ssa-threadedge.cc`.

## 02 · SimplifyCFG

A grab-bag of CFG clean-ups:

- Remove empty BBs (just an unconditional branch).
- Merge a BB into its single predecessor.
- Hoist common code from two arms of an `if` into the join.
- Sink common tail code.
- Convert `if-x return a; else return b` into `return select(x, a, b)`.
- Collapse trivial switches into branches.

```
   B1: br cond, B2, B3
   B2: x = 1; br B4
   B3: x = 2; br B4
   B4: use x

                 ▼ SimplifyCFG: select

   B1: x = select(cond, 1, 2); use x
```

LLVM: `SimplifyCFGPass`. GCC: `cleanup_cfg` (called repeatedly).

## 03 · If-conversion

Replace a small `if-then-else` with a *predicated* / *select* / *cmov*
sequence, removing the branch entirely.

```
   BEFORE                                  AFTER (cmov-style)
   ──────                                  ──────────────────
   if (a > b) r = x;                       cmp a, b
   else       r = y;                       mov r, y
                                           cmovg r, x
```

When it helps:

- The branch is hard to predict (data-dependent, near 50/50).
- Both arms are short and inexpensive to *speculatively* execute.

When it *hurts*:

- The branch is predictable (e.g. always taken) — `cmov` forces both
  computations.
- One side is much more expensive than the other.

LLVM passes: `SelectOptimize`, `EarlyIfConversion` (in the backend), and
the SimplifyCFG fold mentioned above.

### Vectorized if-conversion

Loops with conditional bodies get vectorized via **masking**:

```c
for (i=0;i<n;i++)
    if (a[i] > 0) b[i] = a[i] * 2;
```

becomes (conceptually):

```
   v = load a[i:i+8]
   m = v > 0           ; mask register
   r = v * 2
   masked-store b[i:i+8] under mask m
```

See chapter 08.

## 04 · Tail-call elimination (control-flow viewpoint)

Already discussed in chapter 03 from the *interprocedural* angle. From the
CFG angle, the transformation replaces

```
   ... ; call f ; ret
```

with

```
   ... ; jmp f
```

— a single back-edge into another function's entry. The caller's frame is
reused.

## 05 · Switch lowering

A `switch (x) { case 0: …; case 1: …; … }` is lowered by the backend into
one of several shapes depending on the case density and target ISA:

```
   Dense (small, contiguous keys):           Sparse (few, scattered keys):
   ┌──────────────────┐                       ┌──────────────────┐
   │ jump table       │                       │ binary search    │
   │  jmp [base+x*8]  │                       │  cmp x, mid      │
   └──────────────────┘                       │  jl  lower       │
                                              │  jg  upper       │
                                              │  ...             │
                                              └──────────────────┘
```

Other shapes:

- **Range tests** (bit-test): for case sets like {3, 7, 9, 12} the backend
  may emit `bt mask, x ; jc target`.
- **`switch` → arithmetic**: for a switch that returns one of a few constants,
  the backend can compute the result without branching at all
  (`return arr[x]` or `return (x==0)*3 + (x==1)*7 + …`).

LLVM: `SwitchInstr` lowering in `CodeGen`. GCC: `tree-switch-conversion`.

## 06 · Block placement

The backend reorders basic blocks so that the **most-likely path** is
straight-line, with the *unlikely* (cold) blocks pushed to the bottom of
the function. This:

- Reduces taken branches → fewer mispredicts.
- Improves I-cache (hot blocks contiguous).

```
   BEFORE                                  AFTER PLACEMENT
   ──────                                  ───────────────
   B1:           (taken 99%)                B1:
        if cold goto B2                       if !cold goto B3
        ...                                   ; B2 inlined immediately after
   B2: <cold>                                 ; ...
   B3: ...                                   B3: ...
                                              ...
                                              cold: ...
                                              jmp B3
```

The `__builtin_expect(x, 0)` / `[[unlikely]]` annotation feeds the
heuristic. Profile data (PGO/AutoFDO) is even better.

LLVM: `MachineBlockPlacementPass`. GCC: `bb-reorder.cc`.

## 07 · Loop rotation

Convert a *while-loop* (test at top) into a *do-while-loop* (test at
bottom) plus a *guard*:

```
   BEFORE (while)                            AFTER (do-while + guard)
   ──────────────                            ────────────────────────
       header:                                if (!cond) goto exit
       if !cond goto exit                     loop:
         body                                   body
         goto header                            if cond goto loop
       exit:                                   exit:
```

This eliminates one branch per iteration and creates the canonical
*pre-header / header / latch / exit* shape required by the vectorizer and
LICM.

LLVM: `LoopRotatePass`. GCC happens implicitly through cfgcleanup.

## 08 · Branch folding / cross-jumping

If two BBs end with identical sequences, the backend can fold them, so
both predecessors branch to the same shared epilogue.

```
   BEFORE                                  AFTER
   ──────                                  ─────
   B1: ... ; jmp epi1                       B1: ... ; jmp shared
   B2: ... ; jmp epi2                       B2: ... ; jmp shared
   epi1: store rax; pop rbp; ret            shared: store rax; pop rbp; ret
   epi2: store rax; pop rbp; ret
```

LLVM: `BranchFolding`. GCC: in cfgcleanup.

➡ Next: [`06_memory_optimizations/`](../06_memory_optimizations/).
