/* TH18 v1.00a -- Unconnected Marketeers. TH15's engine three games on: the same update runner
   (list at +0x18, node argument at +0x24, `this` in ECX), the same replay manager, the same
   input object, timers that hold a rate index. What moved is every address, and what is new
   is the ability cards. docs/games/TH18_DEVNOTES.md is the record. */

/* The game speed (0x4ccbf0). Permanent 1.0 writes are the logical speed; the pause menu's
   and the slowdown-immune sprite's are temporary and restored by the game from what it read;
   the ECL instruction (0x435ea3) calls a setter, `movss [speed],xmm1` (0x43a200), and that
   call is the ECL site -- not the setter: AnmVm::run restores the speed it saved through the
   same setter (0x47b832, 0x47b882), and a setter that took every value as the logical speed
   compounds the sub-step factor into it until the game stands still. The laser manager's 0
   and restore (0x4488a4, 0x4488b8) and the bullet manager's clamp (0x42ff19, 0x42ff5d) write
   back what they read. */
static const struct SpeedSite th18_speed_sites[] = {
    {0x4425b6, 10, SPEED_ONE_PERM, SPEED_SRC_NONE},   /* game state change */
    {0x443311, 10, SPEED_ONE_PERM, SPEED_SRC_NONE},   /* stage set-up */
    {0x4536c0, 10, SPEED_ONE_PERM, SPEED_SRC_NONE},   /* supervisor reset */
    {0x45c30b, 10, SPEED_ONE_PERM, SPEED_SRC_NONE},   /* end-of-stage sequence */
    {0x45876a, 10, SPEED_ONE_TEMP, SPEED_SRC_NONE},   /* pause menu: 1.0, restored from what it saved */
    {0x4589d2, 10, SPEED_ONE_TEMP, SPEED_SRC_NONE},
    {0x458ba9, 10, SPEED_ONE_TEMP, SPEED_SRC_NONE},
    {0x458df9, 10, SPEED_ONE_TEMP, SPEED_SRC_NONE},
    {0x4785b9, 10, SPEED_ONE_TEMP, SPEED_SRC_NONE},   /* a sprite flagged "unaffected by slow-motion" */
    {0x435ea3,  5, SPEED_ECL,      SPEED_SRC_XMM1},   /* the script instruction that sets the speed: its call to the setter */
};

/* Replay save and load, TH15's shapes: the saver is stdcall with four arguments, the loader
   takes the manager in ECX and the filename pushed. */
typedef void (__stdcall *Th18ReplaySaveFn)(char*, char*, int, int);
void __stdcall __attribute__((used)) th18_replay_save_c(char* filename, char* name, int p3, int p4) {
    ((Th18ReplaySaveFn)g_game->addr.replay_save)(filename, name, p3, p4);
    replay_append_chunk(filename);
}
int __stdcall __attribute__((used)) th18_replay_load_c(void* mgr, char* filename) {
    restore_replay_settings(); g_replay_playing = 0;
    int r = call_this1(g_game->addr.replay_load, mgr, filename);
    replay_loaded(filename);
    return r;   /* callers test it: zero is success */
}
__asm__(".intel_syntax noprefix\n.globl _th18_replay_load_entry\n_th18_replay_load_entry:\n"
        "push dword ptr [esp+4]\npush ecx\ncall _th18_replay_load_c@8\nret 4\n.att_syntax\n");
extern void th18_replay_load_entry(void);

/* The catch-up tick: what the frame function (0x472fd0) does around the runner, without the
   draw. Flush the sprite batch, select viewport context 2 on the supervisor, run the pass, and
   run the end-of-pass cleanup on the game's answers 0 and -1 as it does. */
static int th18_update_only(void) {
    call_this0(0x47e730, *(void**)0x51f65c);
    call_this1(0x41b330, (void*)0x4ccdf0, (void*)2);
    int r = hfr_runner(G_UPDATE_RUNNER);
    if (r == 0)  { call_this0(0x402b30, (void*)0x4cd884); return 1; }
    if (r == -1) { call_this0(0x402b30, (void*)0x4cd884); return 2; }
    return 0;
}

