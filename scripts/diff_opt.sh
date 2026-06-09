#!/usr/bin/env bash
# diff_opt.sh — produce side-by-side assembly for two optimization levels.
#
# Usage:
#   ./diff_opt.sh path/to/file.c -O0 -O3
#   CC=clang ./diff_opt.sh path/to/file.c -O0 -O2
#
# Output goes to:
#   <stem>.<level>.s   (one per level)
#   <stem>.diff        (unified diff, primary review artifact)
set -euo pipefail

CC="${CC:-cc}"
SRC="${1:?usage: $0 file.c -OX -OY}"; shift
LVL_A="${1:?need first opt level (e.g. -O0)}"; shift
LVL_B="${1:?need second opt level (e.g. -O3)}"; shift || true

stem="${SRC%.c}"
common_flags=(-S -masm=intel -fno-asynchronous-unwind-tables \
              -fno-exceptions -fno-stack-protector -fverbose-asm \
              -fomit-frame-pointer)

# Some platforms (macOS clang) reject -masm=intel; fall back gracefully.
if ! "$CC" "${common_flags[@]}" "$LVL_A" "$SRC" -o "${stem}.${LVL_A#-}.s" 2>/dev/null; then
    common_flags=(-S -fno-asynchronous-unwind-tables -fno-exceptions \
                  -fno-stack-protector -fverbose-asm -fomit-frame-pointer)
    "$CC" "${common_flags[@]}" "$LVL_A" "$SRC" -o "${stem}.${LVL_A#-}.s"
fi
"$CC" "${common_flags[@]}" "$LVL_B" "$SRC" -o "${stem}.${LVL_B#-}.s"

diff -u "${stem}.${LVL_A#-}.s" "${stem}.${LVL_B#-}.s" > "${stem}.diff" || true
echo "wrote ${stem}.${LVL_A#-}.s, ${stem}.${LVL_B#-}.s, ${stem}.diff"
