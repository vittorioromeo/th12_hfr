/* TH15 v1.00b, Legacy of Lunatic Kingdom. TH14's engine with one structural change that matters
   here: a timer no longer carries a rate *pointer*. It carries an index into a one-entry table
   at 0x4ca620 whose entry is the game speed, and every tick forces the index to 0 first -- so
   every timer in the game advances by the game speed, and none of them can be given another
   rate by swapping a pointer. docs/games/TH15_DEVNOTES.md has the address map and what each
   hook below is for; the reasoning behind each kind of hook is TH14's (TH14_DEVNOTES 8). */
#include "../game_profile.h"
#include "th14_family.h"

/* The game's own writes to the game speed (0x4e73e8). Twenty-four stores; the twelve that set
   an absolute value are described here and the other twelve compose by themselves, as in TH14. */
static const struct SpeedSite th15_speed_sites[] = {
    {0x4076ff, 10, SPEED_ONE_PERM, SPEED_SRC_NONE},   /* the speed object's initialiser */
    {0x43c08b, 10, SPEED_ONE_PERM, SPEED_SRC_NONE},   /* game state change */
    {0x43c6ca, 10, SPEED_ONE_PERM, SPEED_SRC_NONE},   /* stage set-up */
    {0x43e139, 10, SPEED_ONE_PERM, SPEED_SRC_NONE},   /* stage tear-down */
    {0x44c560, 10, SPEED_ONE_PERM, SPEED_SRC_NONE},   /* supervisor reset */
    {0x454d5a, 10, SPEED_ONE_PERM, SPEED_SRC_NONE},   /* end-of-stage sequence */
    /* 1.0 for the duration of something; the game restores the raw value it saved */
    {0x450f2b, 10, SPEED_ONE_TEMP, SPEED_SRC_NONE},   /* pause menu opens; restored from [menu+0x2e0] */
    {0x4510cc, 10, SPEED_ONE_TEMP, SPEED_SRC_NONE},
    {0x45126e, 10, SPEED_ONE_TEMP, SPEED_SRC_NONE},
    {0x4513f0, 10, SPEED_ONE_TEMP, SPEED_SRC_NONE},
    {0x477e46, 10, SPEED_ONE_TEMP, SPEED_SRC_NONE},   /* a sprite flagged "unaffected by slow-motion" */
    {0x42d768,  8, SPEED_ECL,      SPEED_SRC_XMM0},   /* the script instruction that sets the speed */
};

/* TH15's graze feedback, as two switches (menu: under the dimming sliders; INI: [game]).
   A bullet inside the graze radius is tinted and its sprite is thrown about by two cosmetic
   random offsets every pass for its first 45 frames there (0x419d0f..0x419d8e); and while any
   bullet is that close the player carries a large additive glow, effect.anm script 27, kept
   alive by a ten-frame timer at player+0x1622c that 0x4581d0 re-arms (0x45484f..0x454a62).
   Neither touches the simulation: the graze count and the item slow-down are set before either
   block, the bullets' frame-45 event is left alone, and with the shake off the stub still draws
   the block's two random numbers. */
static int th15_graze_bullets = 1, th15_graze_glow = 1;
static const struct GameToggle th15_toggles[] = {
    { "th15_graze_bullets", "Graze effect: bullets tint and shake",
      "Legacy of Lunatic Kingdom tints the bullets you are grazing and shakes their sprites.\n"
      "Off: they are drawn still, in their own colours. Their hitboxes never moved.", &th15_graze_bullets, 1 },
    { "th15_graze_glow", "Graze effect: glow around the player",
      "The large translucent glow around the player while bullets are within graze range.\n"
      "Grazing, its score and the item slow-down are unchanged either way.", &th15_graze_glow, 1 },
};

/* thiscall in, stdcall out, exactly as TH14's: the saver is stdcall with four arguments and the
   loader takes the manager in ECX and the filename pushed. */
