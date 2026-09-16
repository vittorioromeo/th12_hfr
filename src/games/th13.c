/* TH13 v1.00c. The engine is TH12's with a few structural changes, each absorbed by a
   profile field rather than a fork of the runtime: the UpdateFunc grew a field (its argument
   is at +0x24), the runner keeps the next list node in itself at +0x50 and re-reads it after
   every callback, remove_node takes (runner, node), and the critical section is gated on a
   byte flag (0x4e49ed) instead of misc_flags & 0x8000. The player's state timer moved to
   +0x664/+0x668/+0x66c and the enemy hit test's guard with it.

   Both executables run the same code: th13e.exe is th13.exe plus an appended section that
   loads th13e.dll, so every address below holds for both (identity accepts either image size). */
static const struct SpeedSite th13_speed_sites[] = {
    {0x42ba24, 6, SPEED_ONE_PERM, 1},   /* stage start */
    {0x42bfb5, 6, SPEED_ONE_PERM, 1},   /* game start */
    {0x43a04b, 6, SPEED_ONE_PERM, 1},   /* supervisor reset */
    {0x443586, 6, SPEED_ONE_PERM, 1},   /* player death */
    {0x474f6c, 6, SPEED_ONE_PERM, 1},   /* speed object init */
    {0x40679d, 6, SPEED_ONE_TEMP, 1},   /* stage: around a 3D update, restored after */
    {0x4624c7, 6, SPEED_ONE_TEMP, 1},   /* anm: "unaffected by slow-motion" sprites */
    {0x43e572, 6, SPEED_PAUSE_SET, 1},
    {0x43e6e4, 6, SPEED_PAUSE_SET, 1},
    {0x43f702, 6, SPEED_PAUSE_SET, 1},
    {0x43f843, 6, SPEED_PAUSE_SET, 1},
    {0x43e743, 6, SPEED_PAUSE_RESTORE, 1},
    {0x43f8d9, 6, SPEED_PAUSE_RESTORE, 1},
    {0x440949, 6, SPEED_PAUSE_RESTORE, 1},
    {0x4407ab, 6, SPEED_PAUSE_RESTORE, 1},
    {0x41f24b, 6, SPEED_ECL, 1},        /* ECL instruction setting the game speed */
};

/* The replay loader takes the manager in EBX and the filename in ECX, and returns in EAX.
   The shared hook wants (manager, filename) on the stack, so give it that. */
int __stdcall __attribute__((used)) th13_replay_load_c(void* manager, char* filename) {
    restore_replay_settings(); g_replay_playing = 0;
    int result;
    __asm__ volatile("push %%ebx\n\tmov %%esi, %%ebx\n\tcall *%%edi\n\tpop %%ebx"
        : "=a"(result) : "c"(filename), "S"(manager), "D"((uintptr_t)0x448c40) : "edx", "memory", "cc");
    replay_loaded(filename);
    return result;
}
__asm__(".intel_syntax noprefix\n.globl _th13_replay_load_entry\n_th13_replay_load_entry:\n"
        "push ecx\npush ebx\ncall _th13_replay_load_c@8\nret\n.att_syntax\n");
extern void th13_replay_load_entry(void);

