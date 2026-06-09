# 08 — Vectorization

> "If your hot loop isn't vectorized, you're leaving 4×, 8×, or 16× of the
> machine on the table."

Vectorization replaces a *scalar* loop iteration with a *SIMD* (Single
Instruction Multiple Data) iteration that processes several elements at
once.

## Map

| #  | Example                              | Topic                                  |
| -- | ------------------------------------ | -------------------------------------- |
| 01 | `01_loop_vec_basic.c`                | The textbook a[i] = b[i] + c[i]        |
| 02 | `02_reduction.c`                     | Reductions (sum, min, dot product)     |
| 03 | `03_slp.c`                           | Straight-line (SLP) vectorization      |
| 04 | `04_masked.c`                        | Masked vectorization (if inside loop)  |
| 05 | `05_alignment.c`                     | Alignment, peeling, runtime checks     |
| 06 | `06_blockers.c`                      | What kills vectorization               |
| 07 | `07_intrinsics.c`                    | Hand-written intrinsics & __builtin    |
| 08 | `08_pragmas.c`                       | `#pragma omp simd`, ivdep, vectorize   |

## Why SIMD matters

```
   Scalar: 8 iterations × 1 element/iter      = 8 ops
   AVX2  : 1 iteration  × 8 elements/iter     = 1 op
   AVX-512: 1 iteration × 16 elements/iter    = 1 op  (half-width float)
```

Real-world speedups are usually less because of memory bandwidth and tail
work, but a vectorized loop of `int` is typically 4–8× faster than the
scalar version.

## The two vectorizers

```
   ┌─────────────────────────────────────────────────────────┐
   │                Loop Vectorizer                          │
   │  works on whole loops:                                  │
   │  for (i=0;i<n;i++) a[i] = b[i] + c[i];                  │
   │                                                         │
   │  generates:                                             │
   │    main vector loop  (i += VF)                          │
   │    optional remainder / scalar tail loop                │
   │    runtime memcheck (alias) if needed                   │
   │    runtime alignment peel if profitable                 │
   └─────────────────────────────────────────────────────────┘

   ┌─────────────────────────────────────────────────────────┐
   │                SLP Vectorizer                           │
   │  works on straight-line code (no loop):                 │
   │  a[0]=b[0]+c[0]; a[1]=b[1]+c[1]; a[2]=b[2]+c[2]; ...    │
   │  finds the implicit "tree" of identical operations on   │
   │  adjacent lanes and bundles them into one SIMD op.      │
   └─────────────────────────────────────────────────────────┘
```

LLVM passes: `LoopVectorizePass`, `SLPVectorizerPass`.
GCC passes: `tree-vect.cc` (loop) + `tree-slp.cc` (SLP).

## The anatomy of a vectorized loop

```
                      ┌───────────────────────┐
                      │ memcheck (alias)      │  ◄── skipped if `restrict` /
                      │   if overlap → scalar │      no-alias proven
                      └──────────┬────────────┘
                                 │
                      ┌──────────▼────────────┐
                      │ peel until aligned    │  ◄── e.g. 0..3 scalar
                      │   (vectorizer-only)   │      iterations
                      └──────────┬────────────┘
                                 │
                ┌────────────────▼─────────────────┐
                │ vector main loop                 │  ◄── stride = VF (e.g. 8)
                │   load %v_b = vload b[i:i+VF]    │
                │   load %v_c = vload c[i:i+VF]    │
                │   %v_a       = %v_b + %v_c       │
                │   vstore a[i:i+VF] = %v_a        │
                │   i += VF; if i+VF <= n: continue│
                └────────────────┬─────────────────┘
                                 │
                      ┌──────────▼────────────┐
                      │ scalar tail           │  ◄── i..n
                      │   for (; i<n; i++) …  │
                      └───────────────────────┘
```

VF = "vectorization factor" = number of lanes per vector (4 for `int32` on
SSE, 8 on AVX2, 16 on AVX-512).

## How to ASK the compiler to tell you what it did

```bash
# LLVM (Clang)
clang -O3 -Rpass=loop-vectorize \
          -Rpass-missed=loop-vectorize \
          -Rpass-analysis=loop-vectorize \
          -c 01_loop_vec_basic.c -o /dev/null

# GCC
gcc   -O3 -fopt-info-vec       -c 01_loop_vec_basic.c -o /dev/null
gcc   -O3 -fopt-info-vec-all   -c 01_loop_vec_basic.c -o /dev/null  # more verbose
gcc   -O3 -fopt-info-vec-missed -c 01_loop_vec_basic.c -o /dev/null
```

