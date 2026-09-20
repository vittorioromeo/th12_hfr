/* ------------------------------------------------------------------ game symbols */
#define G_GAME_SPEED      (*(volatile float*)g_game->addr.speed)
#define G_UPDATE_RUNNER   (*(uint8_t**)g_game->addr.update_runner)
#define G_D3D_DEVICE      (*(IDirect3DDevice9**)g_game->addr.device)
#define G_PP              ((D3DPRESENT_PARAMETERS*)g_game->addr.pp)
#define G_WINDOW_FLAGS    (*(uint32_t*)g_game->addr.window_flags)
#define G_FRAME_TIME_DBL  (*(double*)g_game->addr.frame_time)
#define G_MISC_FLAGS      (*(uint32_t*)g_game->addr.misc_flags)
#define G_INPUT_CUR       (*(uint32_t*)g_game->addr.raw_input)
#define G_INPUT_PRESSED   (*(uint32_t*)g_game->addr.raw_pressed)
#define G_REPLAY_MANAGER  (*(uint8_t**)g_game->addr.replay_manager) /* mode at +0x10; frame/stage offsets in profile */

/* The replay manager's mode word: 1 while a replay is playing back. */
#define REPLAY_MODE(rm) (*(int*)((rm) + (g_game->layout.replay_mode ? g_game->layout.replay_mode : 0x10)))

typedef int (__stdcall *FrameFn)(void* ctx);
/* Calls into the game with `this` in ECX (TH14 on), written out rather than expressed as a cast
   to a thiscall/fastcall pointer type. GCC merges two calls through the same pointer whose
   types differ only in the calling-convention attribute: `ecx ? ((FnEcx)p)(ctx) : ((Fn)p)(ctx)`
   was compiled as the stdcall call alone once a third branch was added beside it, and TH14's
   frame function ran with whatever was left in ECX -- a crash on its first frame. An explicit
   register operand cannot be merged away. */
static inline int call_this0(uintptr_t fn, void* self) {
    int r; void* c = self;
    __asm__ volatile ("call *%2" : "=a"(r), "+c"(c) : "r"(fn) : "edx", "memory", "cc");
    return r;
}
/* ... and with one stack argument, which the callee pops. */
static inline int call_this1(uintptr_t fn, void* self, void* arg) {
    int r; void* c = self;
    __asm__ volatile ("push %2\n\tcall *%3" : "=a"(r), "+c"(c) : "r"(arg), "r"(fn) : "edx", "memory", "cc");
    return r;
}
static inline int frame_call_original(void* ctx) {
    if (g_game->frame_original) return g_game->frame_original(ctx);
    if (g_game->frame_ctx_ecx) return call_this0(g_game->addr.frame_fn, ctx);
    return ((FrameFn)g_game->addr.frame_fn)(ctx);
}