/* Debug only (replay_trace): a per-frame fingerprint. The bullet manager at 0x4cf2bc holds
   2001 bullets of 0xfa0 at +0xec; state word +0xf68 (0: free), position +0x638. Float state is
   never bit-equal between one step and k part-steps, so positions are summed coarsely. out[0]
   is the bullets' quantised position sum and out[2] their count; out[1] packs the items'
   position sum (16 bits), the item count (8), the laser count (4) and the player's life state
   (4). A second line, "shots", fingerprints the player's shots: count, quantised position sum,
   the sum of their timers and of their age counters, and the focus counter -- the fields the
   player-side sites govern, so that a 60 Hz run and a sub-stepped one can be compared on them. */
static void th18_trace_state(uint32_t out[3]) {
    uint32_t n = 0, sum = 0, st = 0;
    const uint8_t* bm = *(const uint8_t* const*)0x4cf2bc;
    if (bm) {
        const uint8_t* b = bm + 0xec;
        for (int i = 0; i < 2001; ++i, b += 0xfa0) {
            unsigned s = *(const uint16_t*)(b + 0xf68);
            if (!s) continue;
            const float* p = (const float*)(b + 0x638);
            sum += (uint32_t)(int)(p[0] * 0.25f) * 31u + (uint32_t)(int)(p[1] * 0.25f);
            st += s * 0x101u; ++n;
        }
    }
    const uint8_t* lm = *(const uint8_t* const*)0x4cf3f4;   /* the laser manager: object count at +0x798 */
    /* Items: 0x1258 of 0xc94 at manager+0x14; state +0xc74 (0: free), position +0xc30. */
    uint32_t items = 0, isum = 0;
    const uint8_t* im = *(const uint8_t* const*)0x4cf2ec;
    if (im) {
        const uint8_t* it = im + 0x14;
        for (int i = 0; i < 0x1258; ++i, it += 0xc94) {
            if (!*(const uint32_t*)(it + 0xc74)) continue;
            const float* p = (const float*)(it + 0xc30);
            isum += (uint32_t)(int)(p[0] * 0.125f) * 31u + (uint32_t)(int)(p[1] * 0.125f); ++items;
        }
    }
    (void)st;
    const uint8_t* pl = *(const uint8_t* const*)0x4cf410;   /* the player's life state, +0x476ac, in the top nibble */
    uint32_t ps = pl ? *(const uint32_t*)(pl + 0x476ac) & 0xf : 0xf;
    if (pl) {   /* the player's shots: 0x400 entries of 0x9c at +0x20574, active bit 0, position +0x1c, timer int +0x64, age +0x88 */
        uint32_t sn = 0, ssum = 0, stim = 0, sage = 0; const uint8_t* sh = pl + 0x20574;
        for (int i = 0; i < 0x400; ++i, sh += 0x9c) {
            if (!(*(const uint32_t*)sh & 1)) continue;
            const float* p = (const float*)(sh + 0x1c);
            ssum += (uint32_t)(int)(p[0] * 0.25f) * 31u + (uint32_t)(int)(p[1] * 0.25f); ++sn;
            stim += *(const uint32_t*)(sh + 0x64); sage += *(const uint32_t*)(sh + 0x88);
        }
        LOG("shots n=%u sum=%08x tim=%u age=%d focus=%d", sn, ssum, stim, (int)sage, *(const int*)(pl + 0x477e8));
    }
    out[0] = sum; out[1] = (isum & 0xffff) | (items & 0xff) << 16 | (lm ? (*(const uint32_t*)(lm + 0x798) & 0xf) << 24 : 0) | ps << 28; out[2] = n;
}

