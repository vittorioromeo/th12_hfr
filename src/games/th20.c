/* Touhou 20 -- Fossilized Wonders, v1.00c. docs/games/TH20_DEVNOTES.md.

   Two things set this engine apart from TH10-15. The executable is relocatable, so every
   literal address below goes through HFR_VA() (the profile's own tables are moved by
   relocate_profile). And it is an unoptimised build: the update list is a container walked
   with registered iterators, which cannot be walked safely from outside, so the game keeps its
   runner and only the node call is taken (GameProfile.runner_wrap). */
#define V(a) HFR_VA(a)

/* One update pass with no frame drawn behind it: what the frame function does before it
   draws. Returns non-zero when the game asked to exit. */
static int th20_update_only(void) {
    call_this0(V(0x4455c0), *(void**)V(0x5c0028));
    call_this1(V(0x41dce0), (void*)V(0x5c4d40), (void*)2);
    int r = call_this0(V(0x412810), *(void**)V(0x5b66d8));
    if (r == 0)  { call_this0(V(0x4d9e30), (void*)V(0x5c4d40)); return 1; }
    if (r == -1) { call_this0(V(0x4d9e30), (void*)V(0x5c4d40)); return 2; }
    return 0;
}

/* Float::set (0x4292a0) called on the game speed: the value is the new logical speed. The
   sprite script runner's own temporary writes are not routed here (th20_install_sites). */
static __attribute__((thiscall, used)) void th20_speed_set(float* speed, float v) { g_logical = v; *speed = v * g_factor; }
/* ... and the same call where the value is a temporary one that the caller will undo. */
static __attribute__((thiscall, used)) void th20_speed_temp(float* speed, float v) { *speed = v * g_factor; }

/* Replay save and load: thiscall on the manager; four arguments and one. */
typedef __attribute__((thiscall)) void (*Th20SaveFn)(void*, char*, char*, int, int);
typedef __attribute__((thiscall)) int  (*Th20LoadFn)(void*, char*);
static __attribute__((thiscall, used)) void th20_replay_save(void* mgr, char* filename, char* name, int p3, int p4) {
    ((Th20SaveFn)g_game->addr.replay_save)(mgr, filename, name, p3, p4);
    replay_append_chunk(filename);
}
static __attribute__((thiscall, used)) int th20_replay_load(void* mgr, char* filename) {
    restore_replay_settings(); g_replay_playing = 0;
    int r = ((Th20LoadFn)g_game->addr.replay_load)(mgr, filename);
    replay_loaded(filename);
    return r;
}

/* Timer::operator% (0x472040) where the game asks "is this the Nth frame": true on every tick
   of that frame when the timer is sub-stepped, so it is answered on the frame's first tick
   only. At one tick a frame that is every tick, and the game's own slow-motion repeats as it
   always did. Only for callers that test the result against zero. */
static __attribute__((thiscall, used)) int th20_timer_every(const int32_t* timer, int n) {
    return g_major ? timer[1] % n : 1;
}

/* Timer::operator+= and -= (0x45c300, 0x4c9170, 0x535770) multiply what they add by the game
   speed, like the tick they are built on. Where the amount is a rate that is right. Where it
   is a jump -- the shot timer wrapped by its period, a curvy laser's timer rewound by the
   nodes it dropped, the cost of a stone taken off its gauge -- a sub-stepped caller would move
   the timer by 1/k of the jump: the player's 14-frame shot cycle wraps from 14 to 7 and she
   fires twice as often. (The game's own slow-motion has the same flaw; that is left as it
   is.) So at these sites the sub-step factor is kept out of the amount. */
