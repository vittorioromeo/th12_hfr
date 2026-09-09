/* ------------------------------------------------------------------ game speed handling */
static inline void set_factor(float f) {
    g_factor = f;
    G_GAME_SPEED = g_logical * f;
}

/* stubs for the game's writes to the speed global */
float g_fpu_tmp __attribute__((used));
/* sites that store the literal 1.0 permanently (new logical speed 1.0) */
void __cdecl __attribute__((used)) speed_set_one_perm(void) { g_logical = 1.0f; G_GAME_SPEED = g_factor; }
/* sites that store 1.0 temporarily (restored later from a saved effective value) */
void __cdecl __attribute__((used)) speed_set_one_temp(void) { G_GAME_SPEED = g_factor; }
/* ECL ins_447: value on the FPU stack */
void __cdecl __attribute__((used)) speed_set_ecl_c(void) { g_logical = g_fpu_tmp; G_GAME_SPEED = g_logical * g_factor; }
/* pause: save + set 1.0 */
void __cdecl __attribute__((used)) speed_pause_set_c(void) { g_pause_shadow = g_logical; g_logical = 1.0f; G_GAME_SPEED = g_factor; }
/* pause: restore */
void __cdecl __attribute__((used)) speed_pause_restore_c(void) { g_logical = g_pause_shadow; G_GAME_SPEED = g_logical * g_factor; }

/* naked asm trampolines: the patched instruction is "fstp dword [g_game->addr.speed]" (6 bytes) -> call stub (5) + nop */
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _stub_set_one_perm\n_stub_set_one_perm:\n"
    "  fstp dword ptr [_g_fpu_tmp]\n  pushad\n  call _speed_set_one_perm\n  popad\n  ret\n"
    ".globl _stub_set_one_temp\n_stub_set_one_temp:\n"
    "  fstp dword ptr [_g_fpu_tmp]\n  pushad\n  call _speed_set_one_temp\n  popad\n  ret\n"
    ".globl _stub_set_ecl\n_stub_set_ecl:\n"
    "  fstp dword ptr [_g_fpu_tmp]\n  pushad\n  call _speed_set_ecl_c\n  popad\n  ret\n"
    ".globl _stub_pause_set\n_stub_pause_set:\n"
    "  fstp dword ptr [_g_fpu_tmp]\n  pushad\n  call _speed_pause_set_c\n  popad\n  ret\n"
    ".globl _stub_pause_restore\n_stub_pause_restore:\n"
    "  fstp dword ptr [_g_fpu_tmp]\n  pushad\n  call _speed_pause_restore_c\n  popad\n  ret\n"
    ".att_syntax\n"
);
extern void stub_set_one_perm(void), stub_set_one_temp(void), stub_set_ecl(void), stub_pause_set(void), stub_pause_restore(void);