static void th18_install_sites(void) {
    g_p = stub_begin();

    /* --- The window loop's third frame path, the inlined automatic-latency one (0x471aa4..),
           is not a call that can be redirected; its branch at 0x471a9e is made unconditional
           so the loop always reaches the two calls the profile hooks. --- */
    STUB_BEGIN(); EJMP(0x471c37); site_hook(0x471a9e, 6);

    /* --- Bullets (0x423e10, ESI = bullet; timer prev +0xf80, integer +0xf84). The two counters
           the update moves itself at its tail -- the wait counter [+0x24] and the collision
           countdown [+0x670] -- once a frame. --- */
    STUB_BEGIN();
    E_timer_unchanged(R_ESI, 0xf80, 0xf84); EJCC(0x84, 0x42486d);
    ECOPY(0x424851, 28); EJMP(0x42486d); site_hook(0x424851, 28);
    /* The state 2 -> 1 promotion, asked once a frame at the point stock asks it (TH15's
       0x419528; TH14_DEVNOTES 13). The block's own `je` is re-emitted rather than copied. */
    STUB_BEGIN();
    E_not_major(); EJCC(0x84, 0x424617);
    ECOPY(0x424007, 7); EJCC(0x84, 0x424617);        /* cmp dword [esi+0x4cc],0; je */
    EJMP(0x424014); site_hook(0x424007, 13);

    /* --- Items (0x445a80, EDI = item; timer prev +0xc4c, integer +0xc50, counter +0xc84): the
           state-5 despawn countdown and the state-1 "wait, then fall" countdown, once a frame. --- */
    STUB_BEGIN();
    E_timer_unchanged(R_EDI, 0xc4c, 0xc50); EJCC(0x84, 0x4467e5);
    ECOPY(0x445af3, 7);                             /* add dword [edi+0xc84],-1 */
    EJCC(0x89, 0x4467e5);                           /* jns: still waiting */
    EJMP(0x445b00); site_hook(0x445af3, 13);

    STUB_BEGIN();
    E(0x8b, 0x87); E32(0xc84);                      /* mov eax,[edi+0xc84] */
    E(0x85, 0xc0);                                  /* test eax,eax */
    EJCC(0x8e, 0x445b9a);                           /* jle: expired, the normal path every tick */
    E_timer_unchanged(R_EDI, 0xc4c, 0xc50); EJCC(0x84, 0x4467e5);
    E(0x48);                                        /* dec eax */
    E(0x89, 0x87); E32(0xc84);                      /* mov [edi+0xc84],eax */
    E(0x85, 0xc0);                                  /* test eax,eax */
    EJCC(0x8f, 0x4467e5);                           /* jg: still waiting */
    EJMP(0x445b91); site_hook(0x445b78, 25);

    /* --- Grazing slows the items' fall (TH15's mechanic): the factor at [manager+0xe6bb14]
           recovers by a constant once per pass. Scaled by the sub-step fraction. --- */
    STUB_BEGIN();
    E(0xf3, 0x0f, 0x10, 0x15); E32(0x4b90d8);       /* movss xmm2,[the increment] */
    E(0xf3, 0x0f, 0x59, 0x15); E32((uint32_t)(uintptr_t)&g_factor);   /* mulss xmm2,[g_factor] */
    E(0xf3, 0x0f, 0x58, 0xc2);                      /* addss xmm0,xmm2 */
    E(0xf3, 0x0f, 0x11, 0x81); E32(0xe6bb14);       /* movss [ecx+0xe6bb14],xmm0 */
    EJMP(0x446829); site_hook(0x446819, 16);

    /* --- Player (0x45be90, EDI = player; life-state timer prev +0x634, integer +0x638, float
           +0x63c; fixed-point position +0x62c/+0x630). The state dispatch: only state 1, alive,
           belongs at the display's rate; the others (spawning, dying, respawning) run their
           scripted sequences once a frame and skip to the tail on the minor ticks. --- */
    STUB_BEGIN();
    E(0x83, 0xf8, 0x01);                            /* cmp eax,1 */
    E(0x74, 0x0d);                                  /* je: alive, always dispatch */
    E_not_major(); EJCC(0x84, 0x45c3d9);            /* minor tick: skip to the tail */
    E(0xff, 0x24, 0x85); E32(0x45ca8c);             /* jmp [eax*4 + table] */
    site_hook(0x45bec3, 7);

    /* --- Movement (0x45b5a0): the velocity, already multiplied by the game speed, is truncated
           from XMM1/XMM2 straight into the fixed-point position. TH15's compiler read the operand
           from memory; this one keeps it in registers, hence the register form of the helper.
           The truncation residual is carried across the ticks. --- */
    movement_cvttss_xmm(0x45b6f7, 8, (const struct CvttssXmm[]){ { R_ECX, 1, 0 }, { R_EDX, 2, 1 } }, 2);

    /* --- Two once-a-frame blocks in the movement routine: the 33-entry position history the
           shot types read (shifted only while moving), and the focus counter [+0x477e8]++. Both
           gated on the player's own state timer. --- */
    gate_block(0x45b89d, 11, 0x45b8c3, R_EDI, 0x634);   /* the loop's set-up; the loop itself stays in place */
    gate_block(0x45b8e4, 6, 0x45b8ea, R_EDI, 0x634);

    /* --- Invincibility blink (in the tail, every state): "did the state timer change" of its
           float across this tick's Player call rather than of the prev/int pair. --- */
    STUB_BEGIN();
    E(0x50, 0xa1); E32((uint32_t)(uintptr_t)&g_ptf_prev);
    E(0x3b, 0x05); E32((uint32_t)(uintptr_t)&g_ptf_cur); E(0x58);
    EJCC(0x84, 0x45c674);
    E(0x8b, 0x87); E32(0x638);                      /* mov eax,[edi+0x638] */
    EJMP(0x45c648); site_hook(0x45c63a, 14);

    /* --- The options' exponential approach, on frame boundaries only; their sprites are
           interpolated by th18_place_options. The counter is EBX here. --- */
    STUB_BEGIN();
    E_not_major(); EJCC(0x84, 0x45bde1);
    E(0x83, 0xfb, 0x1e);                            /* cmp ebx,0x1e */
    EJCC(0x8c, 0x45bde1);
    EJMP(0x45bd38); site_hook(0x45bd2f, 9);

    /* --- The shot loop's head (0x45c402, ECX = the entry, EDI = its timer float at +0x68):
           a shot fired this frame has not moved yet in stock, because the weapons fire after
           the loop. Sub-stepped, the minor ticks that follow the spawn would move it, and
           every shot would fly half a frame ahead of stock for its whole life -- 12 px at the
           shot speed here, seen as a whole frame's difference in when volleys leave the screen.
           The age counter at [+0x88] is still 0 until the next boundary tick's pass, so that is
           the test: on a minor tick a shot with age 0 is skipped like an inactive one. --- */
    STUB_BEGIN();
    E(0xf6, 0x01, 0x01);                            /* test byte [ecx],1 */
    EJCC(0x84, 0x45c510);                           /* inactive */
    E_not_major(); E(0x75, 0x0d);                   /* boundary tick: run it */
    E(0x83, 0xb9); E32(0x88); E(0x00);              /* cmp dword [ecx+0x88],0 */
    EJCC(0x84, 0x45c510);                           /* fired this frame: not yet */
    EJMP(0x45c40b); site_hook(0x45c402, 9);

    /* --- New in TH18: each option (0x45bc90, ESI = option + 0x60) and each weapon (the loop
           at 0x45ee10, EDI = weapon) has a shot-type callback the game calls once a frame --
           the homing aim with its per-call turn limit, the charge ramps, the target search.
           None multiplies by the game speed, so they are called on the boundary tick only. --- */
    STUB_BEGIN();
    E(0x8b, 0x86); E32(0x88);                       /* mov eax,[esi+0x88] */
    E(0x85, 0xc0); EJCC(0x84, 0x45bd1f);            /* none */
    E_not_major(); EJCC(0x84, 0x45bd1f);            /* minor tick: as if none */
    EJMP(0x45bd15); site_hook(0x45bd0b, 10);

    STUB_BEGIN();
    E(0x8b, 0x87); E32(0xd8);                       /* mov eax,[edi+0xd8] */
    E(0x85, 0xc0); EJCC(0x84, 0x45ee37);
    E_not_major(); EJCC(0x84, 0x45ee37);
    EJMP(0x45ee2a); site_hook(0x45ee20, 10);

    /* --- The shot array's two per-frame rates (+0x04 += +0x08, +0x0c += +0x10 of the entry;
           the loop cursor EDI points at the entry's timer float, +0x68), scaled by the sub-step
           fraction. --- */
    STUB_BEGIN();
    E(0xf3, 0x0f, 0x10, 0x47, 0xa0);                /* movss xmm0,[edi-0x60] */
    E(0xf3, 0x0f, 0x59, 0x05); E32((uint32_t)(uintptr_t)&g_factor);
    E(0x33, 0xc9);                                  /* xor ecx,ecx */
    E(0xf3, 0x0f, 0x58, 0x47, 0x9c);                /* addss xmm0,[edi-0x64] */
    EJMP(0x45c427); site_hook(0x45c41b, 12);

    STUB_BEGIN();
    E(0xf3, 0x0f, 0x10, 0x47, 0xa8);                /* movss xmm0,[edi-0x58] */
    E(0xf3, 0x0f, 0x59, 0x05); E32((uint32_t)(uintptr_t)&g_factor);
    E(0xf3, 0x0f, 0x58, 0x47, 0xa4);                /* addss xmm0,[edi-0x5c] */
    EJMP(0x45c446); site_hook(0x45c43c, 10);

    /* --- The shot's hit-cadence timer: one whole frame on the boundary tick and nothing on the
           others, so its integer changes on the tick the enemy asks. ECX holds the rate pointer;
           the minor path reloads the float unchanged. The XMM4 constant load in the block is
           kept on both paths. --- */
    STUB_BEGIN();
    E(0x8b, 0x47, 0xfc);                            /* mov eax,[edi-4] */
    E(0xf3, 0x0f, 0x10, 0x25); E32(0x4b9174);       /* movss xmm4,[0x4b9174] */
    E(0x89, 0x47, 0xf8);                            /* mov [edi-8],eax */
    E_not_major(); E(0x75, 0x09);                   /* boundary tick: advance */
    E(0xf3, 0x0f, 0x10, 0x07);                      /* movss xmm0,[edi] */
    EJMP(0x45c4f3);
    E(0xb9); E32((uint32_t)(uintptr_t)&g_logical);  /* mov ecx,&g_logical */
    EJMP(0x45c4b7); site_hook(0x45c4a5, 18);

    /* --- New in TH18: each shot entry counts its age at [+0x88], decremented after the timer.
           Once a frame. --- */
    STUB_BEGIN();
    E(0x89, 0x47, 0xfc);                            /* mov [edi-4],eax */
    E_not_major(); E(0x74, 0x03);                   /* minor tick: leave it */
    E(0xff, 0x4f, 0x20);                            /* dec dword [edi+0x20] */
    E(0x85, 0xc0);                                  /* test eax,eax */
    EJMP(0x45c507); site_hook(0x45c4ff, 8);

    /* --- Firing (0x45e5d0, EDI = weapon): before the shot type reads the muzzle position
           (+0x48), the routine advances it by one motion step (0x402bf0: position += velocity
           x game speed). A fire is a once-a-frame event, so the step is a whole frame's: the
           call is made with the logical speed in place of the sub-step one. Left alone, every
           shot is born half a frame (12 px) short of where stock puts it and flies there for
           its whole life. --- */
    { uint8_t* st = g_p;
      E(0xff, 0x35); E32(0x4ccbf0);                 /* push dword [speed] */
      E(0xa1); E32((uint32_t)(uintptr_t)&g_logical); E(0xa3); E32(0x4ccbf0);   /* mov [speed],g_logical */
      ECALL(0x402bf0);
      E(0x8f, 0x05); E32(0x4ccbf0); E(0xc3);        /* pop dword [speed]; ret */
      site_call(0x45e6c9, st); }

    /* --- Player shots against enemies (0x45f0c0): the state-timer guard. --- */
    STUB_BEGIN();
    E(0x50, 0xa1); E32((uint32_t)(uintptr_t)&g_ptf_prev);
    E(0x3b, 0x05); E32((uint32_t)(uintptr_t)&g_ptf_cur); E(0x58);
    EJCC(0x85, 0x45f153);                           /* changed: the game's own jne target */
    EJMP(0x45f14c); site_hook(0x45f144, 8);

    /* --- The shot cycles' rewind (`timer_rewind`, 0x452bf0; TH15's 0x44b870): a rewind is a
           discrete reset and takes the logical speed. Two callers, both in the shot cycle driver
           (0x45ece1, 0x45ed3a). --- */
    STUB_BEGIN();
    E(0xba); E32((uint32_t)(uintptr_t)&g_logical);  /* mov edx,&g_logical */
    EJMP(0x452c17); site_hook(0x452c10, 7);

    /* --- The weapons' own timer (the loop at 0x45efc0, EDI = the timer's float; prev -8,
           integer -4, rate index +4): one whole frame on the boundary tick, nothing on the
           others. --- */
    STUB_BEGIN();
    E(0x8b, 0x47, 0xfc);                            /* mov eax,[edi-4] */
    E(0x89, 0x47, 0xf8);                            /* mov [edi-8],eax */
    E_not_major(); E(0x75, 0x09);                   /* boundary tick: advance */
    E(0xf3, 0x0f, 0x10, 0x07);                      /* movss xmm0,[edi] */
    EJMP(0x45f0a3);                                 /* minor: store both back unchanged */
    E(0xb9); E32((uint32_t)(uintptr_t)&g_logical);
    EJMP(0x45f072); site_hook(0x45f068, 10);

    /* --- Replays: two saves, and the two loads that play. 0x461d6b builds a throwaway manager
           with 2 in [+0xc] to read a header for the menu and stays unhooked. --- */
    site_call(0x459ab3, th18_replay_save_c);
    site_call(0x46aa65, th18_replay_save_c);
    site_call(0x461965, th18_replay_load_entry);
    site_call(0x461ab3, th18_replay_load_entry);

    stub_end();
    LOG("TH18 site patches installed (%u bytes of stubs)", (unsigned)g_stub_used);
}

