#!/usr/bin/env bash
# dump_gcc_tree.sh — dump GCC GIMPLE/RTL intermediate representations.
#
# Usage:
#   ./dump_gcc_tree.sh path/to/file.c [extra-gcc-flags...]
#
# Produces a directory <stem>.gcc-dumps/ containing one file per pass.
# Also prints which optimizations were applied with -fopt-info.
set -euo pipefail

GCC="${GCC:-gcc}"
SRC="${1:?usage: $0 file.c [extra flags]}"; shift || true

stem="${SRC%.c}"
outdir="${stem}.gcc-dumps"
rm -rf "$outdir"
mkdir -p "$outdir"

"$GCC" -O3 -fdump-tree-all-graph -fdump-rtl-all \
       -fdump-ipa-all -fopt-info-all \
       -fno-asynchronous-unwind-tables \
       "$@" "$SRC" -o /dev/null 2> "${outdir}/opt-info.txt" || true

# Move dump artefacts into the output dir (gcc writes them next to the source).
mv "$(dirname "$SRC")"/$(basename "$SRC").* "$outdir"/ 2>/dev/null || true

echo "wrote $outdir/"
echo "review: head -n 20 $outdir/opt-info.txt"
