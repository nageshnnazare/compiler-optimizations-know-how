# 04 — Data-Flow Analysis

> Optimizations are *transformations*. Data-flow analyses are the *evidence*
> they need before firing. This chapter gives you the four canonical
> analyses, their lattices, and their direction.

## Map

| #  | Example                              | Analysis                                   |
| -- | ------------------------------------ | ------------------------------------------ |
| 01 | `01_reaching_definitions.c`          | Reaching definitions                       |
| 02 | `02_live_variables.c`                | Liveness                                   |
| 03 | `03_available_expressions.c`         | Available expressions                      |
| 04 | `04_very_busy_expressions.c`         | Very-busy expressions                      |
| 05 | `05_dominators.c`                    | Dominators & post-dominators               |
| 06 | `06_constant_propagation.c`          | Constant propagation lattice               |

## The big picture

Every classical analysis fits in this 2×2 table:

```
                       │       MAY (∪)             │       MUST (∩)
        ───────────────┼───────────────────────────┼────────────────────────
        FORWARD        │  Reaching definitions     │  Available expressions
                       │  Constant propagation     │
        ───────────────┼───────────────────────────┼────────────────────────
        BACKWARD       │  Live variables           │  Very-busy expressions
                       │                           │
```

- **Direction**: forward = data flows from entry; backward = from exits.
- **Confluence operator at a join**:
  - **∪ (may)** — value at the join = union of incoming sets.
    "Property X *may* hold here."
  - **∩ (must)** — value at the join = intersection of incoming sets.
    "Property X *must* hold here."

Each analysis is solved by a **fixed-point iteration** on the CFG:

```
   for each block B:  IN[B] = initial
   repeat until no change:
       for each block B in <direction> order:
           IN[B]  = ⨁ OUT[predecessors] / OUT[successors]
           OUT[B] = transfer(B, IN[B])
```

`⨁` is `∪` (may) or `∩` (must). `transfer(B, IN)` is the **transfer function**
of block B — what the block does to the incoming facts.

## 1. Reaching definitions (FORWARD, MAY)

A definition `d: x = e` at point p **reaches** point q if there is a CFG path
p → q on which x is not redefined.

```
                d1: x = 10
                     │
                     ▼
                  B1 (uses x)             ← d1 reaches B1
                     │
              ┌──────┴──────┐
              ▼             ▼
        d2: x = 20      (no def)
              │             │
              └──────┬──────┘
                     ▼
                  B4 (uses x)             ← {d1, d2} both reach B4
```

**Used by**: SSA construction (where to place φ-nodes), use-def chains,
constant propagation.

**Transfer function**:
```
   OUT[B] = (IN[B]  \ KILL[B]) ∪ GEN[B]
   GEN[B]  = defs in B reaching the end of B
   KILL[B] = all other defs of the same variables
```

## 2. Liveness (BACKWARD, MAY)

A variable v is **live** at point p if some path from p uses v before redefining it.

```
   B1:                     B1 ← {a}  (a is live here because some path uses it)
       a = 5
       b = 7
       ┃
       ▼
   B2:    if cond
       /         \
   B3:            B4:
   c = a + 1       d = b * 2
   ret c           ret d

   Liveness at exit of B3, B4 = {} (returns)
   Liveness at exit of B2     = {a, b}    (children read them on disjoint paths)
   Liveness at exit of B1     = {a, b}    (cond is read, but assume already used)
   Liveness at entry of B1    = {}
```

**Used by**: register allocation (live ranges), DCE (uses ∅ ⇒ dead), copy
coalescing.

**Transfer**:
```
   IN[B]  = USE[B] ∪ (OUT[B] \ DEF[B])
```

## 3. Available expressions (FORWARD, MUST)

An expression `e` is **available** at p if every path to p computes e and no
operand of e is redefined after the computation.

```
   B1:  t = a + b     ← (a+b) generated
        x = t
                 ▼
   B2:                    ← (a+b) still available (no one wrote a or b)
        y = a + b         ← can be replaced by y = t (CSE!)
```

**Used by**: CSE / GVN.

