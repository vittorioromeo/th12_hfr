/*
 * th12_hfr.dll — high refresh rate patch for Touhou 12 ~ Undefined Fantastic Object v1.00b
 * (th12.exe and th12e.exe share the same code layout).
 *
 * Design:
 *   The engine has a global "game speed" float (0x4b2ed0) that every Timer and most integrators
 *   already multiply by (ZUN uses it for the boss-death slow motion, ECL ins_447).  We run the
 *   engine's update pass at the display refresh rate; each UpdateFunc is classified either as
 *   SUB   (runs every sub-tick with game speed = logical * dt, dt = 60/refresh) or
 *   FRAME (runs only on the sub-tick where the integer frame counter increments, with the stock
 *          game speed, i.e. bit-identical 60 Hz behaviour).
 *   Draw funcs run every sub-tick, Present happens every sub-tick.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ logging */
static FILE* g_log;
static void logf_(const char* fmt, ...) {
    if (!g_log) return;
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}
#define LOG(...) logf_(__VA_ARGS__)

/* ------------------------------------------------------------------ config */
static struct {
    int fps;            /* 0 = auto from display */
    int vsync;          /* present interval one */
    int substep;        /* 1 = sub-step gameplay, 0 = stock logic at 60 Hz + high rate presentation */
    int log;
    int fullscreen_refresh; /* refresh rate to request in fullscreen (0 = auto = same as fps) */
    int show_stats;
    int enemy_interp;
    int debug;
} cfg = { 0, 1, 1, 1, 0, 0, 1, 0 };