static void th13_install_sites(void) {
    g_p = stub_begin();
    /* Replay playback load (mode 1) at 0x447c1b: the manager arrives in EBX, so the shared
       stdcall hook cannot take the call directly. */
    site_call(0x447c1b, th13_replay_load_entry);
    uint8_t FMUL_SPEED[6] = { 0xD8, 0x0D, 0, 0, 0, 0 };
    { uint32_t a = (uint32_t)(uintptr_t)&g_factor; memcpy(FMUL_SPEED + 2, &a, 4); }
#define FMUL_FACTOR() E(FMUL_SPEED[0], FMUL_SPEED[1], FMUL_SPEED[2], FMUL_SPEED[3], FMUL_SPEED[4], FMUL_SPEED[5])

    /* MotionState (0x4736a0 pre-step, 0x473780 step) needs no hook here, unlike TH12's: the
       pre-step recomputes the velocity from speed and angle every call and multiplies by the
       game speed, which the runtime already sets per tick. Scaling the step too would double
       the motion of every player shot and bullet. */

    /* --- Player's shot array (0x443660 loop in the player update, 0x100 entries of 0x9c bytes
           at +0xaa5c, EDI = entry + 0x68): two per-frame rates applied outside the MotionState,
           [-0x64] += [-0x60] and the angle [-0x5c] += [-0x58]. 16 bytes, then the game's
           angle normalisation. --- */
    STUB_BEGIN();
    E(0xD9, 0x47, 0xA0); FMUL_FACTOR(); E(0xD8, 0x47, 0x9C, 0x51, 0xD9, 0x5F, 0x9C);
    E(0xD9, 0x47, 0xA8); FMUL_FACTOR(); E(0xD8, 0x47, 0xA4);
    EJMP(0x4436a1); site_hook(0x443691, 16);
    /* --- The same array's countdown timer (prev/int/float/speed at -0x8/-0x4/+0/+4 from EDI),
           ticked inline right after (0x4436b4). It is not motion: it is the shot's lifetime and,
           through "int changed this tick && int % interval == 0" in the enemy hit test
           (0x446870), its hit cadence in frames. The enemy code runs on the boundary tick
           only, so the int must change there and nowhere else -- sub-stepped, it changed on
           whichever tick the float happened to cross an integer, and at rates where dt is not
           exact in float32 (360 Hz: 1/6) that was never the boundary tick, so no shot ever
           registered a hit. Tick it by the logical speed on the boundary tick and leave it
           alone on minor ticks (prev = cur, so the guard reads "unchanged"). --- */
    STUB_BEGIN();
    E(0x8B, 0x47, 0xFC, 0x89, 0x47, 0xF8);          /* mov eax,[edi-4]; mov [edi-8],eax */
    E_not_major(); E(0x74, 0x0A);                   /* minor tick: skip the tick */
    E(0xB9); E32((uint32_t)(uintptr_t)&g_logical);  /* mov ecx,&g_logical (the speed the game reads) */
    EJMP(0x4436bd);
    EJMP(0x443702);                                 /* eax = cur; the game stores it back unchanged */
    site_hook(0x4436b4, 9);

    /* --- Enemy hit test guard (0x446870): "player state timer unchanged -> no damage" at
           0x446888 (cmp eax,[esi+0x664]; jne 0x44689b). Same guard as the other games; see
           DEVNOTES_RUNTIME §7 for what it does under sub-stepping. --- */
    STUB_BEGIN(); E(0x50, 0xA1); E32((uint32_t)(uintptr_t)&g_ptf_prev);
    E(0x3B, 0x05); E32((uint32_t)(uintptr_t)&g_ptf_cur); E(0x58);
    EJCC(0x85, 0x44689b); EJMP(0x446890); site_hook(0x446888, 6);

    /* --- Player movement: pos += ftol(vel * speed) in fixed point at 0x442d9b / 0x442dae;
           carry the truncation residual across sub-steps. --- */
    movement_ftol(0x442d9b, 0x4971f0, 0);
    movement_ftol(0x442dae, 0x4971f0, 1);

    /* --- Player: death particles once per frame. 0x44341c: cmp [esi+0x668],3; jne 0x4434d8.
           On a tick where the integer timer did not change, take the jne. --- */
    STUB_BEGIN();
    E(0x83, 0xBE, 0x68, 0x06, 0x00, 0x00, 0x03);   /* cmp [esi+0x668],3 */
    EJCC(0x85, 0x4434d8);                           /* not 3: the game's own jne, taken */
    E_timer_unchanged(R_ESI, 0x664, 0x668);
    EJCC(0x84, 0x4434d8);                           /* unchanged this tick: skip the block */
    E(0x39, 0xF6);                                  /* cmp esi,esi -> ZF=1 for the jne */
    EJMP(0x443423);
    site_hook(0x44341c, 7);

    /* --- Player: option gather counter inc [edi+0x1469c] once per frame (0x442f34) --- */
    STUB_BEGIN(); E_timer_unchanged(R_EDI, 0x664, 0x668); EJCC(0x84, 0x442f3a);
    E(0xFF, 0x87, 0x9C, 0x46, 0x01, 0x00); EJMP(0x442f3a); site_hook(0x442f34, 6);

    /* --- Bullet (0x40db10, EBX = bullet, timer +0x133c/+0x1340): the two per-frame wait
           counters [+0x24]-- and [+0xbac]-- at the end of the update, once per frame --- */
    STUB_BEGIN(); E_timer_unchanged(R_EBX, 0x133c, 0x1340); EJCC(0x84, 0x40e216);
    E(0x8B, 0x43, 0x24, 0x85, 0xC0); EJMP(0x40e1ff); site_hook(0x40e1fa, 5);

    /* --- Items (0x42e380, ESI = item): "+= 0.2 per frame" gravity on the y velocity at +0xbac
           (fadd qword [0x4aebd0]) -> += 0.2 * speed, at both sites --- */
    { static const uintptr_t sites[] = { 0x42e6fc, 0x42e7be };
      for (int i = 0; i < 2; i++) {
          STUB_BEGIN();
          E(0xDD, 0x05, 0xD0, 0xEB, 0x4A, 0x00);        /* fld qword [0x4aebd0] */
          E(0xD8, 0x0D, 0x28, 0x0A, 0x4C, 0x00);        /* fmul dword [speed] */
          E(0xDE, 0xC1);                                /* faddp st(1),st */
          EJMP(sites[i] + 6); site_hook(sites[i], 6);
      } }
    /* --- Items: state-5 countdown [esi+0xbb0]-- once per frame (0x42e3dd; state-5 items do
           not tick their timer, so gate on the frame boundary). The five fstp that follow do not
           touch the flags; "cmp esi,esi" leaves SF clear for the game's jns. --- */
    STUB_BEGIN(); E_not_major(); E(0x74, 0x0B);   /* je over the dec and its jmp */
    E(0xFF, 0x8E, 0xB0, 0x0B, 0x00, 0x00); EJMP(0x42e3e3);
    E(0x39, 0xF6); EJMP(0x42e3e3); site_hook(0x42e3dd, 6);

    /* --- Lasers: ex wait counter [laser+0x5b4]-- once per frame; the per-object timer at
           +0x14/+0x18 is ticked inline by the manager after every update, as in TH12 --- */
    { struct { uintptr_t addr; uint8_t reg; uintptr_t skip, cont; } L[] = {
          { 0x431728, R_EDI, 0x431739, 0x43172e },   /* LaserLine  (0x4315d0) */
          { 0x435761, R_ESI, 0x435772, 0x435767 },   /* LaserCurve (0x4355f0) */
          { 0x4334c6, R_EDI, 0x4334d7, 0x4334cc } }; /* LaserBeam  (0x433470) */
      for (int i = 0; i < 3; i++) {
          STUB_BEGIN(); E_timer_unchanged(L[i].reg, 0x14, 0x18); EJCC(0x84, L[i].skip);
          E(0x8B, (uint8_t)(0x80 | L[i].reg), 0xB4, 0x05, 0x00, 0x00); EJMP(L[i].cont);
          site_hook(L[i].addr, 6);
      } }
    /* --- LaserLine: graze every 3 frames on the graze timer +0x28/+0x2c (0x431a01). The curve
           laser computes the same test in TH13 but no longer acts on it, and the beam has no
           graze branch at all, so only the line laser needs the gate. --- */
    STUB_BEGIN(); E(0x8B, 0x47, 0x2C, 0x3B, 0x47, 0x28); EJCC(0x84, 0x431a1f);
    E(0x99, 0xB9, 0x03, 0x00, 0x00, 0x00); EJMP(0x431a0a); site_hook(0x431a01, 9);

    /* --- Stage (0x406680, EBX = stage): the spell/bomb background distortion consumes RNG every
           frame; run it on frame ticks only, and count its frames on frame ticks only --- */
    STUB_BEGIN(); E(0x8B, 0x8B, 0x6C, 0x40, 0x00, 0x00, 0x85, 0xC9); EJCC(0x84, 0x406d6a);
    E_not_major(); EJCC(0x84, 0x406d6a); EJMP(0x4067f2); site_hook(0x4067e4, 14);
    STUB_BEGIN(); E(0xB8, 0x01, 0x00, 0x00, 0x00); E_not_major(); EJCC(0x84, 0x406d7a);
    E(0x01, 0x83, 0x68, 0x40, 0x00, 0x00); EJMP(0x406d7a); site_hook(0x406d6f, 11);

    /* --- Timer::add (0x4732c0) sites whose argument is a constant in frames, not a rate:
           add value * logical speed instead of value * logical * dt.
           0x44647c: player shot cycle timer -= 14; 0x4629ef: ANM "timer -= N" (inlined in
           AnmVm::update in TH13; a separate helper in TH12) --- */
    { uint8_t* constant = g_p;
      E(0x8B, 0x46, 0x04, 0x89, 0x06);                              /* mov eax,[esi+4]; mov [esi],eax */
      E(0xD9, 0x44, 0x24, 0x04);                                    /* fld dword [esp+4] */
      E(0xD8, 0x0D); E32((uint32_t)(uintptr_t)&g_logical);          /* fmul dword [g_logical] */
      E(0xD8, 0x46, 0x08, 0xD9, 0x56, 0x08);                        /* fadd [esi+8]; fst [esi+8] */
      ECALL(0x4971f0);                                              /* ftol */
      E(0x89, 0x46, 0x04, 0xC2, 0x04, 0x00);                        /* mov [esi+4],eax; ret 4 */
      site_call(0x44647c, constant); site_call(0x4629ef, constant); }

    /* --- Enemy death ring effect callback (0x415e20): shrink/fade once per frame at 0x415e59,
           gated on the effect's own timer at +0xc/+0x10 --- */
    STUB_BEGIN(); E_timer_unchanged(R_EDI, 0x0c, 0x10); EJCC(0x84, 0x415e69);
    ECOPY(0x415e59, 16); EJMP(0x415e69); site_hook(0x415e59, 16);

    /* --- Scrolling-mesh effect VM callback (0x46b9d0, ECX = vm, timer at +0x538/+0x53c):
           per-frame UV scroll; run once per frame --- */
    STUB_BEGIN(); E(0x50); E(0x8B, 0x81); E32(0x538); E(0x3B, 0x81); E32(0x53c); E(0x58);
    E(0x75, 0x03); E(0x31, 0xC0, 0xC3);
    ECOPY(0x46b9d0, 12); EJMP(0x46b9dc); site_hook(0x46b9d0, 12);

    /* --- Player shot behaviours (table 0x4bb4d8, called with EDX = shot from the per-shot
           update 0x4464d0; the shot array is 256 x 0x9c at player+0x6a0, MotionState at +0x2c,
           state +0x70, shot-type data +0x94): homing 0x446cb0 (turn towards the target, speed
           +-0.2 per call), 0x447590 (speed += 1 per call) and 0x447510 (speed *= 0.8 per call)
           advance per call. Run each only on the tick where the shot's timer (+0x18/+0x1c,
           Timer::tick at the end of the shot update) crossed a whole frame; the MotionState
           still integrates every sub-tick. 0x446f20 anchors an option's laser to the option
           every tick and stays unguarded; 0x4474a0 does nothing. Same treatment as TH11/TH12. --- */
    { struct { uintptr_t addr; size_t n; } S[] = { { 0x446cb0, 6 }, { 0x447590, 6 }, { 0x447510, 6 } };
      for (size_t i = 0; i < sizeof S / sizeof *S; ++i) {
          STUB_BEGIN(); E(0x9c); E_timer_unchanged(R_EDX, 0x18, 0x1c);
          E(0x75, 0x04, 0x9d, 0x31, 0xc0, 0xc3);       /* unchanged: restore flags, return 0 */
          E(0x9d); ECOPY(S[i].addr, S[i].n); EJMP(S[i].addr + S[i].n); site_hook(S[i].addr, S[i].n);
      } }
#undef FMUL_FACTOR
    stub_end();
    LOG("TH13 site patches installed (%u bytes of stubs)", (unsigned)g_stub_used);
}

