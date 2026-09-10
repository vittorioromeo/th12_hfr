/* TH11: reviewed game-specific hooks and object layout. */
static void th11_install_speed(void) {
    const uintptr_t perm[] = {0x41f963, 0x41fe23, 0x42956d, 0x4314b8, 0x459b0c};
    const uintptr_t temp[] = {0x402b7c, 0x44b4f3};
    const uintptr_t pset[] = {0x42c73f, 0x42c85d, 0x42d696, 0x42d7db};
    const uintptr_t prest[] = {0x42c8b6, 0x42d845, 0x42e6a5};
    for (size_t i=0; i<sizeof perm/sizeof *perm; ++i) patch_call_n(perm[i],stub_set_one_perm,6,site_expected(perm[i],6));
    for (size_t i=0; i<sizeof temp/sizeof *temp; ++i) patch_call_n(temp[i],stub_set_one_temp,6,site_expected(temp[i],6));
    for (size_t i=0; i<sizeof pset/sizeof *pset; ++i) patch_call_n(pset[i],stub_pause_set,6,site_expected(pset[i],6));
    for (size_t i=0; i<sizeof prest/sizeof *prest; ++i) patch_call_n(prest[i],stub_pause_restore,6,site_expected(prest[i],6));
    patch_call_n(0x4169d0,stub_set_ecl,6,site_expected(0x4169d0,6));
    /* Paired effective-speed restores and the laser manager's temporary zero stay intact. */
}