/* ------------------------------------------------------------------ patch utils */
static int patch_bytes(uintptr_t addr, const void* data, size_t n, const void* expect) {
    DWORD old;
    if (expect && memcmp((void*)addr, expect, n) != 0) {
        LOG("patch @%08x: unexpected original bytes, skipping", (unsigned)addr);
        return 0;
    }
    if (!VirtualProtect((void*)addr, n, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy((void*)addr, data, n);
    VirtualProtect((void*)addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)addr, n);
    return 1;
}
static int patch_call(uintptr_t addr, void* target, const void* expect5) {
    uint8_t b[5]; b[0] = 0xE8; int32_t rel = (int32_t)((uintptr_t)target - (addr + 5)); memcpy(b + 1, &rel, 4);
    return patch_bytes(addr, b, 5, expect5);
}
static int patch_jmp(uintptr_t addr, void* target, const void* expect5) {
    uint8_t b[5]; b[0] = 0xE9; int32_t rel = (int32_t)((uintptr_t)target - (addr + 5)); memcpy(b + 1, &rel, 4);
    return patch_bytes(addr, b, 5, expect5);
}
/* replace an n-byte instruction sequence (n>=5) with call target + nops */
static int patch_call_n(uintptr_t addr, void* target, size_t n, const void* expect) {
    uint8_t b[16]; memset(b, 0x90, sizeof b);
    b[0] = 0xE8; int32_t rel = (int32_t)((uintptr_t)target - (addr + 5)); memcpy(b + 1, &rel, 4);
    return patch_bytes(addr, b, n, expect);
}

/* ------------------------------------------------------------------ game symbols */
#define G_GAME_SPEED      (*(volatile float*)0x4b2ed0)
#define G_UPDATE_RUNNER   (*(uint8_t**)0x4ce89c)
#define G_D3D_DEVICE      (*(IDirect3DDevice9**)0x4ce8f0)
#define G_PP              ((D3DPRESENT_PARAMETERS*)0x4ce9dc)
#define G_WINDOW_FLAGS    (*(uint32_t*)0x4cf428)
#define G_FRAME_TIME_DBL  (*(double*)0x4cf2a0)
#define G_MISC_FLAGS      (*(uint32_t*)0x4cee78)
#define G_INPUT_CUR       (*(uint32_t*)0x4d48b8)
#define G_INPUT_PRESSED   (*(uint32_t*)0x4d48c4)

typedef int (__stdcall *FrameFn)(void* ctx);
static FrameFn orig_frame_vsync = (FrameFn)0x450600;

/* ------------------------------------------------------------------ tick state */
static int    g_refresh = 60;      /* presents per second (display) */
static int    g_logic_rate = 60;   /* logic ticks per second (== g_refresh normally; a replay's recording rate during playback) */
static int    g_skip_update = 0;   /* present without running the update list (duplicate frame) */
static unsigned g_lacc = 0;        /* Bresenham remainder: logic ticks per present */
static float  g_dt = 1.0f;         /* game frames per tick for SUB nodes */
static unsigned g_tick = 0;        /* sub-tick counter */
static float g_ptf_prev, g_ptf_cur;   /* player state timer (float) before/after the last Player node call */
static double g_t0 = 0;            /* wall-clock origin of the tick schedule */
static unsigned g_stat_skipped;
static unsigned g_last_units = 0;  /* units of the previous tick */
static unsigned g_prev_frame = 0;  /* frame index (within the 60-frame cycle) of the previous tick */
static int    g_major = 1;         /* this tick is a frame boundary */
static double g_phase = 0;         /* position of this tick inside the current frame, [0,1) */
static float  g_logical = 1.0f;    /* the game's own notion of game speed (1.0, or ECL slow-mo) */
static float  g_factor = 1.0f;     /* factor of the node currently running (1 or dt) */
static float  g_pause_shadow = 1.0f;

/* Sub-step lengths are chosen as multiples of 1/256 frame (dyadic, exact in float32), distributed with a
   Bresenham sequence so that exactly 60 frames elapse every R ticks. The engine's timers accumulate the
   sub-step in float and compare with integer frame counts; with dyadic steps every partial sum is exact,
   so timer events happen on the right tick with no drift (a constant 60/R would not be representable). */
#define UNITS_PER_FRAME 256
static unsigned g_units_acc;   /* Bresenham remainder */
static unsigned g_units_total; /* units elapsed at the start of the current tick (mod 60 frames) */
static void set_logic_rate(int rate) {
    if (rate < 60) rate = 60;
    if (rate > 1000) rate = 1000;
    g_logic_rate = rate;
    g_dt = cfg.substep ? 60.0f / (float)rate : 1.0f;
    g_tick = 0; g_units_acc = 0; g_units_total = 0; g_major = 1; g_phase = 0; g_lacc = 0; g_prev_frame = 0;
    g_t0 = 0;
    LOG("logic rate: %d ticks/s (nominal dt=%.6f frames/tick), present rate %d Hz, substep=%d", g_logic_rate, g_dt, g_refresh, cfg.substep);
}
static void recompute_rate(int refresh) {
    if (refresh < 60) refresh = 60;
    if (refresh > 1000) refresh = 1000;
    g_refresh = refresh;
    set_logic_rate(refresh);
}
/* number of logic ticks to run for the next present slot */
static int ticks_for_slot(void) {
    g_lacc += (unsigned)g_logic_rate;
    int n = (int)(g_lacc / (unsigned)g_refresh);
    g_lacc -= (unsigned)n * (unsigned)g_refresh;
    return n;
}
static void advance_tick(void) {
    if (!cfg.substep || g_logic_rate == 60) { g_major = 1; g_dt = 1.0f; g_phase = 0; g_tick++; return; }
    /* this tick starts at g_units_total; it is a frame boundary tick if the previous tick started in an earlier frame */
    unsigned cur_frame = g_units_total / UNITS_PER_FRAME;
    g_major = (g_tick == 0) || (cur_frame != g_prev_frame);
    g_prev_frame = cur_frame;
    g_phase = (double)(g_units_total % UNITS_PER_FRAME) / UNITS_PER_FRAME;
    /* Bresenham step for this tick */
    g_units_acc += 60u * UNITS_PER_FRAME;
    unsigned u = g_units_acc / (unsigned)g_logic_rate;
    g_units_acc -= u * (unsigned)g_logic_rate;
    g_dt = (float)u / (float)UNITS_PER_FRAME;
    g_last_units = u;
    g_units_total += u;
    if (g_units_total >= 60u * UNITS_PER_FRAME) g_units_total -= 60u * UNITS_PER_FRAME; /* every R ticks (1 s) exactly */
    g_tick++;
}

/* ------------------------------------------------------------------ node classification */
enum { MODE_FRAME = 0, MODE_SUB = 1 };
struct node_class { uint32_t func; int mode; const char* name; };
static struct node_class g_classes[] = {
    /* func (UpdateFunc callback address) , mode , name */
    { 0x40a1f0, MODE_SUB,   "BulletManager"   },
    { 0x437660, MODE_SUB,   "Player"          },
    { 0x406bb0, MODE_FRAME, "Bomb"            },
    { 0x427380, MODE_SUB,   "ItemManager"     },
    { 0x4283d0, MODE_SUB,   "LaserManager"    },
    { 0x41f8d0, MODE_FRAME, "Gui"             },
    { 0x403ec0, MODE_SUB,   "Stage"           },
    { 0x460c40, MODE_SUB,   "AnmManagerWorld" },
    { 0x460c30, MODE_SUB,   "AnmManagerUI"    },
    { 0x40e040, MODE_FRAME, "Spellcard"       },
    { 0x413210, MODE_FRAME, "EnemyManager"    },
    { 0x44a860, MODE_FRAME, "UfoManager"      },
    { 0x424190, MODE_FRAME, "PlayerBomb?"     },
    { 0x422bd0, MODE_FRAME, "GameManager"     },
};
static int g_sub_enabled[sizeof g_classes / sizeof g_classes[0]];

static int node_mode(uint32_t func) {
    if (!cfg.substep) return MODE_FRAME;
    for (size_t i = 0; i < sizeof g_classes / sizeof g_classes[0]; i++)
        if (g_classes[i].func == func) return g_sub_enabled[i] ? g_classes[i].mode : MODE_FRAME;
    return MODE_FRAME;
}

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

/* naked asm trampolines: the patched instruction is "fstp dword [0x4b2ed0]" (6 bytes) -> call stub (5) + nop */
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

static const uint8_t FSTP_SPEED[6] = { 0xD9, 0x1D, 0xD0, 0x2E, 0x4B, 0x00 };

static void install_speed_patches(void) {
    /* permanent 1.0 */
    static const uintptr_t perm[] = { 0x421d5f, 0x4222a0, 0x42f56d, 0x436ed7, 0x4653fc };
    /* temporary 1.0 (Stage / AnmVm "unaffected by slow-mo") */
    static const uintptr_t temp[] = { 0x4030fc, 0x455670 };
    /* pause set 1.0 (after saving effective) */
    static const uintptr_t pset[] = { 0x432835, 0x43293c, 0x433853, 0x4339a2 };
    /* pause restore */
    static const uintptr_t prest[] = { 0x432988, 0x433a1d, 0x4348ed };
    for (size_t i = 0; i < 5; i++) patch_call_n(perm[i], (void*)stub_set_one_perm, 6, FSTP_SPEED);
    for (size_t i = 0; i < 2; i++) patch_call_n(temp[i], (void*)stub_set_one_temp, 6, FSTP_SPEED);
    for (size_t i = 0; i < 4; i++) patch_call_n(pset[i], (void*)stub_pause_set, 6, FSTP_SPEED);
    for (size_t i = 0; i < 3; i++) patch_call_n(prest[i], (void*)stub_pause_restore, 6, FSTP_SPEED);
    patch_call_n(0x4193e4, (void*)stub_set_ecl, 6, FSTP_SPEED);
    /* 0x403123, 0x42840c, 0x42841b, 0x455b3e, 0x4586f4, 0x45871f: save/restore of effective values or
       literal 0.0 — left untouched. */
}


/* ------------------------------------------------------------------ mini assembler for guard stubs */
static uint8_t* g_stub_mem; static size_t g_stub_used;
static uint8_t* stub_begin(void) {
    if (!g_stub_mem) g_stub_mem = (uint8_t*)VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    return g_stub_mem + g_stub_used;
}
static uint8_t* g_p;
#define E(...) do { const uint8_t b_[] = { __VA_ARGS__ }; memcpy(g_p, b_, sizeof b_); g_p += sizeof b_; } while (0)
static void E32(uint32_t v) { memcpy(g_p, &v, 4); g_p += 4; }
static void EJMP(uintptr_t target) { *g_p++ = 0xE9; E32((uint32_t)(target - ((uintptr_t)g_p + 4))); }
static void EJCC(uint8_t cc, uintptr_t target) { *g_p++ = 0x0F; *g_p++ = cc; E32((uint32_t)(target - ((uintptr_t)g_p + 4))); } /* cc: 0x84 je, 0x85 jne, 0x8e jle ... */
static void ECALL(uintptr_t target) { *g_p++ = 0xE8; E32((uint32_t)(target - ((uintptr_t)g_p + 4))); }
static void ECOPY(uintptr_t src, size_t n) { memcpy(g_p, (void*)src, n); g_p += n; }
static void stub_end(void) { g_stub_used = (size_t)(g_p - g_stub_mem); }
/* redirect addr (n bytes, n>=5) to the stub being built at g_p */
static uint8_t* g_stub_start;
#define STUB_BEGIN() (g_stub_start = g_p)
static void hook_site(uintptr_t addr, size_t n, const void* expect) {
    uint8_t b[32]; memset(b, 0x90, sizeof b); b[0] = 0xE9; int32_t rel = (int32_t)((uintptr_t)g_stub_start - (addr + 5)); memcpy(b + 1, &rel, 4);
    if (!patch_bytes(addr, b, n, expect)) LOG("site patch @%08x failed", (unsigned)addr);
    g_stub_start = NULL;
}
/* emit "push eax; mov eax,[reg+prev]; cmp eax,[reg+cur]; pop eax" — ZF=1 if the timer's integer part did not change */
static void E_timer_unchanged(uint8_t reg_modrm_base, int32_t prev_off, int32_t cur_off) {
    E(0x50);                                            /* push eax */
    E(0x8B, 0x80 | reg_modrm_base); E32(prev_off);      /* mov eax,[reg+prev] */
    E(0x3B, 0x80 | reg_modrm_base); E32(cur_off);       /* cmp eax,[reg+cur]  */
    E(0x58);                                            /* pop eax */
}
/* emit "cmp dword [g_major],0" — ZF=1 on sub-ticks that are not frame boundaries */
static void E_not_major(void) { E(0x83, 0x3D); E32((uint32_t)(uintptr_t)&g_major); E(0x00); }
enum { R_EAX = 0, R_ECX = 1, R_EDX = 2, R_EBX = 3, R_EBP = 5, R_ESI = 6, R_EDI = 7 };

static void install_site_patches(void) {
    g_p = stub_begin();
    /* fmul dword [g_factor] : scale a per-frame amount by the sub-step only (not by the ECL slow-motion factor,
       which the original code did not apply to these either) */
    uint8_t FMUL_SPEED[6] = { 0xD8, 0x0D, 0, 0, 0, 0 }; { uint32_t a = (uint32_t)(uintptr_t)&g_factor; memcpy(FMUL_SPEED + 2, &a, 4); }

    /* --- Player shots: pos += vel * speed (0x437016..0x43702a, 20 bytes) --- */
    { static const uint8_t ex[] = { 0xD9, 0x46, 0xE0, 0x8B, 0x44, 0x24, 0x10, 0xD8, 0x00, 0xD9, 0x18, 0xD9, 0x46, 0xE8, 0xD8, 0x46, 0xE4, 0xD9, 0x5E, 0xE4 };
      STUB_BEGIN();
      E(0xD9, 0x46, 0xE0); E(FMUL_SPEED[0], FMUL_SPEED[1], FMUL_SPEED[2], FMUL_SPEED[3], FMUL_SPEED[4], FMUL_SPEED[5]);
      E(0x8B, 0x44, 0x24, 0x10);          /* mov eax,[esp+0x10] (same frame: we jumped, not called) */
      E(0xD8, 0x00, 0xD9, 0x18);          /* fadd [eax]; fstp [eax] */
      E(0xD9, 0x46, 0xE8); E(FMUL_SPEED[0], FMUL_SPEED[1], FMUL_SPEED[2], FMUL_SPEED[3], FMUL_SPEED[4], FMUL_SPEED[5]);
      E(0xD8, 0x46, 0xE4, 0xD9, 0x5E, 0xE4);
      EJMP(0x43702a);
      hook_site(0x437016, 20, ex); }
    /* --- Player shots: angle += angular velocity * speed (0x436fe2: fld [esi+0x18]; push ecx; fadd [esi+0x14]) --- */
    { static const uint8_t ex[] = { 0xD9, 0x46, 0x18, 0x51, 0xD8, 0x46, 0x14 };
      STUB_BEGIN();
      E(0xD9, 0x46, 0x18); E(FMUL_SPEED[0], FMUL_SPEED[1], FMUL_SPEED[2], FMUL_SPEED[3], FMUL_SPEED[4], FMUL_SPEED[5]);
      E(0x51); E(0xD8, 0x46, 0x14);
      EJMP(0x436fe9);
      hook_site(0x436fe2, 7, ex); }
    /* --- Player: death particles once per frame: cmp [edi+0xa34],3 @0x436dd9 (jne @0x436de0 -> 0x436e91) --- */
    { static const uint8_t ex[] = { 0x83, 0xBF, 0x34, 0x0A, 0x00, 0x00, 0x03 };
      STUB_BEGIN();
      E(0x83, 0xBF, 0x34, 0x0A, 0x00, 0x00, 0x03);   /* cmp [edi+0xa34],3 */
      EJCC(0x85, 0x436de0);                           /* jne -> original jne (taken) */
      E_timer_unchanged(R_EDI, 0xa30, 0xa34);
      EJCC(0x84, 0x436e91);                           /* unchanged -> skip block */
      E(0x39, 0xFF);                                  /* cmp edi,edi -> ZF=1 */
      EJMP(0x436de0);
      hook_site(0x436dd9, 7, ex); }
    /* --- Player: state_timer % 60 == 0 counter once per frame @0x4374fc --- */
    { static const uint8_t ex[] = { 0x8B, 0x87, 0x34, 0x0A, 0x00, 0x00 };
      STUB_BEGIN();
      E(0x8B, 0x87, 0x34, 0x0A, 0x00, 0x00);          /* mov eax,[edi+0xa34] */
      E(0x3B, 0x87, 0x30, 0x0A, 0x00, 0x00);          /* cmp eax,[edi+0xa30] */
      EJCC(0x84, 0x43753d);
      EJMP(0x437502);
      hook_site(0x4374fc, 6, ex); }
    /* --- Player: option gather counter inc [edi+0xc418] once per frame @0x4368f7 --- */
    { static const uint8_t ex[] = { 0xFF, 0x87, 0x18, 0xC4, 0x00, 0x00 };
      STUB_BEGIN();
      E_timer_unchanged(R_EDI, 0xa30, 0xa34);
      EJCC(0x84, 0x4368fd);
      E(0xFF, 0x87, 0x18, 0xC4, 0x00, 0x00);
      EJMP(0x4368fd);
      hook_site(0x4368f7, 6, ex); }
    /* --- Bullet: per-frame counters [ebp+4]-- and [ebp+0x520]-- once per frame @0x409fdb..0x409ff7 --- */
    { static const uint8_t ex[] = { 0x8B, 0x45, 0x04, 0x85, 0xC0 };
      STUB_BEGIN();
      E_timer_unchanged(R_EBP, 0x4e4, 0x4e8);
      EJCC(0x84, 0x409ff7);
      E(0x8B, 0x45, 0x04, 0x85, 0xC0);
      EJMP(0x409fe0);
      hook_site(0x409fdb, 5, ex); }

    /* --- Items: "+= 0.2 per frame" (fadd qword [0x4a3fb8]) -> += 0.2 * speed --- */
    { static const uintptr_t sites[] = { 0x425fc5, 0x426080, 0x426218 };
      static const uint8_t ex[] = { 0xDC, 0x05, 0xB8, 0x3F, 0x4A, 0x00 };
      for (int i = 0; i < 3; i++) {
          STUB_BEGIN();
          E(0xDD, 0x05, 0xB8, 0x3F, 0x4A, 0x00);        /* fld qword [0x4a3fb8] */
          E(0xD8, 0x0D, 0xD0, 0x2E, 0x4B, 0x00);        /* fmul dword [speed] */
          E(0xDE, 0xC1);                                /* faddp st(1),st */
          EJMP(sites[i] + 6);
          hook_site(sites[i], 6, ex);
      } }
    /* --- Items: UFO attraction acceleration: scale increment before "fadd [edi+0x9bc]" @0x426926 --- */
    { static const uint8_t ex[] = { 0xD8, 0x87, 0xBC, 0x09, 0x00, 0x00 };
      STUB_BEGIN();
      E(0xD8, 0x0D, 0xD0, 0x2E, 0x4B, 0x00);            /* fmul dword [speed] */
      E(0xD8, 0x87, 0xBC, 0x09, 0x00, 0x00);
      EJMP(0x42692c);
      hook_site(0x426926, 6, ex); }
    /* --- Items: state-5 countdown [edi+0x9c0]-- once per frame @0x425c5c (jns @0x425c63 -> 0x426f53) --- */
    { static const uint8_t ex[] = { 0x83, 0x87, 0xC0, 0x09, 0x00, 0x00, 0xFF };
      STUB_BEGIN();
      E_not_major();                                    /* (state-5 items do not tick their timer) */
      EJCC(0x84, 0x426f53);                             /* not a frame tick: behave as "not negative yet" */
      E(0x83, 0x87, 0xC0, 0x09, 0x00, 0x00, 0xFF);
      EJMP(0x425c63);
      hook_site(0x425c5c, 7, ex); }

    /* --- Lasers: ex wait counter [laser+0x44c]-- once per frame (timer +0x14/+0x18 ticked by the manager) --- */
    { struct { uintptr_t addr; uint8_t reg; uintptr_t skip, cont; } L[] = {
          { 0x42979a, R_EDI, 0x4297ab, 0x4297a0 },   /* LaserLine  */
          { 0x42c90c, R_ESI, 0x42c91d, 0x42c912 },   /* LaserCurve */
          { 0x42adef, R_EDI, 0x42ae00, 0x42adf5 } }; /* LaserBeam  */
      for (int i = 0; i < 3; i++) {
          uint8_t ex[6] = { 0x8B, (uint8_t)(0x80 | L[i].reg), 0x4C, 0x04, 0x00, 0x00 };
          STUB_BEGIN();
          E_timer_unchanged(L[i].reg, 0x14, 0x18);
          EJCC(0x84, L[i].skip);
          E(0x8B, (uint8_t)(0x80 | L[i].reg), 0x4C, 0x04, 0x00, 0x00);
          EJMP(L[i].cont);
          hook_site(L[i].addr, 6, ex);
      } }
    /* --- Lasers: graze every 3 frames -> gate on the graze timer's integer change --- */
    { struct { uintptr_t addr; uint8_t reg; int32_t cur, prev; uintptr_t skip, cont; } G[] = {
          { 0x429a55, R_EDI, 0x2c, 0x28, 0x429a76, 0x429a5e },   /* LaserLine  */
          { 0x42cbf7, R_ESI, 0x2c, 0x28, 0x42cc1d, 0x42cc00 },   /* LaserCurve */
          { 0x42b068, R_EDI, 0x18, 0x14, 0x42b089, 0x42b071 } }; /* LaserBeam  */
      for (int i = 0; i < 3; i++) {
          uint8_t ex[9] = { 0x8B, (uint8_t)(0x40 | G[i].reg), (uint8_t)G[i].cur, 0x99, 0xB9, 0x03, 0x00, 0x00, 0x00 };
          STUB_BEGIN();
          E(0x8B, (uint8_t)(0x40 | G[i].reg), (uint8_t)G[i].cur);      /* mov eax,[reg+cur] */
          E(0x3B, (uint8_t)(0x40 | G[i].reg), (uint8_t)G[i].prev);     /* cmp eax,[reg+prev] */
          EJCC(0x84, G[i].skip);
          E(0x99, 0xB9, 0x03, 0x00, 0x00, 0x00);                        /* cdq; mov ecx,3 */
          EJMP(G[i].cont);
          hook_site(G[i].addr, 9, ex);
      } }

    /* --- Stage: spell/bomb background distortion (uses RNG every frame) only on frame ticks @0x403145 --- */
    { static const uint8_t ex[] = { 0x8B, 0x8B, 0xDC, 0x35, 0x00, 0x00, 0x33, 0xFF, 0x3B, 0xCF };
      STUB_BEGIN();
      E(0x8B, 0x8B, 0xDC, 0x35, 0x00, 0x00);          /* mov ecx,[ebx+0x35dc] */
      E(0x33, 0xFF, 0x3B, 0xCF);                      /* xor edi,edi; cmp ecx,edi */
      EJCC(0x84, 0x4036dd);
      E_not_major();
      EJCC(0x84, 0x4036dd);
      EJMP(0x403155);
      hook_site(0x403145, 10, ex); }
    /* --- Stage: distortion frame counter [ebx+0x35d8]++ only on frame ticks @0x4036dd --- */
    { static const uint8_t ex[] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0x01, 0x83, 0xD8, 0x35, 0x00, 0x00 };
      STUB_BEGIN();
      E(0xB8, 0x01, 0x00, 0x00, 0x00);                /* mov eax,1 (also the return value) */
      E_not_major();
      EJCC(0x84, 0x4036e8);
      E(0x01, 0x83, 0xD8, 0x35, 0x00, 0x00);          /* add [ebx+0x35d8],eax */
      EJMP(0x4036e8);
      hook_site(0x4036dd, 11, ex); }

    /* --- Player movement: fixed-point step = ftol(vel*speed) loses up to 1 unit per tick; carry the residual --- */
    { static float res[2];
      static const uint8_t ex[] = { 0xE8, 0x11, 0xCA, 0x05, 0x00 };
      static const uint8_t ex2[] = { 0xE8, 0xFE, 0xC9, 0x05, 0x00 };
      for (int i = 0; i < 2; i++) {
          uintptr_t site = i == 0 ? 0x4367ca : 0x4367dd;
          uint8_t* st = g_p;
          E(0xD8, 0x05); E32((uint32_t)(uintptr_t)&res[i]);        /* fadd dword [res] */
          E(0xD9, 0xC0);                                            /* fld st(0) */
          ECALL(0x4931e0);                                          /* eax = trunc(st0) */
          E(0x50, 0xDB, 0x04, 0x24, 0x58);                          /* push eax; fild dword [esp]; pop eax */
          E(0xDE, 0xE9);                                            /* fsubp st(1),st  -> residual */
          E(0xD9, 0x1D); E32((uint32_t)(uintptr_t)&res[i]);        /* fstp dword [res] */
          E(0xC3);                                                  /* ret */
          patch_call(site, st, i == 0 ? ex : ex2);
      } }

    /* --- Timer::add sites whose argument is a script/engine constant in frames, not a per-frame rate:
           add value * logical speed (stock semantics) instead of value * logical * dt.
           0x439ac2: player shot cycle timer -= 14 ; 0x43adbd: ANM "timer -= N" helper --- */
    { static const uint8_t ex1[] = { 0xE8, 0x59, 0xAF, 0x02, 0x00 }, ex2[] = { 0xE8, 0x5E, 0x9C, 0x02, 0x00 };
      uint8_t* st = g_p;
      E(0x8B, 0x46, 0x04, 0x89, 0x06);                              /* mov eax,[esi+4]; mov [esi],eax */
      E(0xD9, 0x44, 0x24, 0x04);                                    /* fld dword [esp+4] */
      E(0xD8, 0x0D); E32((uint32_t)(uintptr_t)&g_logical);          /* fmul dword [g_logical] */
      E(0xD8, 0x46, 0x08, 0xD9, 0x56, 0x08);                        /* fadd [esi+8]; fst [esi+8] */
      ECALL(0x4931e0);
      E(0x89, 0x46, 0x04);                                          /* mov [esi+4],eax */
      E(0xC2, 0x04, 0x00);                                          /* ret 4 */
      patch_call(0x439ac2, st, ex1);
      patch_call(0x43adbd, st, ex2); }

    /* --- MotionState::step (0x464db0): pos += vel  ->  pos += vel * g_factor (player shots, damage sources; enemies/bombs run with factor 1) --- */
    { static const uint8_t ex[] = { 0xD9, 0x43, 0x0C, 0x8B, 0xF3, 0xD8, 0x03, 0xD9, 0x1B, 0xD9, 0x43, 0x10, 0xD8, 0x43, 0x04, 0xD9, 0x5B, 0x04, 0xD9, 0x43, 0x14, 0xD8, 0x43, 0x08, 0xD9, 0x5B, 0x08 };
      STUB_BEGIN();
      E(0xD9, 0x43, 0x0C); E(FMUL_SPEED[0], FMUL_SPEED[1], FMUL_SPEED[2], FMUL_SPEED[3], FMUL_SPEED[4], FMUL_SPEED[5]); E(0xD8, 0x03, 0xD9, 0x1B);
      E(0xD9, 0x43, 0x10); E(FMUL_SPEED[0], FMUL_SPEED[1], FMUL_SPEED[2], FMUL_SPEED[3], FMUL_SPEED[4], FMUL_SPEED[5]); E(0xD8, 0x43, 0x04, 0xD9, 0x5B, 0x04);
      E(0xD9, 0x43, 0x14); E(FMUL_SPEED[0], FMUL_SPEED[1], FMUL_SPEED[2], FMUL_SPEED[3], FMUL_SPEED[4], FMUL_SPEED[5]); E(0xD8, 0x43, 0x08, 0xD9, 0x5B, 0x08);
      E(0x8B, 0xF3);                                   /* mov esi,ebx */
      EJMP(0x464dd7);
      hook_site(0x464dbc, 27, ex); }

    /* --- Player shots (0x439b10 loop): speed += accel * factor @0x439b72 --- */
    { static const uint8_t ex[] = { 0xD9, 0x47, 0x18, 0x51, 0xD8, 0x47, 0x14 };
      STUB_BEGIN();
      E(0xD9, 0x47, 0x18); E(FMUL_SPEED[0], FMUL_SPEED[1], FMUL_SPEED[2], FMUL_SPEED[3], FMUL_SPEED[4], FMUL_SPEED[5]);
      E(0x51); E(0xD8, 0x47, 0x14);
      EJMP(0x439b79);
      hook_site(0x439b72, 7, ex); }

    /* --- Enemy hit test guard (0x439ed0): "player state timer unchanged this frame -> no damage".
           With sub-steps the integer timer changes on one tick in K; use "player timer advanced since the
           previous Player update" instead (tracked by the runner). @0x439ef2: cmp eax,[esi+0xa30]; jne 0x439f05 --- */
    { static const uint8_t ex[] = { 0x3B, 0x86, 0x30, 0x0A, 0x00, 0x00 };
      STUB_BEGIN();
      E(0x50);                                            /* push eax */
      E(0xA1); E32((uint32_t)(uintptr_t)&g_ptf_prev);      /* mov eax,[g_ptf_prev] */
      E(0x3B, 0x05); E32((uint32_t)(uintptr_t)&g_ptf_cur); /* cmp eax,[g_ptf_cur] */
      E(0x58);                                            /* pop eax */
      EJCC(0x85, 0x439f05);                               /* changed -> proceed */
      EJMP(0x439efa);                                     /* unchanged -> return 0 */
      hook_site(0x439ef2, 6, ex); }

    /* --- Enemy death ring effect callback (0x4107e0): shrink/fade once per frame @0x410814 --- */
    { static const uint8_t ex[] = { 0xD9, 0x47, 0x20, 0x80, 0x47, 0x2B, 0x03, 0xDC, 0x25, 0x20, 0x42, 0x4A, 0x00, 0xD9, 0x5F, 0x20 };
      STUB_BEGIN();
      E_timer_unchanged(R_EDI, 0x0c, 0x10);
      EJCC(0x84, 0x410824);
      ECOPY(0x410814, 16);
      EJMP(0x410824);
      hook_site(0x410814, 16, ex); }

    /* --- Scrolling-mesh effect VM callback (0x45dcd0, fastcall ECX=vm): per-frame UV scroll; run once per frame --- */
    { static const uint8_t ex[] = { 0x83, 0xEC, 0x18, 0xD9, 0x05, 0xC8, 0x3D, 0x4A, 0x00 };
      STUB_BEGIN();
      E(0x50);                                            /* push eax */
      E(0x8B, 0x81); E32(0x68);                           /* mov eax,[ecx+0x68] (timer prev) */
      E(0x3B, 0x81); E32(0x6c);                           /* cmp eax,[ecx+0x6c] (timer cur) */
      E(0x58);                                            /* pop eax */
      E(0x75, 0x03);                                      /* jne run */
      E(0x31, 0xC0, 0xC3);                                /* xor eax,eax; ret   (skip this tick) */
      ECOPY(0x45dcd0, 9);                                 /* run: original prologue */
      EJMP(0x45dcd9);
      hook_site(0x45dcd0, 9, ex); }
    stub_end();
    LOG("site patches installed (%u bytes of stubs)", (unsigned)g_stub_used);
}


