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
#define G_INPUT_RAW_SIZE  0x248   /* the largest: TH14 saves its whole input object */
/* Every input word the runtime reads or writes comes through these two, and a profile that has
   not described one passes zero. Answering "no bits held" and dropping the write is the right
   degradation and it is the only place it has to be written: the game input word, the pressed
   and released edges and the autofocus counter all go through here. */
static uint32_t input_read(uintptr_t addr) {
    if (!addr) return 0;
    return g_game->layout.input_width == 2 ? *(uint16_t*)addr : *(uint32_t*)addr;
}
static void input_write(uintptr_t addr, uint32_t v) {
    if (!addr) return;
    if (g_game->layout.input_width == 2) *(uint16_t*)addr = (uint16_t)v;
    else *(uint32_t*)addr = v;
}
#define G_GAME_INPUT      input_read(g_game->addr.game_input)
static void set_game_input(uint32_t v) { input_write(g_game->addr.game_input, v); }
#define G_OPTION_FLAGS    (*(uint32_t*)g_game->addr.option_flags)
#define G_AUTOFOCUS_CTR   input_read(g_game->addr.autofocus)
#define IN_FOCUS (g_game->layout.focus_mask ? g_game->layout.focus_mask : 0x08u)
#define IN_MOVE  0xf0

/* joystick: winmm's joyGetPosEx is not a cheap read. With no controller attached it goes
   looking for one -- a device enumeration that costs tens of milliseconds -- and it does so
   again every second or so, on the caller's thread. TH10-12 call it from their frame function
   before the game logic runs, which at 360 Hz is a 30 ms hole a few times a second (TH13 reads
   its joystick differently). So the real call is made on a thread of our own, continuously,
   and the game is always answered from the latest reading; the thread eats the stalls. */
typedef MMRESULT (WINAPI *JoyGetPosExFn)(UINT, LPJOYINFOEX);
static JoyGetPosExFn orig_joyGetPosEx;
static JOYINFOEX g_joy_cache; static MMRESULT g_joy_cache_res; static volatile int g_joy_cache_valid; static int g_joy_use_cache;
static CRITICAL_SECTION g_joy_lock; static HANDLE g_joy_thread; static volatile int g_joy_stop;