typedef void (__stdcall *Th15ReplaySaveFn)(char*, char*, int, int);
void __stdcall __attribute__((used)) th15_replay_save_c(char* filename, char* name, int p3, int p4) {
    ((Th15ReplaySaveFn)g_game->addr.replay_save)(filename, name, p3, p4);
    replay_append_chunk(filename);
}
int __stdcall __attribute__((used)) th15_replay_load_c(void* mgr, char* filename) {
    restore_replay_settings(); g_replay_playing = 0;
    int r = call_this1(g_game->addr.replay_load, mgr, filename);
    replay_loaded(filename);
    return r;   /* callers test it: zero is success, and anything else abandons the playback */
}
__asm__(".intel_syntax noprefix\n.globl _th15_replay_load_entry\n_th15_replay_load_entry:\n"
        "push dword ptr [esp+4]\npush ecx\ncall _th15_replay_load_c@8\nret 4\n.att_syntax\n");
extern void th15_replay_load_entry(void);

static void th15_install_sites(void) {
    g_p = stub_begin();

    /* --- Bullet (0x419390, ESI = bullet; timer prev +0x1468, integer +0x146c). The wait counter
           [+0x24]-- in the behaviour loop, and the same counter plus the collision countdown
           [+0xc78]-- at the end of the update: integers the update moves itself. --- */
    STUB_BEGIN();
    E_timer_unchanged(R_ESI, 0x1468, 0x146c); EJCC(0x84, 0x419676);
    ECOPY(0x41966b, 11); EJMP(0x419676); site_hook(0x41966b, 11);

    STUB_BEGIN();
    E_timer_unchanged(R_ESI, 0x1468, 0x146c); EJCC(0x84, 0x419a03);
    ECOPY(0x4199e7, 28); EJMP(0x419a03); site_hook(0x4199e7, 28);

    /* --- Bullet state 5: the cancel effect is spawned on the frame the timer's integer *is* 3.
           Keep the game's two branches and add "the integer changed on this tick". --- */
    STUB_BEGIN();
    E(0x8b, 0x86); E32(0x146c);                     /* mov eax,[esi+0x146c] */
    E(0x83, 0xf8, 0x03);                            /* cmp eax,3 */
    EJCC(0x8c, 0x4198cd);                           /* jl  */
    EJCC(0x85, 0x419865);                           /* jne */
    E_timer_unchanged(R_ESI, 0x1468, 0x146c);
    EJCC(0x84, 0x419865);                           /* the same frame again: motion only */
    EJMP(0x419715); site_hook(0x419700, 21);

    /* --- The state 2 -> 1 promotion, asked once a frame at the point stock asks it (TH14's
           0x416877; see TH14_DEVNOTES 13 for what this does and does not settle). --- */
    gate_block(0x419528, 12, 0x4198cd, 0, -1);

    /* Lasers need nothing: TH14's one laser hook gave a rate to a base timer whose rate pointer
       was null, and TH15's timers have no rate pointer to be null. */

    /* --- Items (0x43f830, EDI = item, stride 0xc88; timer prev +0xc3c, integer +0xc40): the
           state-5 despawn countdown and the state-1 "wait, then fall" countdown. --- */
    STUB_BEGIN();
    E_timer_unchanged(R_EDI, 0xc3c, 0xc40); EJCC(0x84, 0x44013f);
    E(0xff, 0x8f); E32(0xc74);                      /* dec dword [edi+0xc74] */
    EJCC(0x89, 0x44013f);                           /* jns: still waiting */
    EJMP(0x43f8af); site_hook(0x43f8a3, 12);

    STUB_BEGIN();
    E(0x8b, 0x87); E32(0xc74);                      /* mov eax,[edi+0xc74] */
    E(0x85, 0xc0);                                  /* test eax,eax */
    EJCC(0x8e, 0x43f8f5);                           /* jle: expired, the normal path every tick */
    E_timer_unchanged(R_EDI, 0xc3c, 0xc40); EJCC(0x84, 0x44013f);
    E(0x48);                                        /* dec eax */
    E(0x89, 0x87); E32(0xc74);                      /* mov [edi+0xc74],eax */
    E(0x85, 0xc0);                                  /* test eax,eax */
    EJCC(0x8f, 0x44013f);                           /* jg: still waiting */
    EJMP(0x43f8e3); site_hook(0x43f8ca, 25);

    /* --- New in TH15: grazing slows the items' fall. The factor at [manager+0xe5def0] is set
           to 0.3 by a graze and the item manager adds a constant back to it once per pass until
           it reaches 1.0 -- a per-frame rate with no speed multiply, so sub-stepped it would
           recover six times as fast. Scale the increment by the sub-step fraction. --- */
    STUB_BEGIN();
    E(0xf3, 0x0f, 0x10, 0x15); E32(0x4cfdf0);       /* movss xmm2,[the increment] */
    E(0xf3, 0x0f, 0x59, 0x15); E32((uint32_t)(uintptr_t)&g_factor);   /* mulss xmm2,[g_factor] */
    E(0xf3, 0x0f, 0x58, 0xc2);                      /* addss xmm0,xmm2 */
    E(0xf3, 0x0f, 0x11, 0x83); E32(0xe5def0);       /* movss [ebx+0xe5def0],xmm0 */
    EJMP(0x44018b); site_hook(0x44017b, 16);

    /* --- Player (0x454a70, EDI = player; life-state timer prev +0x62c, integer +0x630, float
           +0x634). The state dispatch: only state 1, alive, belongs at the display's rate. --- */
    STUB_BEGIN();
    E(0x83, 0xf8, 0x01);                            /* cmp eax,1 */
    E(0x74, 0x0d);                                  /* je: alive, always dispatch */
    E_not_major(); EJCC(0x84, 0x454de9);            /* minor tick: skip to the tail */
    E(0xff, 0x24, 0x85); E32(0x455950);             /* jmp [eax*4 + table] */
    site_hook(0x454a96, 7);

    /* --- Fixed-point position at [+0x624]/[+0x628]: carry the truncation residual. --- */
    movement_cvttss(0x45455b, 0x87, 0x162ac, R_EAX, 1);
    movement_cvttss(0x454563, 0x8f, 0x162a8, R_ECX, 0);

    /* --- Invincibility blink: ask "did the state timer change" of its float across this
           tick's Player call rather than of the prev/int pair. --- */
    STUB_BEGIN();
    E(0x50, 0xa1); E32((uint32_t)(uintptr_t)&g_ptf_prev);
    E(0x3b, 0x05); E32((uint32_t)(uintptr_t)&g_ptf_cur); E(0x58);
    EJCC(0x84, 0x454ffe);
    E(0x8b, 0x87); E32(0x630);                      /* mov eax,[edi+0x630] */
    EJMP(0x454fd6); site_hook(0x454fc8, 14);

    /* --- The focus counter [+0x162cc]++, once per frame. --- */
    gate_block(0x454664, 6, 0x45466a, R_EDI, 0x62c);

    /* --- The options' exponential approach, on frame boundaries only; their sprites are
           interpolated by th15_place_options. --- */
    STUB_BEGIN();
    E_not_major(); EJCC(0x84, 0x4547c2);
    E(0x83, 0xfa, 0x1e);                            /* cmp edx,0x1e */
    EJCC(0x8c, 0x4547c2);
    EJMP(0x4546f2); site_hook(0x4546e9, 9);

    /* --- The shot array's two per-frame rates (shot +0x14 += +0x18, angle +0x1c += +0x20),
           scaled by the sub-step fraction. The loop cursor is EDI here. --- */
    STUB_BEGIN();
    E(0xf3, 0x0f, 0x10, 0x47, 0xa0);                /* movss xmm0,[edi-0x60] */
    E(0xf3, 0x0f, 0x59, 0x05); E32((uint32_t)(uintptr_t)&g_factor);
    E(0x33, 0xc9);                                  /* xor ecx,ecx */
    E(0xf3, 0x0f, 0x58, 0x47, 0x9c);                /* addss xmm0,[edi-0x64] */
    EJMP(0x454e47); site_hook(0x454e3b, 12);

    STUB_BEGIN();
    E(0xf3, 0x0f, 0x10, 0x47, 0xa8);                /* movss xmm0,[edi-0x58] */
    E(0xf3, 0x0f, 0x59, 0x05); E32((uint32_t)(uintptr_t)&g_factor);
    E(0xf3, 0x0f, 0x58, 0x47, 0xa4);                /* addss xmm0,[edi-0x5c] */
    EJMP(0x454e66); site_hook(0x454e5c, 10);

    /* --- The shot's hit-cadence timer: tick it by one whole frame on the boundary tick and not
           at all on the others, so its integer changes on the tick the enemy asks. ECX holds
           the rate pointer at this point; the minor path reloads the float unchanged. --- */
    STUB_BEGIN();
    E(0x8b, 0x47, 0xfc);                            /* mov eax,[edi-4] */
    E(0x89, 0x47, 0xf8);                            /* mov [edi-8],eax */
    E_not_major(); E(0x75, 0x09);                   /* boundary tick: advance */
    E(0xf3, 0x0f, 0x10, 0x07);                      /* movss xmm0,[edi] */
    EJMP(0x454f06);
    E(0xb9); E32((uint32_t)(uintptr_t)&g_logical);  /* mov ecx,&g_logical */
    EJMP(0x454ece); site_hook(0x454ec4, 10);

    /* --- Player shots against enemies (0x4594e0): the state-timer guard. --- */
    STUB_BEGIN();
    E(0x50, 0xa1); E32((uint32_t)(uintptr_t)&g_ptf_prev);
    E(0x3b, 0x05); E32((uint32_t)(uintptr_t)&g_ptf_cur); E(0x58);
    EJCC(0x85, 0x459511);                           /* changed: the game's own jne target */
    EJMP(0x459507); site_hook(0x4594ff, 8);

    /* --- The shot cycles' rewind. `timer_rewind` (0x44b870) multiplies the amount by the
           timer's rate, which sub-stepped is a sixth; a rewind is a discrete reset and takes the
           logical speed. TH14 swaps the timer's rate pointer around the call. TH15's timers have
           no pointer to swap -- but the function has exactly two callers, both in the shot
           cycle driver (0x458e58 rewinds by 14, 0x458e95 by 119), so the rate it looks up is
           replaced inside it. --- */
    STUB_BEGIN();
    E(0xba); E32((uint32_t)(uintptr_t)&g_logical);  /* mov edx,&g_logical */
    EJMP(0x44b896); site_hook(0x44b88f, 7);

    /* --- The weapons' own timer (0x458ed0, EDI = weapon; prev +0xc, integer +0x10, float
           +0x14): one whole frame on the boundary tick, nothing on the others. --- */
    STUB_BEGIN();
    E(0x8b, 0x47, 0x10);                            /* mov eax,[edi+0x10] */
    E(0x89, 0x47, 0x0c);                            /* mov [edi+0xc],eax */
    E_not_major(); E(0x75, 0x05);                   /* boundary tick: advance */
    EJMP(0x45919d);                                 /* minor: store the integer back unchanged */
    E(0xb9); E32((uint32_t)(uintptr_t)&g_logical);
    EJMP(0x459160); site_hook(0x459156, 10);

    /* --- The replay file: two saves, and the two loads that play (0x45bfd3 and 0x467e9f build
           a throwaway manager with 2 in [+0xc] to read a header for the menu, and stay
           unhooked). --- */
    site_call(0x4521d3, th15_replay_save_c);
    site_call(0x46ac14, th15_replay_save_c);
    site_call(0x45b9bc, th15_replay_load_entry);
    site_call(0x45bb46, th15_replay_load_entry);

    /* --- Graze feedback switches (see th15_toggles). --- */
    STUB_BEGIN();
    ECOPY(0x419d0f, 9);                             /* mov eax,[edi+0x1454]; cmp eax,0x2d */
    EJCC(0x8d, 0x419d99);                           /* the game's own jge: frame 45 is gameplay, and stays */
    E(0x83, 0x3d); E32((uint32_t)(uintptr_t)&th15_graze_bullets); E(0x00);   /* cmp dword [switch],0 */
    EJCC(0x85, 0x419d1a);                           /* on: the game's block */
    /* Off: draw the two random numbers the block would have. 0x4e9a40 is the generator replays
       do not restore, but a boss's drawn position follows it (the demo's enemy fingerprint moved
       for 430 frames without them), so the stream is kept exactly as the game leaves it. */
    E(0xb9); E32(0x4e9a40); ECALL(0x403840); E(0xdd, 0xd8);      /* mov ecx,rng; call; fstp st0 */
    E(0xb9); E32(0x4e9a40); ECALL(0x403840); E(0xdd, 0xd8);
    EJMP(0x419d8e); site_hook(0x419d0f, 11);

    STUB_BEGIN();
    E(0x83, 0x3d); E32((uint32_t)(uintptr_t)&th15_graze_glow); E(0x00);      /* cmp dword [switch],0 */
    E(0x75, 0x40);                                  /* jne original */
    E(0x83, 0xbf, 0x28, 0x62, 0x01, 0x00, 0x00);    /* cmp dword [edi+0x16228],0: is the glow's VM alive? */
    E(0x75, 0x23);                                  /* jne kill */
    E(0xc7, 0x87, 0x30, 0x62, 0x01, 0x00); E32(0);              /* no VM: disarm the timer, */
    E(0xc7, 0x87, 0x34, 0x62, 0x01, 0x00); E32(0);
    E(0xc7, 0x87, 0x2c, 0x62, 0x01, 0x00); E32(0xffffffffu);
    EJMP(0x454a62);                                 /* and skip the block */
    E(0xc7, 0x87, 0x30, 0x62, 0x01, 0x00); E32(1);              /* kill: one tick left, so the game's */
    E(0xc7, 0x87, 0x34, 0x62, 0x01, 0x00); E32(0x3f800000u);    /* own code below deletes the VM */
    ECOPY(0x45484f, 7);                             /* original: cmp dword [edi+0x16230],0 */
    EJCC(0x8e, 0x454a62);
    EJMP(0x45485c); site_hook(0x45484f, 13);

    stub_end();
    LOG("TH15 site patches installed (%u bytes of stubs)", (unsigned)g_stub_used);
}