/* ------------------------------------------------------------------ enemy render interpolation
 * Enemies (ECL) stay on stock 60 Hz logic. To make them move smoothly we place their sprites, on every
 * tick, at the position interpolated between the last two frame positions (same lag as sub-stepped objects).
 */
#define G_ENEMY_MANAGER   (*(uint8_t**)0x4b43dc)
#define G_ANM_MANAGER     (*(uint8_t**)0x4ce8cc)
typedef float* (*GetVmFn)(uint8_t* mgr, int id);
static float* anm_get_vm(uint8_t* mgr, int id) {
    float* r; uint8_t* m = mgr;
    __asm__ volatile ("push %2\n\t" "call *%3\n\t" : "=a"(r), "+d"(m) : "r"(id), "r"(0x461920) : "ecx", "memory", "cc");
    return r;
}
struct EnemyTrack { uint8_t* enemy; float last[3]; float prev[3]; unsigned seen; };
#define TRACK_N 2048
static struct EnemyTrack g_tracks[TRACK_N];
static unsigned g_major_count;
static struct EnemyTrack* track_find(uint8_t* e) {
    unsigned h = ((uintptr_t)e >> 4) & (TRACK_N - 1);
    struct EnemyTrack* stale = NULL;
    for (unsigned i = 0; i < 64; i++) {
        struct EnemyTrack* t = &g_tracks[(h + i) & (TRACK_N - 1)];
        if (t->enemy == e) return t;
        if (!stale && (t->enemy == NULL || t->seen + 2 < g_major_count)) stale = t;   /* free or not seen for 2 frames */
    }
    if (stale) { stale->enemy = NULL; }
    return stale;
}
static void enemy_interp(double phase) {
    if (!cfg.substep || !cfg.enemy_interp) return;
    uint8_t* em = G_ENEMY_MANAGER; uint8_t* am = G_ANM_MANAGER;
    if (!em || !am) return;
    if (g_major) g_major_count++;
    float alpha = (float)(phase + g_dt); if (alpha > 1.0f) alpha = 1.0f;   /* fraction of the frame's motion to show */
    for (uint32_t* node = *(uint32_t**)(em + 0x68); node; node = (uint32_t*)node[1]) {
        uint8_t* e = (uint8_t*)node[0];
        if (!e) continue;
        uint32_t flags = *(uint32_t*)(e + 0x26f8);
        if (flags & 0x01000000) continue;                 /* being deleted */
        float* P = (float*)(e + 0x1074);
        struct EnemyTrack* t = track_find(e);
        if (!t) continue;
        if (g_major) {
            if (t->enemy == e && t->seen == g_major_count - 1) { memcpy(t->prev, t->last, 12); }
            else { memcpy(t->prev, P, 12); }
            memcpy(t->last, P, 12); t->enemy = e; t->seen = g_major_count;
        } else if (t->enemy != e) { memcpy(t->prev, P, 12); memcpy(t->last, P, 12); t->enemy = e; t->seen = g_major_count; }
        float d[3] = { t->last[0] - t->prev[0], t->last[1] - t->prev[1], t->last[2] - t->prev[2] };
        if (fabsf(d[0]) > 48.0f || fabsf(d[1]) > 48.0f) d[0] = d[1] = d[2] = 0;   /* teleport */
        float R[3] = { t->last[0] - d[0] * (1.0f - alpha), t->last[1] - d[1] * (1.0f - alpha), t->last[2] - d[2] * (1.0f - alpha) };
        int* ids = (int*)(e + 0x1120); float* offs = (float*)(e + 0x1168); int* parent = (int*)(e + 0x1220);
        if (!(flags & 0x2000000)) {
            for (int i = 0; i < 14; i++) {
                if (!ids[i]) continue;
                float* vm = anm_get_vm(am, ids[i]);
                if (!vm) continue;
                float x = R[0] + offs[i*3], y = R[1] + offs[i*3+1], z = R[2] + offs[i*3+2];
                if (parent[i] >= 0 && parent[i] < 14 && ids[parent[i]]) {
                    float* pvm = anm_get_vm(am, ids[parent[i]]);
                    if (pvm) { x += pvm[0x109]; y += pvm[0x10a]; z += pvm[0x10b]; }
                }
                vm[0x10c] = x + 224.0f; vm[0x10d] = y + 16.0f; vm[0x10e] = z;
            }
        } else {
            for (int i = 0; i < 14; i++) {
                if (!ids[i]) continue;
                float* vm = anm_get_vm(am, ids[i]);
                if (!vm) continue;
                vm[0x10c] = R[0]; vm[0x10d] = R[1]; vm[0x10e] = R[2];
            }
        }
    }
}