/* Enemy sprites and the player's options, interpolated between 60 Hz positions (TH14_DEVNOTES
   9). TH15's layout moved by 0x20: the enemy's sub-object is at +0x122c (its position +0x44 is
   the enemy's +0x1270, its flags +0x5130 the enemy's +0x635c), the 14 VM ids at +0x124, their
   offsets at +0x164 and parent slots at +0x224 as before (0x42ff80). A VM's position is at
   +0x5f0 and a parent VM contributes the three floats at +0x30. The options are four of 0xf0 at
   +0x670 (0x45bc90), TH15's field layout inside. */
static const struct Th14EnemySprites th18_enemy_sprites = { 0x122c, 0x124, 0x164, 0x224, 0x4000000, 0x17c, 0x0c };
static void th18_place_enemy(uint8_t* e, uint8_t* am, uint32_t flags, const float* R) {
    th14_family_place_enemy(&th18_enemy_sprites, e, am, flags, R);
}
static struct Th14OptionState th18_opt[4];
static unsigned th18_opt_frames;
static void th18_place_options(uint8_t* am, float alpha, int capture) {
    th14_family_place_options(0x670, 0xf0, 4, 0x17c, th18_opt, &th18_opt_frames, am, alpha, capture);
}

/* Dimming. From the draw census of the stage 3 demo (debug=1): the stage background is
   st03wl.anm at 3 and effect.anm layer 2 at 9; a single quad and text.anm layer 35 at 14 and 15
   (0x455610, 0x455530: an overlay gated on 0x4ccf9c); the enemies enemy.anm layers 6, 8, 9 and
   11 at 16, 20, 21 and 24; the player's band is 28..32 (pl00.anm layers 13..15, effect.anm 14
   for the focus ring and the hitbox, ability.anm 13..15 for the card effects around her); the
   item manager draws at 33 and the bullet manager at 38, both from bullet.anm layer 0; effect.anm
   layers 16..21 at 35..47 and the lasers (bullet.anm layers 20 and 21) at 46 and 47; the
   interface from 60. TH15's map with every world priority one or two higher. */
