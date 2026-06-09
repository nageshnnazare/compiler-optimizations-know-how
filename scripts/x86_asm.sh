#!/usr/bin/env bash
# x86_asm.sh — produce x86-64 Intel-syntax assembly for any .c file.
#
# Works on any host where clang has the x86_64 backend (the default LLVM
# install). The output is the canonical 'NASM-style' Intel syntax we use
# in inline comments throughout the guide.
#
# Usage:
#   ./x86_asm.sh <file.c> [-O0|-O1|-O2|-O3] [extra clang flags]
set -euo pipefail
SRC="${1:?usage: $0 file.c [-OX] [extra...]}"; shift
LVL="${1:--O3}"; shift || true

# `--target=x86_64-apple-macos` uses the macOS SDK headers, so libc
# includes (string.h, stdio.h, ...) resolve. `--target=x86_64-pc-linux-gnu`
# is more portable but on macOS doesn't see system headers.
TARGET="${X86_TARGET:-x86_64-apple-macos}"

clang --target="$TARGET" \
      -fno-asynchronous-unwind-tables -fomit-frame-pointer \
      -masm=intel -S "$LVL" "$@" "$SRC" -o -