/* Enemy sprites and the player's options, interpolated between 60 Hz positions (TH14_DEVNOTES
   9). The enemy's sub-object is at +0x120c and a VM's position at +0x5ec; a parent VM
   contributes the three floats at +0x38 (0x428a4a). */
static const struct Th14EnemySprites th15_enemy_sprites = { 0x120c, 0x124, 0x164, 0x224, 0x4000000, 0x17b, 0x0e };
static void th15_place_enemy(uint8_t* e, uint8_t* am, uint32_t flags, const float* R) {
    th14_family_place_enemy(&th15_enemy_sprites, e, am, flags, R);
}
static struct Th14OptionState th15_opt[8];
static unsigned th15_opt_frames;
static void th15_place_options(uint8_t* am, float alpha, int capture) {
    th14_family_place_options(0x668, 0x17b, th15_opt, &th15_opt_frames, am, alpha, capture);
}

/* Debug only (replay_trace): a per-frame fingerprint of the bullets and the enemies, as TH14's.
   2001 bullets of 0x1494 at manager+0x98; flags +0x20, position +0xc38, timer integer +0x146c. */
static void th15_trace_state(uint32_t out[3]) {
    uint32_t hb = 2166136261u, he = 2166136261u, live = 0;
#define MIX(h, v) do { (h) ^= (uint32_t)(v); (h) *= 16777619u; } while (0)
    uint8_t* bm = *(uint8_t**)0x4e9a6c;
    if (bm) {
        uint8_t* b = bm + 0x98;
        for (int i = 0; i < 0x7d1; ++i, b += 0x1494) {
            uint32_t f = *(const uint32_t*)(b + 0x20);
            MIX(hb, f);
            if (!f) continue;
            for (int k = 0; k < 3; ++k) MIX(hb, *(const uint32_t*)(b + 0xc38 + k * 4));
            MIX(hb, *(const uint16_t*)(b + 0xc8a));
            MIX(hb, *(const uint32_t*)(b + 0x146c));
            ++live;
        }
    }
    uint8_t* em = *(uint8_t**)0x4e9a80;
    if (em) {
        for (uint32_t* node = *(uint32_t**)(em + 0x180); node; node = (uint32_t*)node[1]) {
            const uint8_t* e = (const uint8_t*)node[0];
            if (!e) continue;
            MIX(he, *(const uint32_t*)(e + 0x526c));
            MIX(he, *(const uint32_t*)(e + 0x1250));
            MIX(he, *(const uint32_t*)(e + 0x1254));
        }
    }
#undef MIX
    out[0] = hb; out[1] = he; out[2] = live;
}