static void th11_install_sites(void) {
    g_p = stub_begin();
    /* Bullet lifetime / collision grace counters; object Timer is +0x464. */
    gate_block(0x408cbc, 5, 0x408cd8, R_EBX, 0x464);

    /* Stage distortion consumes RNG and advances phase once per frame. */
    gate_block(0x402bc5, 10, 0x40311b, 0, -1);
    gate_block(0x403121, 6, 0x403127, 0, -1);

    /* State-5 items leave five values on the FPU stack. Retain the original pops
       and branch: on minor ticks synthesize SF=0 without decrementing. */
    STUB_BEGIN(); E_not_major(); E(0x75,0x07); E(0x39,0xff); EJMP(0x4235b6);
    ECOPY(0x4235af,7); EJMP(0x4235b6); site_hook(0x4235af,7);
    const uintptr_t item_accel[] = {0x4238f9, 0x4239a1};
    for (int i=0; i<2; ++i) {
        STUB_BEGIN(); E(0xdd,0x05); E32(0x498130); /* double 0.2 */
        E(0xd8,0x0d); E32(0x4a7948); E(0xde,0xc1);
        EJMP(item_accel[i]+6); site_hook(item_accel[i],6);
    }

    /* TH11 has line and beam lasers (no TH12 curved-laser class). */
    gate_block(0x425de0, 6, 0x425df1, R_EBP, 0x14);
    gate_block(0x426097, 9, 0x4260f6, R_EBP, 0x28);
    gate_block(0x427580, 9, 0x4275ec, R_ESI, 0x14);

    /* Player death drops must happen exactly once at timer=3. */
    gate_block(0x4312c6, 7, 0x4313e6, R_ESI, 0x944);
    gate_block(0x431ab6, 6, 0x431af7, R_ESI, 0x944);

    /* Reimu C speed bonus: frame-counted warmup and particle emission, but the
       doubled movement amount still applies on every sub-tick. */
    gate_block(0x43065f, 6, 0x430665, 0, -1);
    STUB_BEGIN(); E_not_major(); E(0x75,0x0d);
    E(0xd1,0x64,0x24,0x2c, 0xd1,0x64,0x24,0x10); /* shl movement x/y,1 */
    EJMP(0x4306ce);
    ECOPY(0x43066e,7); EJMP(0x430675); site_hook(0x43066e,7);
    /* Reimu B auto-collect, Yukari warp state, option loss timer. */
    gate_block(0x43078f, 6, 0x4307ab, 0, -1);
    STUB_BEGIN(); E(0x9c); E_not_major(); E(0x75,0x02,0x9d,0xc3);
    E(0x9d); ECOPY(0x430e50,9); EJMP(0x430e59); site_hook(0x430e50,9);
    gate_block(0x430b48, 6, 0x430b4e, 0, -1);
    /* Options include per-call easing and shot-specific callbacks. Keep their
       update at 60 Hz; the player itself and its shots remain sub-stepped. */
    gate_block(0x430b4e, 6, 0x430d91, 0, -1);

    /* Fixed-point movement is in 1/128 px units. Carry sub-tick truncation. */
    const uintptr_t ftol_sites[] = {0x430722,0x430735};
    for (int i=0; i<2; ++i) {
        uint8_t* st=g_p;
        /* At stock speed retain the game's original truncation, including ECL
           slow motion. Residuals only compensate the additional sub-division. */
        E(0x81,0x3d); E32((uint32_t)(uintptr_t)&g_factor); E32(0x3f800000);
        E(0x75,0x05); EJMP(0x4864e0);
        E(0xd8,0x05); E32((uint32_t)(uintptr_t)&g_move_residual[i]); E(0xd9,0xc0);
        ECALL(0x4864e0); E(0x50,0xdb,0x04,0x24,0x58,0xde,0xe9);
        E(0xd9,0x1d); E32((uint32_t)(uintptr_t)&g_move_residual[i]); E(0xc3);
        site_call(ftol_sites[i],st);
    }

    /* Shared MotionState Cartesian integration (also used by player shots). */
    STUB_BEGIN();
    E(0xd9,0x43,0x0c); emit_factor(); E(0xd8,0x03,0xd9,0x1b);
    E(0xd9,0x43,0x10); emit_factor(); E(0xd8,0x43,0x04,0xd9,0x5b,0x04);
    E(0xd9,0x43,0x14); emit_factor(); E(0xd8,0x43,0x08,0xd9,0x5b,0x08);
    E(0x8b,0xf3); EJMP(0x4595b7); site_hook(0x45959c,27);

    /* Player damage-source acceleration and displacement. */
    STUB_BEGIN(); E(0xd9,0x47,0x18); emit_factor(); E(0x51,0xd8,0x47,0x14);
    EJMP(0x4315ac); site_hook(0x4315a5,7);
    STUB_BEGIN(); E(0xd9,0x47,0xe0); emit_factor(); E(0x8b,0x44,0x24,0x10,0xd8,0x00,0xd9,0x18);
    E(0xd9,0x47,0xe8); emit_factor(); E(0xd8,0x47,0xe4,0xd9,0x5f,0xe4);
    EJMP(0x4315e4); site_hook(0x4315d0,20);
    STUB_BEGIN(); E(0xd9,0x46,0x18); emit_factor(); E(0x51,0xd8,0x46,0x14);
    EJMP(0x434569); site_hook(0x434562,7);
    const uintptr_t turn_sites[]={0x4315af,0x43456c};
    for (int i=0;i<2;++i) {
        STUB_BEGIN(); ECOPY(turn_sites[i],3); emit_factor();
        ECOPY(turn_sites[i]+3,3); EJMP(turn_sites[i]+6); site_hook(turn_sites[i],6);
    }

    /* Shot-cycle subtraction and ANM wait offsets are constants, not rates. */
    uint8_t* add_constant=g_p;
    E(0x8b,0x46,0x04,0x89,0x06,0xd9,0x44,0x24,0x04,0xd8,0x0d); E32((uint32_t)(uintptr_t)&g_logical);
    E(0xd8,0x46,0x08,0xd9,0x56,0x08); ECALL(0x4864e0);
    E(0x89,0x46,0x04,0xc2,0x04,0x00);
    site_call(0x4343fc,add_constant); site_call(0x4355cd,add_constant);

    /* Enemy damage still evaluated at 60 Hz, but its player-timer guard must
       see that the last sub-tick advanced the float timer. */
    STUB_BEGIN(); E(0x50,0xa1); E32((uint32_t)(uintptr_t)&g_ptf_prev);
    E(0x3b,0x05); E32((uint32_t)(uintptr_t)&g_ptf_cur); E(0x58);
    EJCC(0x85,0x434827); EJMP(0x43481c); site_hook(0x434814,6);

    /* Mesh distortion/UV callbacks consume random numbers or advance per call. */
    gate_callback(0x408070,7); gate_callback(0x452420,9);
    /* Shot callback table at 0x4a3a3c: homing turn/acceleration and gravity
       advance per call. Keep those decisions on each shot's integer timer;
       its position still integrates every sub-tick. The third callback
       (0x435330) just anchors a laser to its option and remains unguarded. */
    gate_shot_callback(0x434e30); gate_shot_callback(0x4352a0);
    stub_end();
    LOG("TH11 site patches installed (%u bytes of stubs)",(unsigned)g_stub_used);
}