/* Enemy sprites. The enemy keeps a sub-object at +0x11ec holding the position (+0x44), the 14
   ANM VM ids (+0x120), the sprite offsets (+0x168) and the parent slot of each sprite (+0x220);
   flags at +0x4030 (enemy +0x521c), 0x08000000 = sprite positions are absolute. TH13 places the
   sprites in playfield coordinates; no (224,16) offset is added here. The VM position is at
   +0x574 and a parent's contribution is read from its VM at +0x3c (0x41a8a0). */
static void th13_place_enemy(uint8_t* e, uint8_t* am, uint32_t flags, const float* R) {
    uint8_t* in = e + 0x11ec;
    int* ids = (int*)(in + 0x120); float* offs = (float*)(in + 0x168); int* parent = (int*)(in + 0x220);
    for (int i = 0; i < 14; i++) {
        if (!ids[i]) continue;
        float* vm = anm_get_vm(am, ids[i]);
        if (!vm) continue;
        float x = R[0], y = R[1], z = R[2];
        if (!(flags & 0x8000000)) {
            x += offs[i*3]; y += offs[i*3+1]; z += offs[i*3+2];
            if (parent[i] >= 0 && parent[i] < 14 && ids[parent[i]]) {
                float* pvm = anm_get_vm(am, ids[parent[i]]);
                if (pvm) { x += pvm[0x0f]; y += pvm[0x10]; z += pvm[0x11]; }
            }
        }
        vm[0x15d] = x; vm[0x15e] = y; vm[0x15f] = z;
    }
}

