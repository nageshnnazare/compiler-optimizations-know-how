/* 06_block_placement.c — HOT / COLD CODE LAYOUT
 * ============================================================================
 *
 * Idea
 * ----
 *   The backend reorders basic blocks so the *hot* path is straight-line
 *   fall-through (no taken branches → no I-cache miss for the next BB).
 *
 *     BEFORE (source order)             AFTER (machine layout)
 *     ──────                            ─────
 *      if (rare)                          ; hot path lives here
 *          cold_path();                   if (!rare) goto hot;
 *      hot_path();                        cold_path();
 *                                         jmp end;
 *                                       hot:
 *                                         hot_path();
 *                                       end:
 *
 *   Source ordering does NOT determine machine ordering: the backend
 *   uses branch-probability estimates (`__builtin_expect`, PGO data,
 *   attribute hints) to pick the layout.
 *
 * Pass names
 * ----------
 *   LLVM: MachineBlockPlacement.
 *   GCC : bb-reorder.cc (-freorder-blocks).
 *
 * Knobs
 * -----
 *   __builtin_expect(x, EXPECTED)        per-branch
 *   __attribute__((cold))                whole function rarely called
 *   __attribute__((hot))                  whole function often called
 *   PGO via -fprofile-generate / -fprofile-use
 * ============================================================================
 */
extern void hot_path(int);
extern void cold_path(int);

/*  hint_with_builtin — "rare" branch goes off the fall-through path.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      hint_with_builtin:
 *          cmp  edi, 57005           ; 0xDEAD = 57005
 *          jne  hot_path             ; TAILCALL on the COMMON (predicted) case
 *          push rbx
 *          mov  ebx, edi             ; preserve x across cold_path()
 *          mov  edi, 57005
 *          call cold_path
 *          mov  edi, ebx
 *          pop  rbx
 *          jmp  hot_path             ; TAILCALL after cold work
 *
 *  WHAT TO NOTICE:
 *    1. The COMMON case (`!= 0xDEAD`) is a single `cmp + jne` → no
 *       taken branch (the predicted prediction is "not taken" for jne
 *       when the hint says the equality is unlikely).
 *    2. The RARE case is laid out off the fall-through; reaching it
 *       requires a TAKEN branch.
 *
 *  WHY: __builtin_expect(cond, 0) hints "cond is unlikely". The
 *  branch-probability annotator assigns 1/2000 weight to the true edge.
 *  MachineBlockPlacement then orders blocks so the FALSE arm (hot) is
 *  fall-through. Modern branch predictors do well with static hints on
 *  cold paths.
 */
void hint_with_builtin(int x) {
    if (__builtin_expect(x == 0xDEAD, 0))
        cold_path(x);
    hot_path(x);
}

/*  hint_with_attribute — branch hint via __builtin_expect, MIRRORED.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      hint_with_attribute:
 *          cmp  edi, 48879           ; 0xBEEF
 *          jne  cold_path            ; TAILCALL on the UNLIKELY case
 *          mov  edi, 48879
 *          jmp  hot_path             ; TAILCALL on the LIKELY case
 *
 *  Symmetric to the previous example, but here the equality is the
 *  likely path → fall-through is the LIKELY branch.
 */
void hint_with_attribute(int x) {
    if (__builtin_expect(x == 0xBEEF, 1)) {
        hot_path(x);
    } else {
        cold_path(x);
    }
}

/*  error_handler — `cold` attribute: entire function placed in cold section.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      error_handler:
 *          jmp  cold_path            ; tail-call to cold helper
 *
 *  WHY: `cold` attribute does THREE things:
 *    1. Places the function in `.text.unlikely.error_handler` (a
 *       separate, cold I-cache region).
 *    2. Tells the inliner to never inline this function INTO hot code.
 *    3. Tells the register allocator to prefer callee-saved registers
 *       (no need to optimize for fast call/return when calls are rare).
 */
__attribute__((cold)) void error_handler(int code) {
    cold_path(code);
}

/*  mainloop — `hot` attribute: kept small and contiguous in the hot section.
 *  ──────────────────────────────────────────────────────────────────────────
 *  ACTUAL (-O3):
 *      mainloop:
 *          jmp  hot_path
 *
 *  WHY: `hot` inverts the cold heuristics. The function is placed in
 *  the hot section (.text.hot.mainloop), the inliner is encouraged to
 *  inline INTO it (treating it as a frequently-executed callee), and
 *  the allocator prefers caller-saved scratch (cheaper calls).
 */
__attribute__((hot)) void mainloop(int x) {
    hot_path(x);
}