/* Debug only: every dword of each live bullet, for the frames in the replay_trace window. Diff two
   runs; the differing line and column name the field. */
static void th15_trace_dump(void) {
    uint8_t* bm = *(uint8_t**)0x4e9a6c;
    if (!bm) return;
    uint8_t* b = bm + 0x98;
    for (int i = 0; i < 0x7d1; ++i, b += 0x1494) {
        if (!*(const uint32_t*)(b + 0x20)) continue;
        for (uint32_t o = 0; o < 0x1494; o += 0x80) {
            char line[32 * 9 + 1]; int n = 0;
            for (uint32_t k = 0; k < 32 && o + k * 4 < 0x1494; ++k)
                n += snprintf(line + n, sizeof line - (size_t)n, "%08x ", *(const uint32_t*)(b + o + k * 4));
            LOG("bm i=%d +%04x %s", i, o, line);
        }
    }
}

/* Dimming. TH14's map with the draw priorities after the player's moved up by one: enemies are
   enemy.anm layer 8 at 19 (the first world object), the player's band is 27..31, the item
   manager draws at 32 and the bullet manager at 36, both from bullet.anm layer 0. */
static const struct DimRule th15_dim_rules[] = {
    { 32, 32, NULL,          -1, -1, -1, -1, DIM_ITEMS },
    { 36, 36, "bullet.anm",  -1, -1, -1, -1, DIM_NONE },
    { 27, 31, "effect.anm",  13, 15, -1, -1, DIM_NONE },       /* the focus ring and the hitbox */
    { -1, -1, "pl*.anm",     14, 14, -1, -1, DIM_NONE },       /* the player herself */
    { -1, -1, "pl*.anm",     13, 15, -1, -1, DIM_PLAYER_SHOTS },
    { -1, -1, "pl*.anm",     -1, -1, -1, -1, DIM_NONE },
    { -1, -1, "effect.anm",  -1, -1, -1, -1, DIM_EFFECTS },
};