/* AnmManager sprite quad builder (0x467350): with the VM's flag bit 0 set -- nearly every
   sprite -- the four corners go through frndint before the half-texel offset. */
static const uintptr_t th13_sprite_round_sites[] = { 0x4673f9, 0x467407, 0x467415, 0x467423 };

static const struct node_class th13_classes[] = {
    { 0x40e780, MODE_SUB,   "BulletManager"   },
    { 0x443de0, MODE_SUB,   "Player"          },
    { 0x44ad40, MODE_FRAME, "Bomb"            },
    { 0x42efb0, MODE_SUB,   "ItemManager"     },
    { 0x42fe30, MODE_SUB,   "LaserManager"    },
    { 0x438e70, MODE_FRAME, "Gui"             },
    { 0x407680, MODE_SUB,   "Stage"           },
    { 0x46f360, MODE_SUB,   "AnmManagerWorld" },
    { 0x46f330, MODE_SUB,   "AnmManagerUI"    },
    { 0x413250, MODE_FRAME, "Spellcard"       },
    { 0x418ef0, MODE_FRAME, "EnemyManager"    },
    { 0x403d60, MODE_FRAME, "Effects"         },
    { 0x42cb90, MODE_FRAME, "GameManager"     },
};

/* Dimming classes (DEVNOTES_RUNTIME 3b). The ItemManager draws at 26; the world's free-standing
   VMs are drawn by the sprite-layer callbacks between 12 and 43. astral.anm is the divine spirits.
   The player's own shots sit on sprite layers 10..13 of pl0X.anm, the body has none. */
