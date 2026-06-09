# 12 — LLVM Specifics

The same reference treatment for the LLVM/Clang side.

## 1. The pipeline

```
   source.{c,cpp}
        │
        ▼  Clang front-end (AST, sema)
   LLVM IR (text .ll  /  bitcode .bc)
        │
        ▼  opt (mid-end pass manager)
   LLVM IR (optimized)
        │
        ▼  llc (backend)
        │      ─ ISel (SelectionDAG / GlobalISel)
        │      ─ machine IR (MIR)
        │      ─ register allocator (Greedy or Fast)
        │      ─ scheduler
        │      ─ asm printer
        ▼
   object file
```

Every textbook LLVM pipeline is also pieceable on the command line:

```bash
# 1. Front-end only: produce IR.
clang -S -emit-llvm -O0 -Xclang -disable-O0-optnone file.c -o file.ll

# 2. Mid-end only: run optimizations on IR.
opt -O3 file.ll -S -o file.O3.ll

# 3. Backend only: IR → asm.
llc -O3 file.O3.ll -o file.s
```

## 2. The opt tool

```bash
opt --help                 # human-readable list of all passes
opt -print-passes          # pipeline strings: e.g. "default<O3>"
opt -p='function(simplifycfg)' file.ll -S         # run a single pass
opt -p='default<O3>' file.ll -S                   # the full -O3 pipeline
opt -p='function(loop(loop-rotate,licm))' …       # nested pipeline syntax
opt -print-after-all  file.ll -S 1>/dev/null      # IR after every pass
opt -print-after=licm file.ll -S 1>/dev/null      # IR after LICM only
opt -debug-pass-manager file.ll -S 1>/dev/null    # which passes ran in order
```

## 3. The Clang side of the same knobs

```bash
clang -O3 -mllvm -print-after-all  -c file.c -o /dev/null
clang -O3 -mllvm -debug-pass-manager -c file.c -o /dev/null
clang -O3 -Rpass=loop-vectorize     -c file.c -o /dev/null    # what fired
clang -O3 -Rpass-missed=loop-vectorize \
          -Rpass-analysis=loop-vectorize ...                  # what didn't, and why
clang -O3 -fsave-optimization-record -c file.c -o /dev/null   # YAML opt remarks
```

The YAML remarks pair nicely with
[opt-viewer](https://github.com/llvm/llvm-project/tree/main/llvm/tools/opt-viewer)
which produces an HTML annotated source listing.

## 4. The (new) pass manager

LLVM 13+ defaults to the *new* pass manager (`-fexperimental-new-pass-manager`
is no longer needed). The pipeline is a *string*:

```
default<O3>
  module(annotation2metadata,
         forceattrs,
         inferattrs,
         coro-early,
         function<eager-inv>(lower-expect, simplifycfg, sroa, early-cse),
         ipsccp,
         called-value-propagation,
         globalopt,
         function<eager-inv>(mem2reg, instcombine,
                              loop(loop-rotate, licm),
                              vectorize, instcombine, simplifycfg),
         ... )
```

You can supply your own pipeline:

```bash
opt -p='module(inline,function(sroa,early-cse,licm))' file.ll -S
```

Or attach it to Clang:

```bash
clang -O0 -mllvm -passes='function(sroa,licm)' -c file.c -o /dev/null
```

## 5. Function & parameter attributes

(See chapter 10 for a longer table.)

```
function:   nounwind   readnone   readonly   willreturn   mustprogress
            norecurse  noreturn   cold       hot          alwaysinline
            optnone    speculatable

parameter:  noalias    nocapture  nonnull    dereferenceable(N)
            readonly   writeonly  returned   align(N)
```

Examples in C with `__attribute__`:

```c
__attribute__((const))     int strlen_like(const char *);     // readnone-ish
__attribute__((pure))      int hashfn(const char *);          // readonly
__attribute__((malloc))    void *xmalloc(unsigned);           // ret noalias
__attribute__((nonnull))   int  use(const void *p);           // p nonnull
```

LLVM-specific attribute spellings (less common in user code):

```c
__attribute__((annotate("noalias")))
__attribute__((noescape))     /* parameter cannot escape */
__attribute__((preserve_most))  /* ABI: caller-saves most regs */
__attribute__((preserve_all))   /* ABI: caller-saves ALL regs */
```

## 6. Builtins worth knowing (Clang/LLVM)

```c
__builtin_expect(x, 0)
__builtin_expect_with_probability(x, 0, 0.01)
__builtin_unreachable()
__builtin_assume(cond)                  /* same as if (!cond) __builtin_unreachable() */
__builtin_assume_aligned(p, 64)
__builtin_constant_p(x)
__builtin_object_size(p, type)
__builtin_prefetch(p, rw, locality)
__builtin_offsetof(T, member)
__builtin_clz / ctz / popcount / parity (and ll versions)
__builtin_bswap{16,32,64}
__builtin_add_overflow / mul_overflow / sub_overflow
__builtin_dynamic_object_size(p, type)  /* for hardened libc */
__builtin_LINE() / __builtin_FILE() / __builtin_FUNCTION()
```

`__builtin_assume(x > 0)` is the dual of `__builtin_unreachable()`. The
compiler uses it for VRP, then drops the assumption.

## 7. Debugging missed optimizations

Recipe:

1. Reproduce on the simplest possible C file.
2. Dump IR at -O0 and -O3:
   ```bash
   clang -O0 -Xclang -disable-O0-optnone -S -emit-llvm file.c -o file.O0.ll
   opt -O3 file.O0.ll -S -o file.O3.ll
   diff -u file.O0.ll file.O3.ll
   ```
3. Find which pass *should* have fired:
   ```bash
   opt -O3 -debug-pass-manager file.O0.ll -S -o /dev/null 2>&1 | less
   ```
4. Run just that pass and confirm it didn't fire:
   ```bash
   opt -p='function(licm)' file.O0.ll -S
   ```
5. Often the answer is an attribute the front-end could have emitted but
   didn't (`restrict` → `noalias`, `__attribute__((pure))` → `readonly`).

## 8. Reading LLVM IR fluently

```llvm
; Function attributes
define dso_local noundef i32 @add(i32 noundef %a, i32 noundef %b) #0 {
entry:
  %r = add nsw i32 %a, %b           ; nsw = no signed wrap
  ret i32 %r
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind
                  willreturn memory(none) uwtable }
```

Tags to recognize:

- `nsw` / `nuw`  — no signed / unsigned wrap (UB on overflow → enables
  reassoc & strength-reduce).
- `inbounds`     — on `getelementptr`; the pointer arithmetic stays within
  an allocated object.
- `align N`      — operand alignment.
- `!tbaa !N`     — Type-Based Alias Analysis metadata.
- `!noalias`     — region-scoped noalias (vector-vectorizer memcheck).
- `tail`         — on `call`; allowed to TCE.

➡ Next: [`13_practical/`](../13_practical/).
