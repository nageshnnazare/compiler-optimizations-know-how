# 07 — Static Single Assignment (SSA) Form

SSA is the *single most important* representational choice in modern
compilers. Every later chapter assumes you can read it; this one shows you
how it is built and what makes it tick.

## Map

| #  | Example                            | Topic                                  |
| -- | ---------------------------------- | -------------------------------------- |
| 01 | `01_ssa_intro.c`                   | Why SSA?                               |
| 02 | `02_dominance_frontier.c`          | Dominance frontier (where φ's live)    |
| 03 | `03_phi_placement.c`               | Cytron-style φ insertion + renaming    |
| 04 | `04_memory_ssa.c`                  | MemorySSA — SSA for memory effects     |
| 05 | `05_out_of_ssa.c`                  | Leaving SSA (Lost-Copy / Swap problem) |

## What SSA is, in one sentence

> *Every variable is assigned exactly once; at every join, a φ-node selects
> which incoming definition flows out.*

```
   non-SSA                              SSA
   ───────                              ───
   x = 1                                x_1 = 1
   if cond:                             if cond:
       x = 2                                x_2 = 2
   print(x)                             x_3 = φ(x_1, x_2)
                                         print(x_3)
```

Why bother?

```
   ┌──────────────────────────────────────────────────────────┐
   │  Each name has exactly ONE definition.                   │
   │  ⇒ use-def chains are trivial (1 ↔ 1).                   │
   │  ⇒ "is this expression invariant?" = single hop.         │
   │  ⇒ "what value flows here?" = follow the def.            │
   │  ⇒ constant propagation, GVN, LICM are textbook short.   │
   │  ⇒ dead code = uses-list empty.                          │
   │  ⇒ Register allocation reuses SSA live ranges directly.  │
   └──────────────────────────────────────────────────────────┘
```

## SSA construction in three steps (Cytron et al., 1991)

```
   1. Compute the DOMINANCE FRONTIER for every block.
       DF(B) = { Y | B dominates a pred of Y but does not dominate Y }
       Intuition: DF(B) is "the first node where someone else's data joins
       with mine".

   2. Insert φ-nodes:
       for each variable v assigned in block B:
           for each Y in DF(B):
               insert  v = φ(...) at the start of Y.
       (Apply transitively — inserting a φ defines v in Y, so iterate.)

   3. Rename:
       Walk the dominator tree in pre-order.
       For each block:
           For each statement that assigns to v: bump v's "version" counter.
           For each use of v: replace with the current top-of-stack name.
       Push/pop a stack per variable.
```

The art is in step 1 (DF) and step 3 (rename). Let's picture them.

## Dominance frontier — a picture

```
            Entry
              │
              ▼
              A
            ┌─┴─┐
            B   C
            │   │
            └─┬─┘
              D
              │
            Exit

   Dominator tree:        DOMINANCE FRONTIER:
        Entry              DF(Entry) = ∅
          │                DF(A)     = ∅  (A dominates D; D's predecessors
          A                                are B and C, both dominated by A)
        ┌─┼─┐              DF(B)     = { D }
        B C D              DF(C)     = { D }
                           DF(D)     = ∅
```

So if `x` is assigned in B (and not in C), we still need a φ in D, because
D is on B's frontier — control may arrive at D from C *without* going
through B.

## Renaming — a picture

```
   Before                        After renaming
   ──────                        ──────────────
        x = 1                      x_1 = 1
        if cond:                   if cond:
            x = x + 5                  x_2 = x_1 + 5
            x = x * 2                  x_3 = x_2 * 2
        x = φ(...)                  x_4 = φ(x_1 from entry,
                                              x_3 from then)
        print x                     print x_4
```

Versioning gives every definition a fresh name; the φ at the join binds
them back together.

## Pruned, semi-pruned, minimal — variants of SSA

- **Minimal SSA**: place a φ at every dominance-frontier point of every def.
  Often inserts redundant φs.
- **Semi-pruned**: only insert φs for variables live across blocks. (default
  in many textbooks.)
- **Pruned SSA**: only insert a φ if the variable is live at the φ's block.
  (LLVM uses this.)

## Memory in SSA — MemorySSA

Plain SSA only renames *scalars*. Memory operations (load/store) are not
inherently SSA. **MemorySSA** is a separate, parallel SSA representation
just for memory effects:

```
   ; pseudo-MemorySSA
   1 = MemoryDef(LiveOnEntry)   ;  store i32 0, ptr %p
   2 = MemoryDef(1)             ;  store i32 1, ptr %q
   MemoryUse(2)                 ;  load  i32, ptr %r
```

Each `MemoryDef` is an SSA name representing "the state of memory after this
store"; `MemoryUse` consumes a state. With this, **GVN**, **LICM**, **DSE**
walk a single SSA-style chain to answer "what stores reach this load?".

LLVM: `MemorySSA` (analysis pass). GCC's equivalent is the `vops` (virtual
operands) on GIMPLE.

## Leaving SSA: the swap problem

Before code generation we must eliminate φs. Naïvely:

```
   x = phi(a, b)         →   at every predecessor:
                                  copy x ← a   (in pred 1)
                                  copy x ← b   (in pred 2)
```

But this can introduce *correctness bugs* in the presence of *parallel*
copies:

```
   At a join we have:           Naïve sequentialization gives:
      x = phi(y, ...)             x ← y
      y = phi(x, ...)             y ← x      ← y already overwritten!
```

This is the **swap problem**. Solutions:

1. **Critical-edge splitting** — insert a new BB on every CFG edge with
   multiple predecessors / successors, so the parallel copies become
   sequential within one BB.
2. **Coalescing-aware out-of-SSA** (Sreedhar et al.) — emit a temporary
   when needed:
   ```
       t ← y
       x ← y
       y ← t
   ```

LLVM does this in `PHIEliminationPass` + `MachineCopyPropagation`.

## Example: ssa_intro

`01_ssa_intro.c` is the smallest function whose IR contains both a φ-node
and a memory-SSA scope. Dump it:

```bash
clang -O1 -S -emit-llvm 01_ssa_intro.c -o 01_ssa_intro.ll
```

Look for `phi i32 [%a, %entry], [%b, %then]` — that's the φ.

➡ Next: [`08_vectorization/`](../08_vectorization/).
