#!/usr/bin/env bash
# dump_llvm_ir.sh — produce LLVM IR snapshots before and after the optimizer.
#
# Usage:
#   ./dump_llvm_ir.sh path/to/file.c [extra-clang-flags...]
#
# Produces:
#   <stem>.O0.ll        # textual IR, no optimization, suitable for `opt`
#   <stem>.O3.ll        # final IR after the new pass manager
#   <stem>.passes.txt   # which passes ran and in what order
set -euo pipefail

CLANG="${CLANG:-clang}"
OPT="${OPT:-opt}"
SRC="${1:?usage: $0 file.c [extra flags]}"; shift || true

stem="${SRC%.c}"

"$CLANG" -O0 -Xclang -disable-O0-optnone -S -emit-llvm "$@" \
         "$SRC" -o "${stem}.O0.ll"

"$CLANG" -O3 -S -emit-llvm "$@" \
         "$SRC" -o "${stem}.O3.ll"

# print which passes ran (works on LLVM ≥ 13 with the new PM)
"$CLANG" -O3 -fdebug-pass-manager -c "$@" "$SRC" -o /dev/null \
         2> "${stem}.passes.txt" || true

echo "wrote ${stem}.O0.ll, ${stem}.O3.ll, ${stem}.passes.txt"