/* ------------------------------------------------------------------ update runner replacement */
typedef int (__thiscall *NodeFn)(void* arg);
struct ListNode { struct UpdateFunc* entry; struct ListNode* next; struct ListNode* prev; };
struct UpdateFunc { int priority; uint32_t flags; NodeFn func; void* on_reg; NodeFn on_cleanup; struct ListNode node; void* arg; };
_Static_assert(__builtin_offsetof(struct UpdateFunc, arg) == 0x20, "UpdateFunc layout");
typedef void (__fastcall *RemoveNodeFn)(struct UpdateFunc* uf, uint8_t* runner);
static RemoveNodeFn game_remove_node = (RemoveNodeFn)0x462890;

#define CRIT ((LPCRITICAL_SECTION)0x4cf0f8)
#define CRIT_COUNT (*(volatile uint8_t*)0x4cf218)
static inline void crit_enter(void) { if (G_MISC_FLAGS & 0x8000) { EnterCriticalSection(CRIT); CRIT_COUNT++; } }
static inline void crit_leave(void) { if (G_MISC_FLAGS & 0x8000) { LeaveCriticalSection(CRIT); CRIT_COUNT--; } }

static struct UpdateFunc* g_stop_node; /* node that returned "stop" on the last frame tick */
static unsigned g_stat_sub_calls, g_stat_frame_calls, g_stat_long, g_stat_vlong;