static void th20_timer_jump(int32_t* timer, float delta) {
    float s = (g_logical > 0.99f && g_logical < 1.01f) ? 1.0f : g_logical;
    float f = ((float*)timer)[2] + delta * s;
    timer[0] = timer[1]; ((float*)timer)[2] = f; timer[1] = (int32_t)f;
}
typedef __attribute__((thiscall)) void (*Th20TimerInt)(int32_t*, int);
typedef __attribute__((thiscall)) void (*Th20TimerFloat)(int32_t*, float);
static __attribute__((thiscall, used)) void th20_timer_sub_int(int32_t* timer, int n) {
    if (g_factor == 1.0f) ((Th20TimerInt)HFR_VA(0x4c9170))(timer, n); else th20_timer_jump(timer, -(float)n);
}
/* ... and where the jump stands in for the tick -- "if (timer >= 14) timer -= 14; else
   timer++" -- the wrapping call is a whole frame in which the timer did not advance, but only
   one tick of k. The k-1 ticks that follow would put the cycle (k-1)/k of a frame ahead, so
   the jump takes those back as well. */
static __attribute__((thiscall, used)) void th20_timer_wrap(int32_t* timer, int n) {
    if (g_factor == 1.0f) ((Th20TimerInt)HFR_VA(0x4c9170))(timer, n); else th20_timer_jump(timer, -(float)n - (1.0f - g_factor));
}
static __attribute__((thiscall, used)) void th20_timer_add_int(int32_t* timer, int n) {
    if (g_factor == 1.0f) ((Th20TimerInt)HFR_VA(0x45c300))(timer, n); else th20_timer_jump(timer, (float)n);
}
static __attribute__((thiscall, used)) void th20_timer_sub_float(int32_t* timer, float v) {
    if (g_factor == 1.0f) ((Th20TimerFloat)HFR_VA(0x535770))(timer, v); else th20_timer_jump(timer, -v);
}

/* "Did the player's state timer move on this frame?" (0x4c0ce0) -- the game's own slow-motion
   gate on damage: an enemy takes none from her shots in a call where the integer at
   [player+0x648] did not change. Enemies run once a frame, on its first tick, and a sub-stepped
   player has by then advanced her timer by 1/k: never changed at k = 2, and no enemy can be
   hurt. The question is answered over the whole frame instead: the integer now against the
   integer at the same point of the previous frame (th20_node takes both, after her update on
   a frame's first tick). */
static int32_t th20_ptimer_now, th20_ptimer_before;
typedef __attribute__((thiscall)) int (*Th20PlayerTicked)(void*);
static __attribute__((thiscall, used)) int th20_player_ticked(void* player) {
    if (g_dt == 1.0f) return ((Th20PlayerTicked)HFR_VA(0x4c0ce0))(player);    /* g_factor is 1 here: the caller is a per-frame node */
    return th20_ptimer_now != th20_ptimer_before;
}

/* The options. Each one closes 30% of the distance to its place once a call, in integers
   (0x4fab38..0x4fac2a), which is a per-frame rate and runs on the frame's first tick only. In
   between, the option is drawn -- and fires -- where it would be if it were carried along with
   her: its frame position plus what she has moved since that tick. For steady movement that is
   exactly where the approach puts it a frame later, so nothing jumps on the next first tick.
   Called after the option's integer position has been converted to the float vector the two
   sprites and [option+0x10] are given. */
static int32_t th20_player_at_major[2];
void __cdecl __attribute__((used)) th20_option_display(const uint8_t* player, float* v) {
    const int32_t* pos = (const int32_t*)(player + 0x620);
    if (g_major) { th20_player_at_major[0] = pos[0]; th20_player_at_major[1] = pos[1]; return; }
    if (g_factor == 1.0f) return;
    v[0] += (float)(pos[0] - th20_player_at_major[0]) / 128.0f;
    v[1] += (float)(pos[1] - th20_player_at_major[1]) / 128.0f;
}

/* Sub-tick input. The poll (0x41fe80, `this` = the input manager at [0x5b8898]) reads every
   device, backs the four 0x2c0-byte input objects at 0x5b88b0 up to 0x5b93b0, moves each raw
   word to "previous", rebuilds the hold counters and counts a frame -- none of which may
   happen between frames. The keyboard is GetKeyboardState and the pads are polled state, so
   reading them again loses nothing; everything the routine writes is put back afterwards:
   the objects, their backups, and the manager up to the end of its devices (their key
   states are inside it, 0x3d4 bytes each from +0x20, count at +0x14). */
