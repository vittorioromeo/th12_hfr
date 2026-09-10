/* ------------------------------------------------------------------ sub-tick input
 * The game polls the keyboard/joystick once per frame (Supervisor node, priority 1) into the raw input state at
 * g_game->addr.raw_input (0x130 bytes: cur, prev, repeat, pressed, released, 32 hold counters, held mask); the replay node
 * (priority 0xb) copies it into the game input word g_game->addr.game_input and records it. Between frame ticks we poll again
 * with the game's own routine (so key/joystick mapping is unchanged) and feed only the movement and focus bits to
 * the game input word, which the (sub-stepped) player reads. Menus, shots, bombs and the game's replay record
 * keep the frame-sampled input. The per-tick bits are stored in memory per stage and written to the replay as an
 * extra USER chunk, and applied on playback.
 */
#define G_INPUT_RAW       ((uint8_t*)g_game->addr.raw_input)
#define G_INPUT_RAW_SIZE  0x130
#define G_GAME_INPUT      (*(uint32_t*)g_game->addr.game_input)
#define G_OPTION_FLAGS    (*(uint32_t*)g_game->addr.option_flags)
#define G_AUTOFOCUS_CTR   (*(int*)g_game->addr.autofocus)
#define IN_FOCUS 0x08
#define IN_MOVE  0xf0

/* joystick: winmm's joyGetPosEx can be slow; between frames return the last polled state */
typedef MMRESULT (WINAPI *JoyGetPosExFn)(UINT, LPJOYINFOEX);
static JoyGetPosExFn orig_joyGetPosEx;
static JOYINFOEX g_joy_cache; static MMRESULT g_joy_cache_res; static int g_joy_cache_valid, g_joy_use_cache;
static MMRESULT WINAPI hook_joyGetPosEx(UINT id, LPJOYINFOEX ji) {
    if (!ji) return orig_joyGetPosEx(id, ji);
    DWORD n = ji->dwSize < sizeof g_joy_cache ? ji->dwSize : (DWORD)sizeof g_joy_cache;
    if (g_joy_use_cache && id == 0 && g_joy_cache_valid) { memcpy(ji, &g_joy_cache, n); return g_joy_cache_res; }
    MMRESULT r = orig_joyGetPosEx(id, ji);
    if (id == 0) { memcpy(&g_joy_cache, ji, n); g_joy_cache_res = r; g_joy_cache_valid = 1; }
    return r;
}
/* Run the game's input poll without disturbing the per-frame raw input state. */
static uint32_t poll_input_raw(void) {
    uint8_t save[G_INPUT_RAW_SIZE]; memcpy(save, G_INPUT_RAW, sizeof save);
    g_joy_use_cache = 1;
    uint32_t v;
    __asm__ volatile ("call *%1" : "=a"(v) : "r"(g_game->addr.poll_input) : "ecx", "edx", "memory", "cc");
    g_joy_use_cache = 0;
    v = G_INPUT_CUR;
    memcpy(G_INPUT_RAW, save, sizeof save);
    return v;
}
/* movement + focus bits of a polled value, merged into the frame's game input word */
static uint32_t merge_subtick_bits(uint32_t frame_val, uint32_t polled, int live) {
    uint32_t v = (frame_val & ~(uint32_t)(IN_MOVE | IN_FOCUS)) | (polled & IN_MOVE);
    uint32_t focus = polled & IN_FOCUS;
    if (live && (G_OPTION_FLAGS & 0x200) && G_AUTOFOCUS_CTR >= 8) focus = IN_FOCUS;   /* "hold shot to focus" option: synthesized by the replay node */
    return v | focus;
}
static inline uint8_t  bits_encode(uint32_t v) { return (uint8_t)((v >> 3) & 0x1f); }
static inline uint32_t bits_decode(uint8_t b)  { return ((uint32_t)b & 0x1f) << 3; }

struct TickBuf { uint8_t* d; uint32_t n, cap; int failed; };
static struct TickBuf g_rec[8];    /* per stage: bits of every tick since the stage's first frame (this session) */
static struct TickBuf g_play[8];   /* per stage: the same, loaded from the replay being played */
static int      g_frame_active;    /* the replay node ran on the current frame's boundary tick */
static int      g_stream_stage = -1;
static uint32_t g_stream_tick;     /* index of the current tick in the stage stream */
static unsigned g_stat_subtick_polls, g_stat_subtick_applied;
static void tickbuf_push(struct TickBuf* b, uint8_t v) {
    if (b->failed) return;
    if (b->n >= 3600000) { b->failed=1; return; }
    if (b->n == b->cap) { uint32_t nc = b->cap ? b->cap * 2 : 65536; uint8_t* nd = (uint8_t*)realloc(b->d, nc); if (!nd) { b->failed=1;return; } b->d = nd; b->cap = nc; }
    b->d[b->n++] = v;
}
/* called on the frame boundary tick just before the replay record/playback node runs its first frame of a stage */
static void replay_stage_start(uint8_t* rm) {
    int stage = *(int*)(rm + g_game->layout.replay_stage); int playing = *(int*)(rm + 0x10) == 1;
    schedule_reset_here();
    g_stream_stage = (stage >= 0 && stage < 8) ? stage : -1;
    g_stream_tick = 0;
    if (g_stream_stage >= 0 && !playing) { g_rec[g_stream_stage].n = 0;g_rec[g_stream_stage].failed=0; }
    LOG("stage %d first frame (%s): sub-step sequence restarted%s", stage, playing ? "playback" : "recording",
        (playing && g_stream_stage >= 0 && g_play[g_stream_stage].n) ? ", per-tick input available" : "");
}
static int subtick_active(uint8_t* rm) { return cfg.subtick_input && cfg.substep && g_logic_rate != 60 && rm && g_frame_active && g_stream_stage >= 0; }
/* start of a non-boundary tick: feed the player fresh (or recorded) movement/focus bits */
static void subtick_input_begin(void) {
    uint8_t* rm = G_REPLAY_MANAGER;
    if (!subtick_active(rm)) return;
    if (*(int*)(rm + 0x10) == 1) {
        struct TickBuf* b = &g_play[g_stream_stage];
        if (g_stream_tick < b->n) { G_GAME_INPUT = merge_subtick_bits(G_GAME_INPUT, bits_decode(b->d[g_stream_tick]), 0); g_stat_subtick_applied++; }
    } else {
        G_GAME_INPUT = merge_subtick_bits(G_GAME_INPUT, poll_input_raw(), 1); g_stat_subtick_polls++;
    }
}
/* end of every tick of an active frame: record the bits the player saw, advance the stream */
static void subtick_input_end(void) {
    uint8_t* rm = G_REPLAY_MANAGER;
    if (!subtick_active(rm)) return;
    if (*(int*)(rm + 0x10) != 1) tickbuf_push(&g_rec[g_stream_stage], bits_encode(G_GAME_INPUT));
    g_stream_tick++;
}