static const struct node_class th15_classes[] = {
    { 0x487b40, MODE_SUB,   "AnmSpritesEarly" },   /* priority 9 */
    { 0x487b10, MODE_SUB,   "AnmSpritesLate"  },   /* priority 34 */
    { 0x41a5b0, MODE_SUB,   "BulletManager"   },   /* 28 */
    { 0x441920, MODE_SUB,   "LaserManager"    },   /* 27 */
    { 0x4559c0, MODE_SUB,   "Player"          },   /* 23 */
    { 0x440870, MODE_SUB,   "ItemManager"     },   /* 29 */
    { 0x44c3f0, MODE_FRAME, "Update01"        },
    { 0x44c310, MODE_FRAME, "Update03"        },
    { 0x40c340, MODE_FRAME, "Ascii"           },
    { 0x460ea0, MODE_FRAME, "Title"           },
    { 0x4503c0, MODE_FRAME, "Update10"        },
    { 0x43d190, MODE_FRAME, "Stage"           },
    { 0x45ce90, MODE_FRAME, "ReplayRecord"    },
    { 0x40f000, MODE_FRAME, "Update17"        },
    { 0x45ef40, MODE_FRAME, "Update21"        },
    { 0x4147c0, MODE_FRAME, "Update25"        },
    { 0x426bd0, MODE_FRAME, "EnemyManager"    },
    { 0x4203e0, MODE_FRAME, "Update31"        },
    { 0x4228c0, MODE_FRAME, "Effects?"        },
    { 0x4374f0, MODE_FRAME, "Front"           },
    { 0x45cea0, MODE_FRAME, "ReplayPlayback"  },   /* latches the recorded input (0x45c150) */
    { 0x45ceb0, MODE_FRAME, "ReplaySpeed"     },   /* fast-forward only */
};