typedef __attribute__((thiscall)) int (*Th20Poll)(void*);
static uint32_t th20_poll_raw(void) {
    static uint8_t objects[2 * 4 * 0x2c0], manager[0x2e40];
    uint8_t* mgr = *(uint8_t**)HFR_VA(0x5b8898);
    if (!mgr || !mem_readable(mgr, sizeof manager)) return 0;
    uint8_t* obj = (uint8_t*)HFR_VA(0x5b88b0); uint8_t* bak = (uint8_t*)HFR_VA(0x5b93b0);
    memcpy(objects, obj, 4 * 0x2c0); memcpy(objects + 4 * 0x2c0, bak, 4 * 0x2c0); memcpy(manager, mgr, sizeof manager);
    ((Th20Poll)HFR_VA(0x41fe80))(mgr);
    uint32_t v = *(uint32_t*)obj & 0xffff;               /* what the replay node would take (0x507fc0) */
    memcpy(obj, objects, 4 * 0x2c0); memcpy(bak, objects + 4 * 0x2c0, 4 * 0x2c0); memcpy(mgr, manager, sizeof manager);
    return v;
}

extern int  __cdecl hfr_wrap_begin(void);
extern int  __cdecl hfr_wrap_node(uint32_t fn, void* arg);
extern void __cdecl hfr_wrap_end(void);

/* One "side" of the game keeps its managers in a table at 0x5ba568: bullets, the player, enemies,
   items, (+0x10), lasers. */
#define TH20_BULLETS (*(uint8_t**)HFR_VA(0x5ba568))
#define TH20_ITEMS   (*(uint8_t**)HFR_VA(0x5ba574))
int __cdecl __attribute__((used)) th20_node(uint32_t fn, void* arg) {
    uint32_t f = fn - (uint32_t)(g_image_base - HFR_PREFERRED_BASE);
    int r = hfr_wrap_node(fn, arg);
    if (f == 0x4feda0 && g_major && !g_skip_update) {
        const uint8_t* pl = *(uint8_t* const*)g_game->addr.player;
        if (pl) { th20_ptimer_before = th20_ptimer_now; th20_ptimer_now = *(const int32_t*)(pl + 0x648); }
    }
    return r;
}

/* Debug only (replay_trace): what a sub-stepped playback is compared on. Float state is never
   bit-equal between one step and k part-steps, so this counts things and sums positions
   coarsely rather than hashing bits: out[0] bullets' quantised position sum, out[1] items
   (count in the high half) and lasers, out[2] live bullets. The bullet list is the container at
   manager+0x286d64: head node at +4, a node is {bullet, next}; position at bullet+0x64. */
static void th20_trace_state(uint32_t out[3]) {
    uint32_t n = 0, sum = 0;
    if (TH20_BULLETS && mem_readable(TH20_BULLETS + 0x286d64, 8)) {
        for (uint32_t* node = *(uint32_t**)(TH20_BULLETS + 0x286d64 + 4), walked = 0; node && walked < 8000; node = (uint32_t*)node[1], ++walked) {
            if (!mem_readable(node, 8)) break;                    /* the lists are rebuilt while a stage loads */
            const uint8_t* b = (const uint8_t*)node[0];
            if (!b) continue;
            if (!mem_readable(b + 0x64, 8)) break;
            const float* p = (const float*)(b + 0x64);
            sum += (uint32_t)(int)(p[0] * 0.25f) * 31u + (uint32_t)(int)(p[1] * 0.25f);
            ++n;
        }
    }
    /* Items: the same container at manager+0x49c818; state at item+0xc24 (0 is free),
       position at item+0xbe4. */
    uint32_t items = 0, isum = 0;
    if (TH20_ITEMS && mem_readable(TH20_ITEMS + 0x49c818, 8)) {
        for (uint32_t* node = *(uint32_t**)(TH20_ITEMS + 0x49c818 + 4), walked = 0; node && walked < 8000; node = (uint32_t*)node[1], ++walked) {
            if (!mem_readable(node, 8)) break;
            const uint8_t* it = (const uint8_t*)node[0];
            if (!it) continue;
            if (!mem_readable(it + 0xbe4, 0x44)) break;
            if (!*(const uint32_t*)(it + 0xc24)) continue;
            const float* p = (const float*)(it + 0xbe4);
            isum += (uint32_t)(int)(p[0] * 0.125f) * 31u + (uint32_t)(int)(p[1] * 0.125f);
            ++items;
        }
    }
    items = items << 16 | (isum & 0xffff);
    out[0] = sum; out[1] = items; out[2] = n;
}