static DWORD WINAPI joy_thread_main(void* arg) {
    (void)arg;
    while (!g_joy_stop) {
        JOYINFOEX ji; memset(&ji, 0, sizeof ji); ji.dwSize = sizeof ji; ji.dwFlags = JOY_RETURNALL;
        double t = now_s(); MMRESULT r = orig_joyGetPosEx(0, &ji); t = now_s() - t;
        EnterCriticalSection(&g_joy_lock); g_joy_cache = ji; g_joy_cache_res = r; g_joy_cache_valid = 1; g_stat_joy_polls++; if (t > g_stat_joy_max) g_stat_joy_max = t; LeaveCriticalSection(&g_joy_lock);
        Sleep(r == JOYERR_NOERROR ? 2 : 250);   /* a controller: 500 Hz; none: look again every quarter second, off the game's thread */
    }
    return 0;
}
static MMRESULT WINAPI hook_joyGetPosEx(UINT id, LPJOYINFOEX ji) {
    if (!ji || id != 0) return orig_joyGetPosEx(id, ji);
    if (!g_joy_thread) { InitializeCriticalSection(&g_joy_lock); g_joy_thread = CreateThread(NULL, 0, joy_thread_main, NULL, 0, NULL); }
    if (!g_joy_thread || !g_joy_cache_valid) {   /* the first call, or no thread: the slow way, once */
        double t = now_s(); MMRESULT r = orig_joyGetPosEx(id, ji); t = now_s() - t;
        if (t > g_stat_joy_max) g_stat_joy_max = t; g_stat_joy_polls++;
        return r;
    }
    DWORD n = ji->dwSize < sizeof g_joy_cache ? ji->dwSize : (DWORD)sizeof g_joy_cache; DWORD size = ji->dwSize, flags = ji->dwFlags;
    EnterCriticalSection(&g_joy_lock); memcpy(ji, &g_joy_cache, n); MMRESULT r = g_joy_cache_res; LeaveCriticalSection(&g_joy_lock);
    ji->dwSize = size; ji->dwFlags = flags;
    return r;
}
/* Run the game's input poll without disturbing the per-frame raw input state. */
static uint32_t poll_input_raw(void) {
    if (g_game->poll_raw) { g_joy_use_cache = 1; uint32_t r = g_game->poll_raw(); g_joy_use_cache = 0; return r; }
    uint8_t save[G_INPUT_RAW_SIZE];
    size_t n = g_game->layout.input_size;
    if (!n || n > sizeof save) n = sizeof save;
    memcpy(save, G_INPUT_RAW, n);
    g_joy_use_cache = 1;
    uint32_t v;
    /* Earlier engines take keyboard index zero in ECX; later ones ignore it. */
    __asm__ volatile ("xor %%ecx, %%ecx\n\tcall *%1" : "=a"(v) : "r"(g_game->addr.poll_input) : "ecx", "edx", "memory", "cc");
    g_joy_use_cache = 0;
    v = input_read(g_game->addr.raw_input);
    memcpy(G_INPUT_RAW, save, n);
    return v;
}
/* movement + focus bits of a polled value, merged into the frame's game input word */
static uint32_t merge_subtick_bits(uint32_t frame_val, uint32_t polled, int live) {
    uint32_t v = (frame_val & ~(uint32_t)(IN_MOVE | IN_FOCUS)) | (polled & IN_MOVE);
    uint32_t focus = polled & IN_FOCUS;
    if (live && g_game->addr.option_flags && (G_OPTION_FLAGS & (g_game->layout.autofocus_option ? g_game->layout.autofocus_option : 0x200u)) && G_AUTOFOCUS_CTR >= (g_game->layout.autofocus_frames ? g_game->layout.autofocus_frames : 8u)) focus = IN_FOCUS;   /* "hold shot to focus" option: synthesized by the replay node */
    return v | focus;
}
static inline uint8_t bits_encode(uint32_t v) { return (uint8_t)(((v & IN_MOVE) >> 3) | !!(v & IN_FOCUS)); }
static inline uint32_t bits_decode(uint8_t b) { return ((uint32_t)(b & 0x1e) << 3) | ((b & 1) ? IN_FOCUS : 0); }

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
    int stage = *(int*)(rm + g_game->layout.replay_stage); int playing = REPLAY_MODE(rm) == 1;
    schedule_reset_here();
    g_stream_stage = (stage >= 0 && stage < 8) ? stage : -1;
    g_stream_tick = 0;
    if (g_stream_stage >= 0 && !playing) { g_rec[g_stream_stage].n = 0;g_rec[g_stream_stage].failed=0; }
    LOG("stage %d first frame (%s): sub-step sequence restarted%s", stage, playing ? "playback" : "recording",
        (playing && g_stream_stage >= 0 && g_play[g_stream_stage].n) ? ", per-tick input available" : "");
}
/* ... and the same for sub-tick input: it reads the game's input word and calls its polling
   routine, so a profile without those addresses cannot have it however the ini is written. */
static int subtick_active(uint8_t* rm) {
    return cfg.subtick_input && cfg.substep && g_logic_rate != 60 && rm && g_frame_active && g_stream_stage >= 0
        && (g_game->addr.poll_input || g_game->poll_raw) && g_game->addr.game_input;
}
/* start of a non-boundary tick: feed the player fresh (or recorded) movement/focus bits */
static void subtick_input_begin(void) {
    uint8_t* rm = G_REPLAY_MANAGER;
    if (!subtick_active(rm)) return;
    if (REPLAY_MODE(rm) == 1) {
        struct TickBuf* b = &g_play[g_stream_stage];
        if (g_stream_tick < b->n) { set_game_input(merge_subtick_bits(G_GAME_INPUT, bits_decode(b->d[g_stream_tick]), 0)); g_stat_subtick_applied++; }
    } else {
        set_game_input(merge_subtick_bits(G_GAME_INPUT, poll_input_raw(), 1)); g_stat_subtick_polls++;
    }
}
/* end of every tick of an active frame: record the bits the player saw, advance the stream */
static void subtick_input_end(void) {
    uint8_t* rm = G_REPLAY_MANAGER;
    if (!subtick_active(rm)) return;
    if (REPLAY_MODE(rm) != 1) tickbuf_push(&g_rec[g_stream_stage], bits_encode(G_GAME_INPUT));
    g_stream_tick++;
}