static const struct GameProfile th15_profile = {
    .identity = &game_identities[GI_TH15],
    .addr = {
        .speed = 0x4e73e8,
        .device = 0x4e77d8,
        .window_flags = 0x51bbec,
        .misc_flags = 0x503d86,
        .raw_input = 0x4e6d10,
        /* Sub-tick input, as TH14: one 0x248-byte object, hardware layer at +0, the game's copy
           from +0x218. The record node latches it at 0x45c050; the player reads it at 0x4540ed;
           "hold shot to focus" is option bit 0x200 with a threshold of 10 (0x45c06b). */
        .poll_input = 0x401f50,
        .game_input = 0x4e6f28, .game_pressed = 0x4e6f34, .game_released = 0x4e6f38,
        .option_flags = 0x4e79cc, .autofocus = 0x4e6ea4,
        .raw_pressed = 0x4e6d1c,
        .replay_manager = 0x4e9bc4,
        .replay_save = 0x45c460, .replay_load = 0x45cc80,
        .data_dir = 0x519bdd,            /* "%APPDATA%\\ShanghaiAlice\\th15\\", in the object at 0x519bb0 */
        .player = 0x4e9bb8, .player_callback = 0x4559c0,
        .anm_manager = 0x503c18, .anm_get_vm = 0x488510,
        .enemy_manager = 0x4e9a80,            /* stored by its constructor at 0x426471 */
        .record_callback = 0x45ce90, .playback_callback = 0x45cea0,
        .update_runner = 0x4e9a54,
        .frame_fn = 0x4729c0,
        .remove_node = 0x4018a0,
        .crit = 0x503c28,
        .crit_count = 0x503d78,
        .frame_context_ptr = 0x4e7ec0, .frame_flag = 0x4e7ec4, .frame_context_value = 0x4e7c68,
        .cleanup_fn = 0x403f30, .cleanup_this = 0x4e8170,
        .frame_calls = {0x471a8d, 0x471aab, 0x471ab7},
        .runner_fn = 0x4014f0, .runner_ret = 0x4015fa,
        .latency_cmp = 0x472af0,
        .screenshot_fn = 0x44cbf0, .screenshot_call = 0x472c66,
    },
    .layout = {
        .node_arg = 0x24, .runner_next = 0x50, .runner_ending = 0x54, .input_width = 4, .input_size = 0x248, .autofocus_frames = 10,
        .player_timer = 0x634,
        .enemy_list = 0x180, .enemy_flags = 0x526c, .enemy_position = 0x1250,
        .enemy_skip_mask = 0x2000000,
        /* One dword earlier than TH14 throughout: the mode word is at +0x0c. */
        .replay_mode = 0x0c, .replay_stage = 0x214, .replay_frame = 0x20c, .replay_stages = 0x1c,
        .player_pos = 0x624,
    },
    .critical_flag_mask = 0xff,
    .runner_return8_ends = 1,
    .runner_arg = RUNNER_ARG_ECX,
    .frame_ctx_ecx = 1,
    .screenshot_stack_arg = 1,
    .cleanup_this_ecx = 1,
    .frame_flag_value = 2,
    .remove_node_abi = REMOVE_NODE_RUNNER_THIS,
    .native_size_cycle = 0,
    .install_sites = th15_install_sites, .place_enemy = th15_place_enemy, .place_options = th15_place_options,
    .trace_state = th15_trace_state, .trace_dump = th15_trace_dump,
    .anm_get_vm_ecx = 1,
    .speed_sites = th15_speed_sites, .speed_site_count = sizeof th15_speed_sites / sizeof *th15_speed_sites,
    .toggles = th15_toggles, .toggle_count = sizeof th15_toggles / sizeof *th15_toggles,
    .classes = th15_classes, .class_count = sizeof th15_classes / sizeof *th15_classes,
    .draw = { .dispatch = 0x40168a, .dispatch_len = 8, .node_reg = R_EDI, .prio_off = 0,
              .flush_fn = 0x47e3f0, .flush_reg = R_ECX, .flush_this = 0x503c18,
              .vm_draw = 0x4817d0, .vm_draw_len = 9, .vm_stack_arg = 1,
              .vm_anm_off = 0, .vm_layer_off = 0x24, .vm_script_off = 0,
              .vm_slot_off = 0x28, .anm_table_off = 0x187f4d8, .anm_slots = 32,
              .world_prio = 19, .rules = th15_dim_rules,
              .rule_count = sizeof th15_dim_rules / sizeof *th15_dim_rules },
    .d3dx = "d3dx9_43.dll",
};