static void th20_install_sites(void) {
    g_p = stub_begin();

    /* --- The update runner (0x412810, `this` in ECX). Entry: a skipped pass returns 1 at once. --- */
    STUB_BEGIN();
    E(0x51); ECALL((uintptr_t)hfr_wrap_begin); E(0x59);      /* push ecx; call; pop ecx */
    E(0x85, 0xc0, 0x74, 0x06);                               /* test eax,eax; jz run */
    E(0xb8); E32(1); E(0xc3);                                /* mov eax,1; ret */
    ECOPY(V(0x412810), 5); EJMP(V(0x412815)); site_hook(V(0x412810), 5);

    /* The node call. Nodes are cdecl with one argument; fn is at [ebp-0x38], arg at [ebp-0x34]. */
    STUB_BEGIN();
    E(0xff, 0x75, 0xcc, 0xff, 0x75, 0xc8);                   /* push [ebp-0x34]; push [ebp-0x38] */
    ECALL((uintptr_t)th20_node); E(0x83, 0xc4, 0x08);    /* call; add esp,8 */
    EJMP(V(0x41291d)); site_hook(V(0x412913), 10);

    /* The exit every path takes. */
    STUB_BEGIN();
    ECALL((uintptr_t)hfr_wrap_end);
    ECOPY(V(0x412a4b), 7); EJMP(V(0x412a52)); site_hook(V(0x412a4b), 7);

    /* --- The frame function's own 60 Hz limiter. It returns early while its next deadline is in
           the future and sleeps a millisecond when that is more than a little way off. The
           scheduler paces the frame now, so: never sleep, never return early. The loop that
           moves the deadline on stays, and keeps it just ahead of the clock. --- */
    { static const uint8_t jmp = 0xeb;  patch_bytes(V(0x419e64), &jmp, 1, site_expected(V(0x419e64), 1)); }
    { static const uint8_t nops[6] = {0x90,0x90,0x90,0x90,0x90,0x90}; patch_bytes(V(0x419e85), nops, 6, site_expected(V(0x419e85), 6)); }

    /* --- The game speed. --- */
    static const uintptr_t speed_sets[] = {
        0x4958b8,                                                                /* the script instruction */
        0x4b9f60, 0x4bae05, 0x4dd61b,                                            /* game.cpp, mother.cpp: 1.0 */
        0x4e4a82, 0x4e58e3, 0x4e5931, 0x4e5b68, 0x4e5d2a, 0x4e5eaa, 0x4e601d,   /* pause: 1.0, and its restores */
        0x4f7dee,                                                                /* player.cpp: 1.0 */
    };
    for (size_t i = 0; i < sizeof speed_sets / sizeof *speed_sets; ++i) site_call(V(speed_sets[i]), th20_speed_set);
    /* Bullet::update (0x485b60) saves the speed, runs the bullet at a speed of its own -- the
       product of two per-bullet factors, or 1.0 -- and puts the saved value back. The values it
       sets know nothing of the sub-step factor, so they are multiplied by it; the logical speed
       is not theirs to change, and the restores (0x4861ff, 0x486271, 0x48628b) write back what
       was read and are left alone. enemy.cpp's 0x4a8760 clamps the speed it read and restores
       it, which is already right and is not hooked. */
    static const uintptr_t speed_temps[] = { 0x485c29, 0x485d42, 0x48608c, 0x48612f };
    for (size_t i = 0; i < sizeof speed_temps / sizeof *speed_temps; ++i) site_call(V(speed_temps[i]), th20_speed_temp);
    /* A sprite flagged immune to slow-down runs its script with the speed forced to 1.0 and put
       back afterwards. Sub-stepped, "full speed" is the sub-step factor. */
    { uint8_t b[8]; memcpy(b, site_expected(V(0x42b628), 8), 8); uint32_t f = (uint32_t)(uintptr_t)&g_factor; memcpy(b + 4, &f, 4);
      patch_bytes(V(0x42b628), b, 8, site_expected(V(0x42b628), 8)); }

    /* --- Bullets (sub-stepped). Two per-call countdowns at the end of Bullet::update. --- */
    gate_block(V(0x48620c), 7, V(0x48623c), 0, -1);
    /* A bullet still in its spawn animation (state 2) moves by velocity / 2.0 a call, whatever
       the game speed. The divisor is loaded at 0x485cd1; it becomes 2.0 / factor. */
    STUB_BEGIN(); ECOPY(V(0x485cd1), 8);
    E(0xf3, 0x0f, 0x5e, 0x05); E32((uint32_t)(uintptr_t)&g_factor);              /* divss xmm0, [g_factor] */
    EJMP(V(0x485cd9)); site_hook(V(0x485cd1), 8);

    /* --- Lasers (sub-stepped). Each of the three laser kinds counts [laser+0x6cc] down once a
           call, and grazes on every 8th frame of its timer. --- */
    gate_block(V(0x4d6eb0), 10, V(0x4d6ed1), 0, -1);
    gate_block(V(0x4d737d), 10, V(0x4d739e), 0, -1);
    gate_block(V(0x4d799d), 10, V(0x4d79be), 0, -1);
    site_call(V(0x4d2970), th20_timer_every);
    site_call(V(0x4d2c21), th20_timer_every);
    site_call(V(0x4d2fdb), th20_timer_every);

    /* --- The player (sub-stepped). Player::move (0x4ffa10) truncates the speed-scaled float
           velocity into the fixed-point step it adds to the position; the fraction is carried. --- */
    movement_cvttss(V(0x4ffaa2), 0x91, 0x20c4, R_EDX, 0);
    movement_cvttss(V(0x4ffab6), 0x91, 0x20c8, R_EDX, 1);
    /* Player::update (0x4f7430) switches on the life state. Only state 1, alive, belongs at
       the display's rate; the others (spawning, dying, the deathbomb window...) do per-call
       work and run on the frame's first tick. The tail at 0x4f8093 is timers, and always runs. */
    STUB_BEGIN();
    E(0x83, 0xf9, 0x01);                                          /* cmp ecx,1 */
    { uint8_t* je = g_p; E(0x74, 0x00);
      E_not_major(); EJCC(0x84, V(0x4f8093));
      je[1] = (uint8_t)(g_p - (je + 2)); }
    E(0xff, 0x24, 0x8d); E32((uint32_t)V(0x4f83fc));              /* jmp [ecx*4 + table] */
    site_hook(V(0x4f748b), 7);
    /* The position history the options trail along: shifted once a call while she moves. */
    gate_block(V(0x4fa66d), 7, V(0x4fa762), 0, -1);
    /* The focus counter [+0x20e8]++. */
    gate_block(V(0x4fa762), 6, V(0x4fa784), 0, -1);
    /* An option's approach to its place: 30% of the way a call, in integers, and the one-call
       countdown [option+0xfc] that makes it snap instead. */
    gate_block(V(0x4fab38), 10, V(0x4fac2a), 0, -1);
    STUB_BEGIN();
    E(0x8d, 0x45, 0xe8, 0x50);                                    /* lea eax,[ebp-0x18]; push eax */
    E(0xff, 0x75, 0xe4);                                          /* push [ebp-0x1c]: the player */
    ECALL((uintptr_t)th20_option_display); E(0x83, 0xc4, 0x08);
    ECOPY(V(0x4fac3f), 9); EJMP(V(0x4fac48)); site_hook(V(0x4fac3f), 9);
    site_call(V(0x4c04bf), th20_player_ticked);
    /* Timer jumps (th20_timer_jump). */
    site_call(V(0x505c5b), th20_timer_wrap);         /* the shot timer, -= 14 */
    site_call(V(0x505d23), th20_timer_wrap);         /* the second shot timer, -= 119 */
    /* A shot's per-call turn and acceleration (PlayerShot::update, 0x504480: angle += [def+0x18],
       speed += [def+0x20]), scaled by the sub-step fraction. */
    STUB_BEGIN(); ECOPY(V(0x504547), 5); E(0xf3, 0x0f, 0x59, 0x05); E32((uint32_t)(uintptr_t)&g_factor);
    EJMP(V(0x50454c)); site_hook(V(0x504547), 5);
    STUB_BEGIN(); ECOPY(V(0x504566), 5); E(0xf3, 0x0f, 0x59, 0x05); E32((uint32_t)(uintptr_t)&g_factor);
    EJMP(V(0x50456b)); site_hook(V(0x504566), 5);
    site_call(V(0x505fb1), th20_timer_add_int);      /* a shot's own timer, += 1 on a hit */
    site_call(V(0x534fb5), th20_timer_sub_float);    /* a stone's gauge, -= its cost */
    site_call(V(0x4ce008), th20_timer_sub_int);      /* laser.cpp: a curvy laser's timer, -= nodes dropped */
    /* weapon*.cpp: a stone's bullet-eating field. Each call counts [field+0x58] down and, at
       zero, cancels the bullets in reach and waits two frames for each one it ate. */
    gate_block(V(0x53645d), 7, V(0x536527), 0, -1);
    gate_block(V(0x53674f), 7, V(0x5367b8), 0, -1);
    gate_block(V(0x53790d), 7, V(0x5379d7), 0, -1);
    gate_block(V(0x537bdf), 7, V(0x537c48), 0, -1);
    /* An effect spawned, with random numbers, on every 30th frame of a player-side timer. */
    site_call(V(0x502c67), th20_timer_every);

    /* --- Replays. --- */
    site_call(V(0x4e3dea), th20_replay_save);
    site_call(V(0x52754f), th20_replay_save);
    site_call(V(0x508882), th20_replay_load);

    stub_end();
    LOG("TH20 site patches installed (%u bytes of stubs)", (unsigned)g_stub_used);
}

