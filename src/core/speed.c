/* ------------------------------------------------------------------ game speed handling */
/* The game speed the simulation reads. A profile that has not described where it lives has
   nothing to scale and nothing that can be sub-stepped, so this is where that degrades: the
   factor is still tracked, and the write -- through a null pointer -- is not made. Leaving it
   unguarded is a page fault on the first tick of a half-described game, which is what TH14 hit
   the first time it ran. */
static inline void set_factor(float f) {
    g_factor = f;
    if (!g_game->addr.speed) return;
    G_GAME_SPEED = g_logical * f;
}

/* stubs for the game's writes to the speed global. The value the overwritten instruction was
   going to store, captured by the stub from wherever that instruction had it -- the x87 stack
   on the older games, an XMM register on TH14. Operations that already know the value (the
   sites that store the literal 1.0) ignore it. */
float g_fpu_tmp __attribute__((used));
/* sites that store the literal 1.0 permanently (new logical speed 1.0) */
void __cdecl __attribute__((used)) speed_set_one_perm(void) { g_logical = 1.0f; G_GAME_SPEED = g_factor; }
/* sites that store 1.0 temporarily (restored later from a saved effective value) */
void __cdecl __attribute__((used)) speed_set_one_temp(void) { G_GAME_SPEED = g_factor; }
/* the ECL instruction that sets the game speed: the new logical speed is the captured value */
void __cdecl __attribute__((used)) speed_set_ecl_c(void) { g_logical = g_fpu_tmp; G_GAME_SPEED = g_logical * g_factor; }
/* pause: save + set 1.0 */
void __cdecl __attribute__((used)) speed_pause_set_c(void) { g_pause_shadow = g_logical; g_logical = 1.0f; G_GAME_SPEED = g_factor; }
/* pause: restore */
void __cdecl __attribute__((used)) speed_pause_restore_c(void) { g_logical = g_pause_shadow; G_GAME_SPEED = g_logical * g_factor; }