static const struct DimRule th18_dim_rules[] = {
    { 33, 33, NULL,          -1, -1, -1, -1, DIM_ITEMS },
    { 38, 38, "bullet.anm",  -1, -1, -1, -1, DIM_NONE },
    { 46, 47, "bullet.anm",  -1, -1, -1, -1, DIM_NONE },       /* lasers */
    { 28, 32, "effect.anm",  13, 15, -1, -1, DIM_NONE },       /* the focus ring and the hitbox */
    { -1, -1, "pl*.anm",     14, 14, -1, -1, DIM_NONE },       /* the player herself */
    { -1, -1, "pl*.anm",     13, 15, -1, -1, DIM_PLAYER_SHOTS },
    { -1, -1, "pl*.anm",     -1, -1, -1, -1, DIM_NONE },
    { -1, -1, "ability.anm", -1, -1, -1, -1, DIM_NONE },       /* the card effects: left as drawn */
    { -1, -1, "effect.anm",  -1, -1, -1, -1, DIM_EFFECTS },
};

static const struct node_class th18_classes[] = {
    { 0x488250, MODE_SUB,   "AnmSpritesEarly" },   /* priority 11 */
    { 0x488220, MODE_SUB,   "AnmSpritesLate"  },   /* 34 */
    { 0x424e70, MODE_SUB,   "BulletManager"   },   /* 29 */
    { 0x448870, MODE_SUB,   "LaserManager"    },   /* 28 */
    { 0x45caa0, MODE_SUB,   "Player"          },   /* 23 */
    { 0x446ec0, MODE_SUB,   "ItemManager"     },   /* 30 */
    { 0x453460, MODE_FRAME, "Supervisor"      },   /* 1 */
    { 0x475440, MODE_FRAME, "Update02"        },   /* 2 */
    { 0x453380, MODE_FRAME, "Update04"        },   /* 4 */
    { 0x4198b0, MODE_FRAME, "Ascii"           },   /* 5 */
    { 0x465a80, MODE_FRAME, "Title"           },   /* 8 */
    { 0x457be0, MODE_FRAME, "Update12"        },   /* 12 */
    { 0x443d70, MODE_FRAME, "Stage"           },   /* 16 */
    { 0x462940, MODE_FRAME, "ReplayRecord"    },   /* 17 */
    { 0x462a50, MODE_FRAME, "ReplayPlayback"  },   /* 17: latches the recorded input */
    { 0x41ca60, MODE_FRAME, "StageBackground" },   /* 18 */
    { 0x4645d0, MODE_FRAME, "Update21"        },   /* 21 */
    { 0x420040, MODE_FRAME, "Update25"        },   /* 25 */
    { 0x42df50, MODE_FRAME, "EnemyManager"    },   /* 27 */
    { 0x42a160, MODE_FRAME, "Update31"        },   /* 31 */
    { 0x42af30, MODE_FRAME, "Effects"         },   /* 32 */
    { 0x43d6f0, MODE_FRAME, "Front"           },   /* 33 */
    { 0x462c30, MODE_FRAME, "ReplaySpeed"     },   /* 35: fast-forward only */
    { 0x408a90, MODE_FRAME, "AbilityCards"    },   /* 22 */
};