/* The draw runner (0x412aa0) walks its list with an iterator at [ebp-0x28]; 0x40f160 gives the
   list node and its first word is the draw node. */
static void th20_emit_draw_node(void) {
    E(0x8b, 0x4d, 0xd8); ECALL(V(0x40f160)); E(0x8b, 0x00);      /* mov ecx,[ebp-0x28]; call; mov eax,[eax] */
}

/* A VM names its ANM by slot ([vm+0x18]); the sprite manager keeps 42 record pointers at
   +0x6000730. A record is {slot, std::string name, ...} and this is a debug build of the
   runtime, whose string starts with an iterator-proxy pointer: the text is at record+8, in
   place while the capacity at record+0x1c is 15 and behind a pointer otherwise. */
static const char* th20_anm_name(const uint8_t* vm) {
    const uint8_t* mgr = *(const uint8_t* const*)HFR_VA(0x5c0028);
    const uint32_t slot = *(const uint32_t*)(vm + 0x18);
    if (!mgr || slot >= 42) return NULL;
    const uint8_t* rec = *(const uint8_t* const*)(mgr + 0x6000730 + slot * 4);
    if (!rec) return NULL;
    return *(const uint32_t*)(rec + 0x1c) > 15 ? *(const char* const*)(rec + 8) : (const char*)(rec + 8);
}

