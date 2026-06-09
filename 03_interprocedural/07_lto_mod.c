/* 07_lto_mod.c — caller of `add` from 07_lto.c.
 *
 * See 07_lto.c for the full LTO walkthrough.
 *
 * Quick check (after building with the recipe in 07_lto.c):
 *
 *   Without -flto:
 *     callee_no_lto:
 *       mov   edi, 1
 *       mov   esi, 2
 *       jmp   _add               ; tail call to add survives across TU
 *
 *   With -flto:
 *     callee_no_lto:
 *       mov   eax, 3              ; whole call vanished; constants folded
 *       ret
 */
extern int add(int, int);

int callee_no_lto(void) {
    return add(1, 2);
}
