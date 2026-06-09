/* 07_loop_rotation.c — LOOP ROTATION
 * ============================================================================
 *
 * Idea
 * ----
 *   Rotate `while (cond) { body; }` into `if (cond) do { body; } while (cond);`.
 *
 *   BEFORE (while-form; test-at-top)         AFTER (do-while + guard)
 *   ─────────────────────────────────       ─────────────────────────
 *   header:                                  if (!cond) goto exit;
 *      if (!cond) goto exit                  do {
 *      body                                      body
 *      goto header                           } while (cond);
 *   exit:                                    exit:
 *
 * Benefits
 * --------
 *   – ONE branch per iteration instead of two (loop body falls through
 *     to the comparison and conditionally jumps back).
 *   – Canonical CFG shape required by LICM, IVOPT, LoopVectorize, etc.
 *   – Better branch prediction on the back-edge: a "taken" branch
 *     to the loop start every iteration, "not-taken" only once on exit.
 *
 * Pass names
 * ----------
 *   LLVM:  LoopRotatePass.
 *   GCC :  -ftree-loop-rotate (default at -O3).
 * ============================================================================
 */

/*  rotateable — straight while-loop; rotated then vectorized.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      rotateable:
 *          test esi, esi
 *          jle  .exit                ; GUARD: if n <= 0 → skip entire loop
 *          mov  edx, esi
 *          mov  eax, edx
 *          and  eax, 3               ; tail = n % 4
 *          cmp  esi, 4
 *          jae  .vec_main            ; n >= 4 → use unrolled main
 *          xor  ecx, ecx
 *          jmp  .scalar_tail
 *      .vec_main:                    ; 4-way UNROLLED loop body
 *          and  edx, ~3
 *          xor  ecx, ecx
 *      .vec_body:
 *          mov  esi, ecx
 *          imul esi, ecx             ; (i)^2
 *          mov  [rdi + 4*rcx], esi
 *          lea  esi, [rcx + 1]
 *          imul esi, esi             ; (i+1)^2
 *          mov  [rdi + 4*rcx + 4], esi
 *          lea  esi, [rcx + 2]
 *          imul esi, esi             ; (i+2)^2
 *          mov  [rdi + 4*rcx + 8], esi
 *          lea  esi, [rcx + 3]
 *          imul esi, esi             ; (i+3)^2
 *          mov  [rdi + 4*rcx + 12], esi
 *          add  rcx, 4
 *          cmp  rdx, rcx
 *          jne  .vec_body
 *          ; ... scalar tail for n % 4 ...
 *      .exit:
 *          ret
 *
 *  THE ROTATION IS VISIBLE: the GUARD `test esi, esi ; jle .exit` is
 *  the if at the top, and the loop body falls through to `jne .vec_body`
 *  at the bottom — back-edge as a conditional jump backwards. ONE
 *  comparison per iteration instead of the test-at-top while-loop's
 *  two.
 *
 *  Without rotation the vectorizer would refuse to unroll (it needs the
 *  do-while shape) and we'd get a plain scalar loop.
 */
void rotateable(int *a, int n) {
    int i = 0;
    while (i < n) {
        a[i] = i * i;
        i++;
    }
}