/* The draw table, as the census (debug=1) prints it: the stage at 3, enemy.anm layer 8 at 20
   (the first world object), the player's band 28..34 from pl*.anm layers 13..15, the item
   manager at 35 and the bullet manager at 41, both from bullet.anm, effect.anm in layers of its
   own at 37, 46, 49 and 50, and from 60 up the interface. */
/* Enemies run once a frame and their sprites are placed between frame positions
   (interpolation.c). An enemy's data is at +0x88: its final position at data+0x110, a vector
   of sprite slots at data+0xc (begin and end at +4 and +8 of it; a slot is 0x14 bytes: the VM
   handle, a three-float offset, a parent slot index or -1). EnemyData::update places them at
   0x4a8300: position + offset, plus the three floats at +0x2c of the parent's VM, written to
   the VM's position at +0x5bc; bit 10 of the flags at data+0x2cc leaves them alone. */
typedef __attribute__((thiscall)) uint8_t* (*Th20VmOf)(void* handle);
static void th20_place_enemy(uint8_t* e, uint8_t* am, uint32_t flags, const float* R) {
    (void)am; (void)flags;
    const uint8_t* vec = e + 0x88 + 0xc;
    uint8_t* begin = *(uint8_t* const*)(vec + 4); uint8_t* end = *(uint8_t* const*)(vec + 8);
    if (!begin || end < begin || (size_t)(end - begin) > 64 * 0x14) return;
    const Th20VmOf vm_of = (Th20VmOf)HFR_VA(0x44ced0);
    for (uint8_t* s = begin; s + 0x14 <= end; s += 0x14) {
        uint8_t* vm = vm_of(s);
        if (!vm) continue;
        const float* off = (const float*)(s + 4); const int parent = *(const int*)(s + 0x10);
        float p[3] = { R[0] + off[0], R[1] + off[1], R[2] + off[2] };
        if (parent >= 0 && begin + (size_t)parent * 0x14 + 0x14 <= end) {
            const uint8_t* pvm = vm_of(begin + (size_t)parent * 0x14);
            if (pvm) { const float* pp = (const float*)(pvm + 0x2c); p[0] += pp[0]; p[1] += pp[1]; p[2] += pp[2]; }
        }
        memcpy(vm + 0x5bc, p, sizeof p);
    }
}

