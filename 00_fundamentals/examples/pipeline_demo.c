/* pipeline_demo.c
 *
 * Tiny program used as the running example for chapter 00.
 *
 * Inspect each stage:
 *   clang -Xclang -ast-dump -fsyntax-only pipeline_demo.c   # AST
 *   clang -O0 -Xclang -disable-O0-optnone -S -emit-llvm pipeline_demo.c \
 *         -o pipeline_demo.O0.ll                            # unoptimized IR
 *   clang -O3                              -S -emit-llvm pipeline_demo.c \
 *         -o pipeline_demo.O3.ll                            # optimized IR
 *   clang -O3 -S -masm=intel               pipeline_demo.c \
 *         -o pipeline_demo.O3.s                             # final asm
 */
#include <stdint.h>

int32_t classify(int32_t x) {
    int32_t y = x + 1;
    if (y > 0)
        y = y * 2;
    else
        y = -y;
    return y;
}
