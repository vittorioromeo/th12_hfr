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

typedef int (__stdcall *FrameFn)(void* ctx);
#define orig_frame_vsync ((FrameFn)g_game->addr.frame_fn)