static const struct DimRule th20_dim_rules[] = {
    { 19, 19, NULL,          -1, -1, -1, -1, DIM_ITEMS },
    { 35, 35, NULL,          -1, -1, -1, -1, DIM_ITEMS },
    { 41, 41, "bullet.anm",  -1, -1, -1, -1, DIM_NONE },
    { 27, 34, "effect.anm",  -1, -1, -1, -1, DIM_NONE },       /* the focus ring and the hitbox */
    { -1, -1, "pl*.anm",     14, 14, -1, -1, DIM_NONE },       /* the player herself */
    { -1, -1, "pl*.anm",     13, 15, -1, -1, DIM_PLAYER_SHOTS },
    { -1, -1, "pl*.anm",     -1, -1, -1, -1, DIM_NONE },
    { 20, 59, "effect.anm",  -1, -1, -1, -1, DIM_EFFECTS },
};

static const struct node_class th20_classes[] = {
    { 0x4508c0, MODE_SUB,   "SpritesEarly"   },   /* 14  sprtlib.cpp */
    { 0x450880, MODE_SUB,   "SpritesLate"    },   /* 44 */
    { 0x4dc510, MODE_FRAME, "Mother"         },   /* 1   mother.cpp: input, screenshot, the frame's bookkeeping */
    { 0x4d85a0, MODE_FRAME, "Loading"        },   /* 7 */
    { 0x470300, MODE_FRAME, "Ascii"          },   /* 8 */
    { 0x52c1e0, MODE_FRAME, "Title"          },   /* 11 */
    { 0x4e6520, MODE_FRAME, "Pause"          },   /* 15 */
    { 0x4bd0b0, MODE_FRAME, "Game"           },   /* 19  game.cpp */
    { 0x509f60, MODE_FRAME, "ReplayRecord"   },   /* 21 */
    { 0x509f50, MODE_FRAME, "ReplayPlayback" },   /* 21 */
    { 0x510810, MODE_FRAME, "SmallScore"     },   /* 25 */
    { 0x51b1d0, MODE_FRAME, "StoneMenu"      },   /* 27 */
    { 0x534070, MODE_FRAME, "WeaponStone"    },   /* 27 */
    { 0x4c0cb0, MODE_FRAME, "Hit"            },   /* 28 */
    { 0x4feda0, MODE_SUB,   "Player"         },   /* 29 */
    { 0x477fb0, MODE_FRAME, "Bomb"           },   /* 33 */
    { 0x4aa1b0, MODE_FRAME, "Enemy"          },   /* 36 */
    { 0x4d5b10, MODE_SUB,   "Laser"          },   /* 37 */
    { 0x4849d0, MODE_SUB,   "Bullet"         },   /* 38 */
    { 0x4c46f0, MODE_SUB,   "Item"           },   /* 39 */
    { 0x4885d0, MODE_FRAME, "Card"           },   /* 40 */
    { 0x49de90, MODE_FRAME, "Effect"         },   /* 41 */
    { 0x4b7c00, MODE_FRAME, "Front"          },   /* 42 */
    { 0x509f70, MODE_FRAME, "Slowdown"       },   /* 46  replay.cpp's second node: the bullet-count slowdown */
    { 0x4739d0, MODE_FRAME, "Background"     },
};