int __cdecl __attribute__((used)) hfr_runner(uint8_t* runner) {
    int count = 0;
    if (g_skip_update && runner == G_UPDATE_RUNNER) { enemy_interp(g_phase); return 1; }
    crit_enter();
    if (g_major) g_stop_node = NULL;
    struct ListNode* n = *(struct ListNode**)(runner + 0x18);
restart:
    while (n) {
        struct UpdateFunc* uf = n->entry;
        n = n->next;
        if (!uf->func) continue;
        if (!(uf->flags & 2)) { count++; continue; }
    call_again:
        if (*(int*)(runner + 0x48) != 0) {
            if (uf->on_cleanup) uf->on_cleanup(uf->arg);
            count++; continue;
        }
        int mode = node_mode((uint32_t)uf->func);
        if (mode == MODE_FRAME && !g_major) {
            if (uf == g_stop_node) { count = 1; goto done; } /* the list was cut here on the last frame tick */
            /* GameManager (0x422bd0) returns "stop" while paused (flags 0x10/0x20/0x40); a pause raised by a
               sub-stepped node mid-frame must cut the list immediately, not only at the next frame tick */
            if ((uint32_t)uf->func == 0x422bd0) { uint8_t* gm = *(uint8_t**)0x4b44e8; if (gm && (*(uint32_t*)(gm + 0x60) & 0x70)) { count = 1; goto done; } }
            count++; continue;
        }
        crit_leave();
        set_factor(mode == MODE_SUB ? g_dt : 1.0f);
        if (mode == MODE_SUB) g_stat_sub_calls++; else g_stat_frame_calls++;
        int r = uf->func(uf->arg);
        if ((uint32_t)uf->func == 0x437660) { uint8_t* pl = *(uint8_t**)0x4b4514; if (pl) { g_ptf_prev = g_ptf_cur; g_ptf_cur = *(float*)(pl + 0xa38); } }
        crit_enter();
        switch (r) {
        case 0: game_remove_node(uf, runner); count++; break;
        case 2: if (uf->flags & 2) goto call_again; count++; break;
        case 3: if (g_major) g_stop_node = uf; count = 1; goto done;
        case 4: case 8: count = 0; goto done;
        case 5: count = -1; goto done;
        case 6: n = *(struct ListNode**)(runner + 0x18); count = 0; goto restart;
        case 7: if (uf->on_cleanup) uf->on_cleanup(uf->arg); count++; break;
        default: count++; break;
        }
    }
done:
    crit_leave();
    set_factor(1.0f);
    if (runner == G_UPDATE_RUNNER) enemy_interp(g_phase);
    return count;
}
__asm__(
    ".intel_syntax noprefix\n.globl _hfr_runner_entry\n_hfr_runner_entry:\n"
    "  push ebx\n  call _hfr_runner\n  add esp, 4\n  ret\n"
    ".att_syntax\n"
);
extern void hfr_runner_entry(void);

