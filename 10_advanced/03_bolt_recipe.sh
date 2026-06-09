#!/usr/bin/env bash
# 03_bolt_recipe.sh — end-to-end BOLT recipe (Linux/x86-64 only)
#
# Requires: llvm-bolt, perf2bolt, perf (root for kernel events).
# Apply to ANY ELF binary built with -Wl,--emit-relocs.

set -euo pipefail
BIN="${1:?usage: $0 path/to/binary [args...]}"; shift || true

# 1. Build the target with relocations preserved.
echo "==> ensure the binary was linked with -Wl,--emit-relocs"

# 2. Collect a perf profile while exercising real workload.
perf record -e cycles:u -j any,u -o perf.data -- "$BIN" "$@"

# 3. Convert to BOLT's profile format.
perf2bolt -p perf.data -o app.fdata "$BIN"

# 4. Rewrite the binary.
llvm-bolt "$BIN" \
    -o "${BIN}.bolt" \
    -data=app.fdata \
    -reorder-blocks=ext-tsp \
    -reorder-functions=hfsort+ \
    -split-functions \
    -split-all-cold \
    -split-eh \
    -dyno-stats
echo "wrote ${BIN}.bolt"