static const struct GameProfile th20_profile = {
    .identity = &game_identities[GI_TH20],
    .addr = {
        .speed = 0x5aefe4,
        .update_runner = 0x5b66d8,
        .replay_manager = 0x5c60fc, .replay_save = 0x509280, .replay_load = 0x508b90,
        .record_callback = 0x509f60, .playback_callback = 0x509f50,
        .data_dir = 0x5b67e1,
        /* The first of four input objects at 0x5b88b0: raw word +0, held +0x29c, pressed +0x2a8,
           released +0x2ac, the second array of hold counters +0x218 (bit 0, shot: the "hold shot
           to focus" option, bit 0x100 of [0x5c4f8c], asks for 10 frames of it at 0x50807e). */
        .raw_input = 0x5b88b0, .game_input = 0x5b8b4c, .game_pressed = 0x5b8b58, .game_released = 0x5b8b5c,
        .option_flags = 0x5c4f8c, .autofocus = 0x5b8ac8,
        .anm_manager = 0x5c0028, .enemy_manager = 0x5ba570,
        .player = 0x5ba56c,            /* player 0 of the per-player table at 0x5ba568 */
        .player_callback = 0x4feda0,
        .runner_fn = 0x412810,
        .frame_fn = 0x419de0,
        .frame_calls = {0x41eb4f, 0x41eb83, 0x41eba3},
    },
    .layout = { .replay_frame = 0x250, .replay_stage = 0x258, .replay_stages = 0x20, .player_pos = 0x620, .player_timer = 0x64c,
                .input_width = 4, .autofocus_frames = 10, .autofocus_option = 0x100,
                .enemy_list = 0x10c, .enemy_flags = 0x354, .enemy_position = 0x198, .enemy_skip_mask = 0x600 },
    .classes = th20_classes, .class_count = sizeof th20_classes / sizeof *th20_classes,
    .runner_arg = RUNNER_ARG_ECX, .runner_wrap = 1, .speed_sites_own = 1,
    .frame_ctx_ecx = 1, .frame_flag_value = 2, .runner_return8_ends = 1,
    .update_only = th20_update_only, .poll_raw = th20_poll_raw, .place_enemy = th20_place_enemy, .mask_minor_player_edges = 1, .trace_state = th20_trace_state,
    .install_sites = th20_install_sites,
    /* Dimming. The dispatch is "mov eax,[ebp-0x40]; push eax; call [ebp-0x44]; add esp,4"; a
       draw node's priority is its first word. The batch flush is 0x4455c0 on the sprite manager
       at [0x5c0028], and one VM is drawn by 0x443880 (the manager in ECX, the VM on the stack). */
    .draw = { .dispatch = 0x412b96, .dispatch_len = 10, .node_reg = 4, .emit_node = th20_emit_draw_node, .prio_off = 0,
              .flush_fn = 0x4455c0, .flush_reg = R_ECX, .flush_this = 0x5c0028,
              .vm_draw = 0x443880, .vm_draw_len = 9, .vm_stack_arg = 1,
              .vm_layer_off = 0x14, .vm_script_off = 0x24, .anm_name = th20_anm_name,
              .world_prio = 20, .rules = th20_dim_rules,
              .rule_count = sizeof th20_dim_rules / sizeof *th20_dim_rules },
    .d3dx = "d3dx9_43.dll",
};
#undef V