/* ------------------------------------------------------------------ frame limiter */
static LARGE_INTEGER g_qpf;
static double now_s(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (double)t.QuadPart / (double)g_qpf.QuadPart; }
static double g_next = 0;
static double g_stat_last = 0; static unsigned g_stat_ticks = 0;

static int g_vsync_effective = 1;   /* vsync appears to pace us: no sleeping in the limiter */
static double g_rate_win_start = 0; static unsigned g_rate_win_ticks = 0;
static unsigned g_stat_catchup; static long long g_stat_ticks_run_last; static long long g_ticks_run;

#define G_FRAME_FLAG34   (*(uint32_t*)0x4cee34)
#define G_FRAME_FLAG38   (*(uint32_t*)0x4cee38)
static void call_with_esi(uintptr_t fn, uintptr_t esi) {
    uintptr_t s = esi;
    __asm__ volatile ("call *%1" : "+S"(s) : "r"(fn) : "eax", "ecx", "edx", "memory", "cc");
}
/* an update pass without drawing/presenting (used to catch up after a missed vblank) */
static int update_only_tick(void) {
    G_FRAME_FLAG34 = 0x4cec04;
    G_FRAME_FLAG38 = 1;
    int r = hfr_runner(G_UPDATE_RUNNER);
    if (r == 0)  { call_with_esi(0x464c40, 0x4cf0d8); return 1; }
    if (r == -1) { call_with_esi(0x464c40, 0x4cf0d8); return 2; }
    return 0;
}

static void limiter_stats(double now) {
    g_stat_ticks++;
    { static double last = 0; double period = 1.0 / (double)g_refresh; if (last > 0) { double gap = now - last; if (gap > 1.5 * period) g_stat_long++; if (gap > 3 * period) g_stat_vlong++; } last = now; }
    g_rate_win_ticks++;
    if (g_rate_win_start == 0) g_rate_win_start = now;
    else if (now - g_rate_win_start >= 1.0) {
        double rate = g_rate_win_ticks / (now - g_rate_win_start);
        if (cfg.vsync && g_vsync_effective && rate > g_refresh * 1.08) { g_vsync_effective = 0; LOG("vsync does not seem to pace presentation (%.1f/s) — using software limiter", rate); }
        g_rate_win_start = now; g_rate_win_ticks = 0;
    }
    if (now - g_stat_last >= 5.0) {
        if (g_stat_last > 0)
            LOG("stats: %.2f presents/s (target %d), extra ticks %u, skipped ticks %u, sub calls %u, frame calls %u, logical %.3f, long gaps %u/%u, ticks/s %.2f",
                g_stat_ticks / (now - g_stat_last), g_refresh, g_stat_catchup, g_stat_skipped, g_stat_sub_calls, g_stat_frame_calls, g_logical, g_stat_long, g_stat_vlong,
                (double)(g_ticks_run - g_stat_ticks_run_last) / (now - g_stat_last));
        g_stat_last = now; g_stat_ticks = 0; g_stat_sub_calls = g_stat_frame_calls = g_stat_long = g_stat_vlong = g_stat_catchup = g_stat_skipped = 0; g_stat_ticks_run_last = g_ticks_run;
    }
}

/* ------------------------------------------------------------------ replay awareness */
#define G_REPLAY_MANAGER (*(uint8_t**)0x4b4518)
static int g_replay_rate = 0;        /* logic rate stored in the replay being played (0 = none: stock 60 Hz logic) */
static int g_replay_playing = 0;
static void replay_check(void) {
    uint8_t* rm = G_REPLAY_MANAGER;
    int playing = rm && *(int*)(rm + 0x10) == 1;
    if (playing != g_replay_playing) {
        g_replay_playing = playing;
        int want = playing ? (g_replay_rate ? g_replay_rate : 60) : g_refresh;
        if (cfg.fps > 0 && !playing) want = cfg.fps;
        LOG("replay playback %s -> logic rate %d", playing ? "started" : "ended", want);
        set_logic_rate(want);
    }
}


/* ------------------------------------------------------------------ replay file chunk (recording rate) */
#define HFR_CHUNK_TYPE 0x48   /* 'H' : our USER chunk type */
static void replay_path(char* out, size_t n, const char* name) {
    char dir[MAX_PATH]; GetModuleFileNameA(NULL, dir, MAX_PATH); char* p = strrchr(dir, '\\'); if (p) *p = 0;
    snprintf(out, n, "%s\\replay\\%s", dir, name);
}
static void replay_append_chunk(const char* name) {
    if (!cfg.substep || g_logic_rate == 60) return;
    char path[MAX_PATH]; replay_path(path, sizeof path, name);
    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { LOG("replay chunk: cannot open %s", path); return; }
    uint8_t buf[64]; memset(buf, 0, sizeof buf);
    int len = snprintf((char*)buf + 12, sizeof buf - 12, "th12_hfr rate=%d", g_logic_rate) + 1;
    uint32_t size = (12 + len + 3) & ~3u;
    memcpy(buf, "USER", 4); memcpy(buf + 4, &size, 4); buf[8] = HFR_CHUNK_TYPE;
    DWORD w = 0; WriteFile(h, buf, size, &w, NULL); CloseHandle(h);
    LOG("replay chunk written to %s (rate %d)", name, g_logic_rate);
}
static int replay_read_chunk(const char* name) {
    char path[MAX_PATH]; replay_path(path, sizeof path, name);
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD sz = GetFileSize(h, NULL); int rate = 0;
    if (sz > 0 && sz < 64 * 1024 * 1024) {
        uint8_t* d = (uint8_t*)malloc(sz); DWORD rd = 0;
        if (d && ReadFile(h, d, sz, &rd, NULL) && rd == sz) {
            for (DWORD i = 0; i + 16 <= sz; i++) {
                if (memcmp(d + i, "USER", 4) == 0 && d[i + 8] == HFR_CHUNK_TYPE) {
                    const char* t = (const char*)d + i + 12; const char* k = strstr(t, "rate=");
                    if (k) rate = atoi(k + 5);
                    break;
                }
            }
        }
        free(d);
    }
    CloseHandle(h);
    return rate;
}
typedef void (__fastcall *ReplaySaveFn)(char* filename, char* name, int p3);
typedef int (__stdcall *ReplayLoadFn)(void* mgr, char* filename);
static ReplaySaveFn orig_replay_save = (ReplaySaveFn)0x43bc10;
static ReplayLoadFn orig_replay_load = (ReplayLoadFn)0x43c350;
static void __fastcall hfr_replay_save(char* filename, char* name, int p3) {
    orig_replay_save(filename, name, p3);
    replay_append_chunk(filename);
}
static int __stdcall hfr_replay_load(void* mgr, char* filename) {
    int r = orig_replay_load(mgr, filename);
    g_replay_rate = replay_read_chunk(filename);
    LOG("replay %s loaded for playback: recorded rate %d", filename, g_replay_rate);
    return r;
}