You will see messages like:
```
01_loop_vec_basic.c:8:5: remark: vectorized loop (vectorization width: 8,
  interleaved count: 4) [-Rpass=loop-vectorize]
```

## What kills vectorization (the "blockers" list)

```
   ✘ Loop-carried dependence
     for (i=1;i<n;i++) a[i] = a[i-1] + b[i];          // serial chain
   ✘ Function call inside the loop with unknown side effects
   ✘ Pointers that may alias (no restrict, no memcheck legality)
   ✘ Early exits / break inside the loop body
   ✘ Variable trip count or unknown stride that backend can't reason about
   ✘ Indirect memory (a[idx[i]] with unknown idx)  — needs gather/scatter
     ISA (AVX2 gather, NEON SVE) AND profitability above scalar
   ✘ Bit-precise integer types whose width isn't a multiple of VF lane size
   ✘ Reductions with non-associative operators (float without -ffast-math)
   ✘ Loop body too large (cost model gives up)
```

## Reductions

Naïve reduction:
```c
int s = 0;
for (int i=0;i<n;i++) s += a[i];
```

Vectorized:
```
   s = 0;
   v = (vector){0,0,0,0,0,0,0,0};
   for (i=0; i+7<n; i+=8) {
       v += vload a[i:i+8];
   }
   /* horizontal reduction of v into a single scalar */
   s = v[0]+v[1]+v[2]+v[3]+v[4]+v[5]+v[6]+v[7];
   /* scalar tail */
   for (; i<n; i++) s += a[i];
```

For *floating-point* reductions the compiler will NOT vectorize by default
because addition isn't associative in IEEE-754. Enable with
`-ffast-math` / `-fassociative-math` or annotate the reduction with
`#pragma omp simd reduction(+:s)`.

## Masking — vectorizing loops with `if`

```c
for (i=0;i<n;i++)
    if (a[i] > 0) b[i] = a[i] * 2;
```

```
   for (i=0; i+VF<=n; i+=VF) {
       va = vload a[i:i+VF];
       m  = va > 0;                 ; mask: per-lane bool
       r  = va * 2;
       vmasked_store b[i:i+VF] = r, mask=m
   }
```

AVX-512 and SVE have *first-class* mask registers, so this is direct. On
AVX2 the compiler may emulate the masked store with `vmaskmovps` or fall
back to a select + blended store.

## Hand-written intrinsics

When the compiler can't or won't, you can write vectors yourself. Two
options:

1. **GCC/Clang vector extensions**:
   ```c
   typedef int v8i __attribute__((vector_size(32)));   // 8×i32, AVX2 width
   v8i sum(v8i a, v8i b) { return a + b; }              // pure C, lowered to vpaddd
   ```
2. **ISA-specific intrinsics**:
   ```c
   #include <immintrin.h>
   __m256i s = _mm256_add_epi32(_mm256_loadu_si256(a),
                                 _mm256_loadu_si256(b));
   ```

For portable code prefer (1) or `std::experimental::simd` (C++).
Use (2) when you need exact instructions (e.g., `vpternlogd`, `vpdpbusd`).

## Pragmas

```c
// Clang / GCC: ask the loop vectorizer to *try harder*.
#pragma GCC ivdep
#pragma clang loop vectorize(enable) interleave(enable)

// OpenMP SIMD — the most portable annotation.
#pragma omp simd
for (i=0;i<n;i++) a[i] = b[i] + c[i];

// With reduction:
#pragma omp simd reduction(+:s)
for (i=0;i<n;i++) s += a[i];
```

`#pragma omp simd` is gold: it tells the compiler the loop has no
loop-carried dependencies, removing the legality barrier on its own.

## Aligning your data

Vectorized loads/stores are faster when aligned to the vector width.
You can request alignment for an allocation:

```c
__attribute__((aligned(64))) float a[1024];

int *p = aligned_alloc(64, n * sizeof(int));
```

And tell the compiler about the alignment of a pointer parameter:

```c
void f(int *p) {
    p = __builtin_assume_aligned(p, 64);
    /* now loops over p generate aligned vector ops */
}
```

➡ Next: [`09_register_allocation/`](../09_register_allocation/).