static const struct DimRule th13_dim_rules[] = {
    { 26, 26, NULL,          -1, -1, -1, -1, DIM_ITEMS },
    { 29, 31, NULL,          -1, -1, -1, -1, DIM_NONE },          /* lasers, bullets */
    {  8,  8, "effect.anm",   2,  2, -1, -1, DIM_EFFECTS },       /* petals, the stage's and the enemies' deaths', drawn under the world */
    { 12, 43, "astral.anm",  -1, -1, -1, -1, DIM_SPECIAL },
    { 12, 43, "pl*.anm",     12, 12, -1, -1, DIM_NONE },          /* the hitbox */
    { 12, 43, "effect.anm",  12, 12, -1, -1, DIM_NONE },          /* the focus ring */
    { 12, 43, "pl*.anm",     10, 13, -1, -1, DIM_PLAYER_SHOTS },
    { 12, 43, "pl*.anm",     -1, -1, -1, -1, DIM_NONE },
    { 12, 43, "enemy.anm",   -1, -1, -1, -1, DIM_NONE },
    { 12, 43, "effect.anm",  -1, -1, -1, -1, DIM_EFFECTS },
    { 12, 43, "bullet.anm",   6,  6, -1, -1, DIM_EFFECTS },       /* bullet cancels */
    { 12, 43, "bullet.anm",  16, 16, -1, -1, DIM_EFFECTS },
};
static const struct GameProfile th13_profile = {
    .identity = &game_identities[GI_TH13],
    .addr = {
        .speed = 0x4c0a28,
        .update_runner = 0x4dc658,
        .device = 0x4dc6a8,
        .pp = 0x4dc794,
        .window_flags = 0x4df0e0,
        .frame_time = 0x4dcf58,
        .misc_flags = 0x4e49ed,          /* a byte: whether the runner locks; see critical_flag_mask */
        .raw_input = 0x4e49f0,
        .raw_pressed = 0x4e49fc,
        .replay_manager = 0x4c22c8,
        .frame_fn = 0x45d570,
        .enemy_manager = 0x4c2188,
        .anm_manager = 0x4dc688,
        .anm_get_vm = 0x46fb90,
        .game_input = 0x4e4c08,
        .option_flags = 0x4dc8a8,
        .autofocus = 0x4e4b84,
        .poll_input = 0x471620,
        .remove_node = 0x470e90,
        .crit = 0x4e48a8,
        .crit_count = 0x4e49e0,
        .gm_callback = 0x42cb90,
        .game_manager = 0x4c2194,
        .record_callback = 0x448e30,
        .playback_callback = 0x448e40,
        .player_callback = 0x443de0,
        .game_pressed = 0x4e4c14,
        .game_released = 0x4e4c18,
        .player = 0x4c22c4,
        .frame_context_ptr = 0x4dcc18,
        .frame_flag = 0x4dcc1c,
        .frame_context_value = 0x4dc9d0,
        .cleanup_fn = 0x473590,
        .cleanup_this = 0x4dcebc,
        .replay_save = 0x4484d0,
        .frame_calls = {0x45c5de, 0x45c5fb, 0x45c607},
        .replay_saves = {0x43f26b, 0x440280, 0x4412e0, 0x454e15},
        .runner_fn = 0x470af0, .runner_ret = 0x470c04,
        .latency_cmp = 0x45d69e,
        .screenshot_fn = 0x43a950, .screenshot_call = 0x45d856,
        .data_dir = 0x4dd0d1,            /* "%APPDATA%\\ShanghaiAlice\\th13\\", built at startup */
    },
    .layout = {
        .runner_ending = 0x54, .gm_pause_flags = 0x60, .input_size = 0x130, .input_width = 4,
        .node_arg = 0x24, .runner_next = 0x50,
        .replay_stage = 0x218,
        .replay_frame = 0x210,
        .replay_stages = 0x20,
        .player_timer = 0x66c,
        .enemy_list = 0xb0, .enemy_flags = 0x521c, .enemy_position = 0x1230, .enemy_skip_mask = 0x4000000,
    },
    .speed_sites = th13_speed_sites, .speed_site_count = sizeof th13_speed_sites / sizeof *th13_speed_sites,
    .critical_flag_mask = 0xff, .runner_return8_ends = 1, .remove_node_abi = REMOVE_NODE_RUNNER_FIRST,
    .classes = th13_classes, .class_count = sizeof th13_classes / sizeof *th13_classes,
    .mask_minor_player_edges = 0, .d3dx = "d3dx9_43.dll",
    .native_size_cycle = 1,
    .sprite_round_sites = th13_sprite_round_sites, .sprite_round_count = 4,
    /* Draw runner 0x470c30: for each node (ESI) `mov ecx,[esi+0x24]; mov edx,[esi+8]; call edx`.
       Sprite batch flush 0x4679a0 wants the AnmManager (pointer at 0x4dc688) in ESI. Draw
       priorities: 1..10 stage 3D, sprite layers 0..3 and the spell background, all into the stage
       target; 12/14 copy that into the world target; 15 on is the world (enemies 21, items 26,
       lasers 29, bullets 31 ...); 44 on the interface (TH13_DEVNOTES has the table). */
    .draw = { .dispatch = 0x470c9e, .dispatch_len = 8, .node_reg = R_ESI, .prio_off = 0,
              .flush_fn = 0x4679a0, .flush_reg = R_ESI, .flush_this = 0x4dc688,
              .world_prio = 12, .rules = th13_dim_rules, .rule_count = sizeof th13_dim_rules / sizeof *th13_dim_rules, .special_name = "spirits",
              .vm_draw = 0x46a700, .vm_draw_len = 6, .vm_reg = R_EAX, .vm_anm_off = 0x30, .vm_layer_off = 0x24, .vm_script_off = 0x4aa },
    .install_sites = th13_install_sites, .place_enemy = th13_place_enemy,
};