/* ------------------------------------------------------------------ frame hook */

static int __stdcall hfr_frame(void* ctx) {
    double now = now_s();
    replay_check();
    if (g_t0 == 0) { g_t0 = now; g_ticks_run = 0; }
    /* how many ticks we should have run by now (long-term schedule) minus how many we did */
    double expected = (now - g_t0) * (double)g_logic_rate;
    long long deficit = (long long)floor(expected) - g_ticks_run;
    if (deficit > 60 || deficit < -60) {              /* stall / clock jump: re-anchor instead of catching up */
        g_t0 = now - (double)g_ticks_run / (double)g_logic_rate; deficit = 0;
    }
    if (!(cfg.vsync && g_vsync_effective)) {
        /* software pacing: wait until the next tick is due */
        double due = g_t0 + (double)(g_ticks_run + 1) / (double)g_logic_rate;
        while (now < due) {
            double rem = due - now;
            if (rem > 0.0025) Sleep(1); else if (rem > 0.0008) Sleep(0); else YieldProcessor();
            now = now_s();
        }
        expected = (now - g_t0) * (double)g_logic_rate;
        deficit = (long long)floor(expected) - g_ticks_run;
    }
    limiter_stats(now);
    if (cfg.debug) {
        static unsigned dbg = 0;
        if (++dbg % 120 == 0) {
            uint8_t* pause = *(uint8_t**)0x4b4510; uint8_t* veil = *(uint8_t**)0x4ceaac; uint8_t* flash = *(uint8_t**)0x4ceaa8;
            uint8_t* gm = *(uint8_t**)0x4b44e8;
            LOG("dbg: speed=%.4f logical=%.3f factor=%.4f pause=%p state=%d veil.color=%08x flash.color=%08x gm.flags=%08x scene=%d logic=%d skip=%d",
                G_GAME_SPEED, g_logical, g_factor, pause, pause ? *(int*)(pause + 4) : -1,
                veil ? *(uint32_t*)(veil + 0x3bc) : 0, flash ? *(uint32_t*)(flash + 0x3bc) : 0,
                gm ? *(uint32_t*)(gm + 0x60) : 0, *(int*)0x4cee40, g_logic_rate, g_skip_update);
            uint8_t* am = G_ANM_MANAGER;
            if (am) {
                static char line[16384];
                for (int li = 0; li < 2; li++) {
                    int cnt = 0, len = 0; line[0] = 0;
                    for (uint32_t* n = *(uint32_t**)(am + (li ? 0x8856c0 : 0x8856b8)); n && len < 16000; n = (uint32_t*)n[1]) {
                        uint8_t* vm = (uint8_t*)n[0]; cnt++;
                        if (dbg % 720 == 0)
                            len += snprintf(line + len, sizeof line - len, " [%d s%d c%08x t%d ip%d i%d f%08x p%.0f,%.0f sc%.2f,%.2f]", *(int*)vm, *(short*)(vm + 0x3ea),
                                        *(uint32_t*)(vm + 0x3bc), *(int*)(vm + 0x6c), *(int*)(vm + 0x3f0) != 0, *(short*)(vm + 0x3c4), *(uint32_t*)(vm + 0x47c),
                                        *(float*)(vm + 0x430), *(float*)(vm + 0x434), *(float*)(vm + 0x44), *(float*)(vm + 0x48));
                    }
                    LOG("dbg: %s VMs %d:%s", li ? "ui" : "world", cnt, line);
                }
            }
        }
    }
    int n = ticks_for_slot();
    if (deficit > 6) { n += 1; g_stat_catchup++; }         /* presentation is slower than the display rate: catch up (6 = hysteresis for the DWM present queue) */
    else if (deficit < -6 && n > 0) { n -= 1; g_stat_skipped++; } /* presentation is faster: hold a tick back */
    for (int i = 0; i + 1 < n; i++) { advance_tick(); int r = update_only_tick(); if (r) return r; }
    if (n >= 1) { advance_tick(); g_skip_update = 0; } else g_skip_update = 1;
    g_ticks_run += n;
    int r = orig_frame_vsync(ctx);
    g_skip_update = 0;
    return r;
}

/* ------------------------------------------------------------------ Direct3D hooks */
typedef HRESULT (__stdcall *CreateDeviceFn)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
typedef HRESULT (__stdcall *ResetFn)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
typedef IDirect3D9* (__stdcall *Direct3DCreate9Fn)(UINT);
static CreateDeviceFn orig_CreateDevice; static ResetFn orig_Reset; static Direct3DCreate9Fn orig_Direct3DCreate9;