**Transfer**:
```
   OUT[B] = (IN[B] ∪ GEN[B]) \ KILL[B]
   meet   = intersection                       (∩; the "MUST" direction)
   IN[entry] = ∅
```

## 4. Very-busy expressions (BACKWARD, MUST)

An expression `e` is **very busy** at p if every path from p computes e
before any operand is changed.

```
   B1:                      ◄── (a+b) very busy here
        if cond
       /         \
   B2:            B3:
   t = a + b      u = a + b   ◄── both children compute it
   ...            ...
```

If `(a+b)` is very busy at the entry of `B1`, you can **hoist** the
computation into B1 (code hoisting, a form of partial-redundancy
elimination).

**Used by**: PRE (LLVM `GVNHoistPass`, GCC `tree-ssa-pre`), code hoisting.

## 5. Dominators

Block A **dominates** block B if every path from entry to B passes through A.

```
                Entry
                  │
                  ▼
                ┌─A─┐
                │   │
              ┌─┴─┐ │
              ▼   ▼ │
              B   C │
              │   │ │
              └─┬─┘ │
                ▼   │
                D ◄─┘

   Dominators:
     A dom A,B,C,D
     B dom B
     C dom C
     D dom D
     (Entry dominates everything)
```

The **immediate dominator** idom(B) is the closest dominator other than B.
The **dominator tree** has every node's parent = idom.

```
   Dominator tree of the CFG above:
                Entry
                  │
                  A
                ┌─┼─┐
                B C D
```

**Used by**:
- SSA construction (φ-placement uses *dominance frontier*)
- LICM (only hoist out of a loop into its preheader — the preheader
  dominates the header)
- Loop detection (a back-edge is one whose target dominates the source)
- Speculative execution (a side-effect can be hoisted iff its new location
  dominates all its former uses)

**Post-dominators** are the dual: A *post-dominates* B if every path from B
to exit passes through A. Used by code sinking.

## 6. The lattice picture

Every analysis is a lattice and a monotone transfer function. The lattice
encodes "how much do we know?":

```
       ⊤   ← "we know nothing yet" (top; many possibilities)
       │
   ┌───┼───┐
   │   │   │
   x   y   z   ← discrete facts
   │   │   │
   └─┬─┴─┬─┘
     │   │
     ┴   ⊥   ← "contradiction / completely undefined"
```

For **constant propagation**:

```
                  ⊤        ← "no value yet known"
              ╱       ╲
            -1   0   1  2  3 …    ← concrete constants
              ╲       ╱
                  ⊥        ← "not a constant" (could be anything)
```

The meet operator on this lattice is:
- meet(⊤, x) = x
- meet(x, x) = x
- meet(x, y) = ⊥ if x ≠ y, where ⊤ ≠ x ≠ ⊥

This is why SCCP is "sparse" and "conditional": it walks SSA edges, not the
entire CFG, and it tracks which CFG edges are *executable* — making it more
precise than naïve constant propagation.

## 7. How a transform consumes an analysis

Concrete example — LICM in three steps:

```
   ┌──────────────────┐
   │ 1. compute LoopInfo:
   │    "this is loop L, preheader P, header H, latch X"
   ├──────────────────┤
   │ 2. compute Dominators:
   │    "instruction I dominates all uses of I within L"
   ├──────────────────┤
   │ 3. compute AliasAnalysis:
   │    "the load at I does not alias any store inside L"
   ├──────────────────┤
   │ 4. for each I in loop body that is loop-invariant and safe to speculate:
   │       move I to preheader P
   └──────────────────┘
```

If any of analyses 1/2/3 are missing or imprecise, the transform is too
conservative and the optimization doesn't fire.

## 8. Building & inspecting

Each example here is annotated with the **exact** GEN/KILL/IN/OUT sets you'd
write on a whiteboard during a compilers exam.

```bash
make all                      # build object files
make asm                      # asm dump per file
clang -O0 -Xclang -disable-O0-optnone -S -emit-llvm 01_reaching_definitions.c \
    -o 01_reaching_definitions.O0.ll
```

➡ Next: [`05_control_flow/`](../05_control_flow/).