static void th11_place_enemy(uint8_t* e, uint8_t* am, uint32_t flags, const float* R) {
        int* ids = (int*)(e + 0x111c);
        for (int i = 0; i < 8; i++) {
            if (!ids[i]) continue;
            uint8_t* vm = (uint8_t*)anm_get_vm(am, ids[i]);
            if (!vm) continue;
            *(float*)(vm + 0x3e8) = R[0] + 224.0f;
            *(float*)(vm + 0x3ec) = R[1] + 16.0f;
            *(float*)(vm + 0x3f0) = R[2];
            if (*(int*)(vm + 0x18) == 0) {
                for (uint32_t* c = *(uint32_t**)(vm + 0x14); c; c = (uint32_t*)c[1]) {
                    uint8_t* child = (uint8_t*)c[0];
                    memcpy(child + 0x3e8, vm + 0x3e8, 12);
                }
            }
        }
}
static const struct node_class th11_classes[] = {
    /* func (UpdateFunc callback address) , mode , name */
    { 0x408ec0, MODE_SUB,   "BulletManager"   },
    { 0x431c50, MODE_SUB,   "Player"          },
    { 0x4064d0, MODE_FRAME, "Bomb"            },
    { 0x424090, MODE_SUB,   "ItemManager"     },
    { 0x424d70, MODE_SUB,   "LaserManager"    },
    { 0x41cfb0, MODE_FRAME, "Gui"             },
    { 0x403900, MODE_SUB,   "Stage"           },
    { 0x455120, MODE_SUB,   "AnmManagerWorld" },
    { 0x455130, MODE_SUB,   "AnmManagerUI"    },
    { 0x40e300, MODE_FRAME, "Spellcard"       },
    { 0x4111a0, MODE_FRAME, "EnemyManager"    },
    { 0x421b20, MODE_FRAME, "PlayerBomb?"     },
    { 0x420840, MODE_FRAME, "GameManager"     },
};

static const struct GameProfile th11_profile = {
    .identity = &game_identities[0],
    .addr = {
        .speed = 0x4a7948,
        .update_runner = 0x4c3234,
        .device = 0x4c3288,
        .pp = 0x4c3374,
        .window_flags = 0x4c3dc0,
        .frame_time = 0x4c3c38,
        .misc_flags = 0x4c3810,
        .raw_input = 0x4c92a8,
        .raw_pressed = 0x4c92b4,
        .replay_manager = 0x4a8eb8,
        .frame_fn = 0x446650,
        .enemy_manager = 0x4a8d7c,
        .anm_manager = 0x4c3268,
        .anm_get_vm = 0x4561e0,
        .game_input = 0x4c93c0,
        .option_flags = 0x4c3480,
        .autofocus = 0x4c93bc,
        .poll_input = 0x4576b0,
        .remove_node = 0x457080,
        .crit = 0x4c3a90,
        .crit_count = 0x4c3bb0,
        .gm_callback = 0x420840,
        .game_manager = 0x4a8e88,
        .record_callback = 0x436d20,
        .playback_callback = 0x436d30,
        .player_callback = 0x431c50,
        .game_pressed = 0x4c93cc,
        .game_released = 0x4c93d0,
        .player = 0x4a8eb4,
        .frame_context_ptr = 0x4c37cc,
        .frame_flag = 0x4c37d0,
        .frame_context_value = 0x4c359c,
        .cleanup_fn = 0x459430,
        .cleanup_this = 0x4c3a70,
        .replay_save = 0x436420,
        .replay_load = 0x436b60,
        .frame_calls = {0x44587e,0x44589b,0x4458a7},
        .replay_saves = {0x42d2ae,0x42e21b,0x42ef4b,0x4403c5},
        .replay_load_call = 0x435a0d,
        .runner_fn = 0x456cb0,
        .latency_cmp = 0x446799,
    },
    .layout = {
        .replay_stage = 0x1d4,
        .replay_frame = 0x1cc,
        .replay_stages = 0x1c,
        .player_timer = 0x94c,
        .enemy_flags = 0x25bc,
        .enemy_position = 0x1070,
        .enemy_skip_mask = 0x400000,
    },
    .classes = th11_classes, .class_count = sizeof th11_classes / sizeof *th11_classes,
    .mask_minor_player_edges = 1, .d3dx = "d3dx9_37.dll",
    .install_speed = th11_install_speed, .install_sites = th11_install_sites,
    .place_enemy = th11_place_enemy,
};