static int detect_refresh(IDirect3DDevice9* dev) {
    int hz = 0;
    if (dev) { D3DDISPLAYMODE m; if (SUCCEEDED(dev->lpVtbl->GetDisplayMode(dev, 0, &m)) && m.RefreshRate > 0) hz = m.RefreshRate; }
    if (hz <= 1) { DEVMODEA dm; memset(&dm, 0, sizeof dm); dm.dmSize = sizeof dm;
        if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1) hz = dm.dmDisplayFrequency; }
    if (hz <= 1) hz = 60;
    return hz;
}
static void apply_pp(D3DPRESENT_PARAMETERS* pp) {
    pp->PresentationInterval = cfg.vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
    if (!pp->Windowed) {
        int hz = cfg.fullscreen_refresh ? cfg.fullscreen_refresh : cfg.fps;
        pp->FullScreen_RefreshRateInHz = hz > 0 ? hz : D3DPRESENT_RATE_DEFAULT;
    }
    LOG("present params: windowed=%d %ux%u refresh=%u interval=0x%x backbuffers=%u", pp->Windowed, pp->BackBufferWidth,
        pp->BackBufferHeight, pp->FullScreen_RefreshRateInHz, pp->PresentationInterval, pp->BackBufferCount);
}
static void after_device(IDirect3DDevice9* dev) {
    int hz = detect_refresh(dev);
    LOG("display refresh detected: %d Hz", hz);
    recompute_rate(cfg.fps > 0 ? cfg.fps : hz);
    g_next = 0;
}
static HRESULT __stdcall hook_Reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
    apply_pp(pp);
    HRESULT hr = orig_Reset(dev, pp);
    LOG("Reset -> 0x%08lx", (long)hr);
    if (SUCCEEDED(hr)) after_device(dev);
    return hr;
}
static HRESULT __stdcall hook_CreateDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND hwnd, DWORD flags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) {
    apply_pp(pp);
    HRESULT hr = orig_CreateDevice(d3d, adapter, type, hwnd, flags, pp, out);
    LOG("CreateDevice -> 0x%08lx", (long)hr);
    if (SUCCEEDED(hr) && out && *out) {
        IDirect3DDevice9* dev = *out;
        void** vt = *(void***)dev;
        if (!orig_Reset) { orig_Reset = (ResetFn)vt[16]; DWORD old; VirtualProtect(&vt[16], 4, PAGE_EXECUTE_READWRITE, &old); vt[16] = (void*)hook_Reset; VirtualProtect(&vt[16], 4, old, &old); }
        after_device(dev);
    }
    return hr;
}
static IDirect3D9* __stdcall hook_Direct3DCreate9(UINT sdk) {
    IDirect3D9* d3d = orig_Direct3DCreate9(sdk);
    if (d3d) {
        void** vt = *(void***)d3d;
        if (!orig_CreateDevice) { orig_CreateDevice = (CreateDeviceFn)vt[16]; DWORD old; VirtualProtect(&vt[16], 4, PAGE_EXECUTE_READWRITE, &old); vt[16] = (void*)hook_CreateDevice; VirtualProtect(&vt[16], 4, old, &old); }
        LOG("Direct3DCreate9 hooked");
    }
    return d3d;
}
static int hook_iat(const char* dll, const char* func, void* hook, void** orig) {
    uint8_t* base = (uint8_t*)GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base; IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR* imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress);
    for (; imp->Name; imp++) {
        if (_stricmp((char*)(base + imp->Name), dll) != 0) continue;
        IMAGE_THUNK_DATA* thunk = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        IMAGE_THUNK_DATA* oth = (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk);
        for (; oth->u1.AddressOfData; thunk++, oth++) {
            if (oth->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            IMAGE_IMPORT_BY_NAME* ibn = (IMAGE_IMPORT_BY_NAME*)(base + oth->u1.AddressOfData);
            if (strcmp((char*)ibn->Name, func) != 0) continue;
            DWORD old; VirtualProtect(&thunk->u1.Function, 4, PAGE_READWRITE, &old);
            *orig = (void*)thunk->u1.Function; thunk->u1.Function = (DWORD)hook;
            VirtualProtect(&thunk->u1.Function, 4, old, &old);
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ init */
static void read_config(void) {
    char ini[MAX_PATH]; GetModuleFileNameA(NULL, ini, MAX_PATH);
    char* p = strrchr(ini, '\\'); if (p) strcpy(p + 1, "th12_hfr.ini"); else strcpy(ini, "th12_hfr.ini");
    cfg.fps = GetPrivateProfileIntA("hfr", "fps", 0, ini);
    cfg.vsync = GetPrivateProfileIntA("hfr", "vsync", 1, ini);
    cfg.substep = GetPrivateProfileIntA("hfr", "substep", 1, ini);
    cfg.log = GetPrivateProfileIntA("hfr", "log", 1, ini);
    cfg.fullscreen_refresh = GetPrivateProfileIntA("hfr", "fullscreen_refresh", 0, ini);
    cfg.enemy_interp = GetPrivateProfileIntA("hfr", "enemy_interp", 1, ini);
    cfg.debug = GetPrivateProfileIntA("hfr", "debug", 0, ini);
    for (size_t i = 0; i < sizeof g_classes / sizeof g_classes[0]; i++) {
        char key[64]; snprintf(key, sizeof key, "sub_%s", g_classes[i].name);
        g_sub_enabled[i] = GetPrivateProfileIntA("systems", key, g_classes[i].mode == MODE_SUB, ini);
    }
}

static int verify_exe(void) {
    /* a few signature checks that both th12.exe and th12e.exe v1.00b satisfy */
    static const uint8_t sig_runner[] = { 0x83, 0xEC, 0x08, 0xF7, 0x05, 0x78, 0xEE, 0x4C, 0x00 };
    static const uint8_t sig_frame[]  = { 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x0C, 0x56, 0x8B, 0x35, 0xCC, 0xE8, 0x4C, 0x00 };
    if (memcmp((void*)0x4624c0, sig_runner, sizeof sig_runner) != 0) return 0;
    if (memcmp((void*)0x450600, sig_frame, sizeof sig_frame) != 0) return 0;
    return 1;
}

static int install(void) {
    QueryPerformanceFrequency(&g_qpf);
    timeBeginPeriod(1);
    if (!verify_exe()) { LOG("executable does not look like th12 v1.00b — no patches applied"); return 0; }

    /* main loop: all three frame variants -> hfr_frame */
    static const uint8_t c1[] = { 0xE8, 0xFA, 0x07, 0x00, 0x00 }, c2[] = { 0xE8, 0x5D, 0x0D, 0x00, 0x00 }, c3[] = { 0xE8, 0x41, 0x0B, 0x00, 0x00 };
    patch_call(0x44f881, (void*)hfr_frame, c1);
    patch_call(0x44f89e, (void*)hfr_frame, c2);
    patch_call(0x44f8aa, (void*)hfr_frame, c3);
    /* update runner */
    patch_jmp(0x4624c0, (void*)hfr_runner_entry, NULL);
    /* FUN_450720: disable the "input latency" sleep (cmp byte [4cead3],1 -> cmp ...,0x7f) */
    { static const uint8_t e[] = { 0x80, 0x3D, 0xD3, 0xEA, 0x4C, 0x00, 0x01 }; uint8_t n[7]; memcpy(n, e, 7); n[6] = 0x7f;
      /* find it inside FUN_450720 */
      for (uintptr_t a = 0x450720; a < 0x450800; a++) if (memcmp((void*)a, e, 7) == 0) { patch_bytes(a, n, 7, e); LOG("latency sleep disabled @%08x", (unsigned)a); break; } }
    install_speed_patches();
    install_site_patches();
    /* replay save (4 call sites, fastcall) and playback load */
    { static const uintptr_t saves[] = { 0x433444, 0x434459, 0x43519b, 0x448e4f };
      for (int i = 0; i < 4; i++) { uint8_t ex[5] = { 0xE8 }; int32_t rel = (int32_t)(0x43bc10 - (saves[i] + 5)); memcpy(ex + 1, &rel, 4); patch_call(saves[i], (void*)hfr_replay_save, ex); }
      { uint8_t ex[5] = { 0xE8, 0x79, 0x11, 0x00, 0x00 }; patch_call(0x43b1d2, (void*)hfr_replay_load, ex); } }
    if (!hook_iat("d3d9.dll", "Direct3DCreate9", (void*)hook_Direct3DCreate9, (void**)&orig_Direct3DCreate9)) LOG("IAT hook for Direct3DCreate9 failed");
    recompute_rate(cfg.fps > 0 ? cfg.fps : detect_refresh(NULL));
    return 1;
}

/* ------------------------------------------------------------------ dinput8.dll proxy mode */
typedef HRESULT (WINAPI *DirectInput8CreateFn)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static DirectInput8CreateFn real_DirectInput8Create;
__declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD ver, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
    if (!real_DirectInput8Create) {
        char path[MAX_PATH]; GetSystemDirectoryA(path, MAX_PATH); strcat(path, "\\dinput8.dll");
        HMODULE m = LoadLibraryA(path);
        if (m) real_DirectInput8Create = (DirectInput8CreateFn)GetProcAddress(m, "DirectInput8Create");
    }
    if (!real_DirectInput8Create) return E_FAIL;
    return real_DirectInput8Create(hinst, ver, riid, out, outer);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID res) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        /* only one instance may patch (the DLL can be loaded as th12_hfr.dll and as dinput8.dll) */
        HANDLE mtx = CreateMutexA(NULL, FALSE, "th12_hfr_single_instance");
        if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) return TRUE;
        read_config();
        if (cfg.log) { char path[MAX_PATH]; GetModuleFileNameA(NULL, path, MAX_PATH); char* p = strrchr(path, '\\'); if (p) strcpy(p + 1, "th12_hfr.log"); g_log = fopen(path, "w"); }
        LOG("th12_hfr loading; fps=%d vsync=%d substep=%d", cfg.fps, cfg.vsync, cfg.substep);
        install();
    }
    return TRUE;
}