static const struct GameProfile th18_profile = {
    .identity = &game_identities[GI_TH18],
    .addr = {
        .speed = 0x4ccbf0,
        .device = 0x4ccdf8, .pp = 0x4ccee4,
        .window_flags = 0x56ac70,
        .misc_flags = 0x5217be,
        /* One 0x248-byte input object at 0x4ca210; the game's copy from +0x218. The record node
           latches it at 0x46295a; "hold shot to focus" is option bit 0x200, threshold 10
           (0x462982). */
        .raw_input = 0x4ca210, .raw_pressed = 0x4ca21c,
        .poll_input = 0x401c50,
        .game_input = 0x4ca428, .game_pressed = 0x4ca434, .game_released = 0x4ca438,
        .option_flags = 0x4cd014, .autofocus = 0x4ca3a4,
        .replay_manager = 0x4cf418,
        .replay_save = 0x461e90, .replay_load = 0x462680,
        .data_dir = 0x568c61,            /* "%APPDATA%\\ShanghaiAlice\\th18\\", in the object at 0x568c30 */
        .player = 0x4cf410, .player_callback = 0x45caa0,
        .anm_manager = 0x51f65c, .anm_get_vm = 0x488b40,
        .enemy_manager = 0x4cf2d0,
        .record_callback = 0x462940, .playback_callback = 0x462a50,
        .update_runner = 0x4cf294,
        .frame_fn = 0x472fd0,
        .remove_node = 0x4015a0,
        .crit = 0x521660, .crit_count = 0x5217b0,
        .frame_calls = {0x471c4e, 0x471c5a},
        .runner_fn = 0x4012e0, .runner_ret = 0x4013f5,
        .latency_cmp = 0x4730be,
        .screenshot_fn = 0x453f40, .screenshot_call = 0x473367,
    },
    .layout = {
        .node_arg = 0x24, .runner_next = 0x50, .runner_ending = 0x54, .input_width = 4, .input_size = 0x248, .autofocus_frames = 10,
        .replay_mode = 0x0c, .replay_stage = 0x214, .replay_frame = 0x20c, .replay_stages = 0x1c,
        .player_pos = 0x62c,
        .player_timer = 0x63c,
        /* The enemy list's head in the EnemyManager (0x42df99); flags and position per enemy
           (0x42dfc5, 0x45f89f). The skip mask is the bit the manager itself skips its update on. */
        .enemy_list = 0x18c, .enemy_flags = 0x635c, .enemy_position = 0x1270,
        .enemy_skip_mask = 0x2000000,
    },
    .critical_flag_mask = 0xff,
    .runner_return8_ends = 1,
    .runner_arg = RUNNER_ARG_ECX,
    .frame_ctx_ecx = 1,
    .screenshot_stack_arg = 1,
    .cleanup_this_ecx = 1,
    .remove_node_abi = REMOVE_NODE_RUNNER_THIS,
    .install_sites = th18_install_sites, .place_enemy = th18_place_enemy, .place_options = th18_place_options,
    .update_only = th18_update_only, .trace_state = th18_trace_state,
    .anm_get_vm_ecx = 1,
    .speed_sites = th18_speed_sites, .speed_site_count = sizeof th18_speed_sites / sizeof *th18_speed_sites,
    .classes = th18_classes, .class_count = sizeof th18_classes / sizeof *th18_classes,
    .draw = { .dispatch = 0x401490, .dispatch_len = 8, .node_reg = R_EDI, .prio_off = 0,
              .flush_fn = 0x47e730, .flush_reg = R_ECX, .flush_this = 0x51f65c,
              .vm_draw = 0x481210, .vm_draw_len = 9, .vm_stack_arg = 1,
              .vm_layer_off = 0x18, .vm_slot_off = 0x20,
              .anm_table_off = 0x312072c, .anm_slots = 33,
              .world_prio = 16, .rules = th18_dim_rules,
              .rule_count = sizeof th18_dim_rules / sizeof *th18_dim_rules },
    .d3dx = "d3dx9_43.dll",
};
