/* TH10 v1.00a: timers carry a pointer to the shared speed at +0x0c.
   Scheduling, rendering, window controls, shaders and menu remain shared. */
static const struct SpeedSite th10_speed_sites[] = {
    {0x4178b1,10,SPEED_ONE_PERM,0}, {0x417ca9,10,SPEED_ONE_PERM,0},
    {0x4201b7,10,SPEED_ONE_PERM,0}, {0x425cc4,10,SPEED_ONE_PERM,0},
    {0x4027ea,10,SPEED_ONE_TEMP,0}, {0x43ee84,10,SPEED_ONE_TEMP,0},
    {0x422c20,10,SPEED_PAUSE_SET,0}, {0x42335f,10,SPEED_PAUSE_SET,0},
    {0x4234ff,10,SPEED_PAUSE_SET,0},
    {0x422c73,6,SPEED_PAUSE_RESTORE,0}, {0x423563,5,SPEED_PAUSE_RESTORE,0},
    {0x411c3d,6,SPEED_ECL,1},
};

/* Different native replay ABIs, identical file extension and playback policy. */
static void __fastcall th10_replay_save(void* manager, char* filename, char* name) {
    ((void (__fastcall*)(void*,char*,char*))0x429b60)(manager,filename,name);
    replay_append_chunk(filename);
}
int __stdcall __attribute__((used)) th10_replay_load_c(void* manager, char* filename) {
    restore_replay_settings(); g_replay_playing=0;
    uintptr_t s=(uintptr_t)manager; int result;
    __asm__ volatile("push %2\n\tcall *%3" : "=a"(result), "+S"(s)
        : "r"(filename), "r"((uintptr_t)0x42a200) : "ecx","edx","memory","cc");
    replay_loaded(filename);
    return result;
}
__asm__(".intel_syntax noprefix\n.globl _th10_replay_load_entry\n_th10_replay_load_entry:\n"
        "push dword ptr [esp+4]\npush esi\ncall _th10_replay_load_c@8\nret 4\n.att_syntax\n");
extern void th10_replay_load_entry(void);

static void th10_install_sites(void) {
    g_p=stub_begin();
    /* The shared frame hook owns pacing and presents every scheduled slot. */
    const uint8_t nops[6]={0x90,0x90,0x90,0x90,0x90,0x90};
    patch_bytes(0x4393b7,nops,6,site_expected(0x4393b7,6));
    patch_bytes(0x439488,nops,6,site_expected(0x439488,6));
    /* The FPS counter treats >65 FPS as a faulty clock: after two samples it
       rebases the native timer, after four it disables QPC. At HFR this produces
       clock jumps, 0.0 FPS readings and huge catch-up loops in 0x4393d0. Keep the
       measured FPS/slowdown accounting, but always take the watchdog's reset
       branch (EDI is already zero). This concerns presentation even at 60 Hz
       logic, so it must also apply with substep=0 and during stock replays. */
    const uint8_t skip_clock_watchdog[2]={0xeb,0x5b};
    patch_bytes(0x413508,skip_clock_watchdog,2,site_expected(0x413508,2));
    site_call(0x423f16,th10_replay_save); site_call(0x43399d,th10_replay_save);
    site_call(0x429257,th10_replay_load_entry); site_call(0x42948c,th10_replay_load_entry);
    site_call(0x429765,th10_replay_load_entry);
    /* Integer counters use the object's own timer. */
    gate_block(0x406584,5,0x4065a0,R_EBP,0x3f8);
    gate_block(0x425aa9,7,0x425b9d,R_EBP,0x474);
    gate_block(0x426285,5,0x4262b4,R_EBP,0x474);
    /* Option history, easing and callbacks are measured in original frames. */
    gate_block(0x425520,6,0x4256de,0,-1);
    gate_block(0x4251c1,7,0x4251c8,0,-1);
    movement_ftol(0x42540a,0x463b2c,0);
    movement_ftol(0x42541f,0x463b2c,1);
    /* Item homing acceleration is a rate rather than a constant offset. */
    const uintptr_t item_accel[]={0x41b28d,0x41b315};
    for(unsigned i=0;i<2;i++) {
        STUB_BEGIN(); E(0xd9,0x05); E32(0x470c38); emit_factor(); E(0xde,0xc1);
        EJMP(item_accel[i]+6); site_hook(item_accel[i],6);
    }
    /* Cartesian integration shared by player shots and hitboxes. */
    STUB_BEGIN();
    E(0xd9,0x46,0x0c);emit_factor();E(0xd8,0x06,0xd9,0x1e);
    E(0xd9,0x46,0x10);emit_factor();E(0xd8,0x46,0x04,0xd9,0x5e,0x04);
    E(0xd9,0x46,0x14);emit_factor();E(0xd8,0x46,0x08,0xd9,0x5e,0x08);
    EJMP(0x44c30f);site_hook(0x44c2aa,25);
    /* Repeated centipixel rounding would bias every subdivided displacement. */
    STUB_BEGIN(); E(0x81,0x3d);E32((uintptr_t)&g_factor);E32(0x3f800000);
    EJCC(0x85,0x44c347);ECOPY(0x44c30f,5);EJMP(0x44c314);site_hook(0x44c30f,5);
    const uintptr_t acceleration[]={0x425dab,0x4283a7};
    const uintptr_t turn[]={0x425db5,0x4283b1};
    for(unsigned i=0;i<2;i++) {
        STUB_BEGIN();ECOPY(acceleration[i],3);emit_factor();ECOPY(acceleration[i]+3,4);
        EJMP(acceleration[i]+7);site_hook(acceleration[i],7);
        STUB_BEGIN();ECOPY(turn[i],3);emit_factor();ECOPY(turn[i]+3,3);
        EJMP(turn[i]+6);site_hook(turn[i],6);
    }
    STUB_BEGIN();E(0xd9,0x47,0xe0);emit_factor();E(0xd8,0x03,0xd9,0x1b);
    E(0xd9,0x47,0xe8);emit_factor();E(0xd8,0x47,0xe4,0xd9,0x5f,0xe4);
    EJMP(0x425dde);site_hook(0x425dce,16);
    /* Enemy hit test (0x428630) opens with the stock double-hit guard, "player state timer
       unchanged since last frame -> no damage" (cmp eax,[ebp+0x474] against the integer timer
       just loaded from +0x478). Sub-stepped, the integer timer advances on the last minor tick
       of a frame, so on the boundary tick -- the only tick the 60 Hz enemy code runs this test --
       it always reads unchanged, and no shot ever lands. Compare the float timer around the
       last Player update instead, as TH11 and TH12 do; the flags feed the game's own jne. */
    STUB_BEGIN(); E(0x50,0xa1); E32((uint32_t)(uintptr_t)&g_ptf_prev);
    E(0x3b,0x05); E32((uint32_t)(uintptr_t)&g_ptf_cur); E(0x58);
    EJMP(0x428644); site_hook(0x42863e,6);
    /* Shot-cycle and ANM wait corrections subtract constants, not rates. */
    uint8_t* constant=g_p;
    E(0x8b,0x46,0x04,0x89,0x06,0xd9,0x44,0x24,0x04,0xd8,0x0d);E32((uintptr_t)&g_logical);
    E(0xd8,0x46,0x08,0xd9,0x56,0x08);ECALL(0x463b2c);
    E(0x89,0x46,0x04,0xc2,0x04,0x00);
    site_call(0x428243,constant);site_call(0x440e3d,constant);
    stub_end();
    LOG("TH10 site patches installed (%u bytes of stubs); >65 FPS clock-reset watchdog disabled",(unsigned)g_stub_used);
}
static const struct node_class th10_classes[] = {
    {0x406770,MODE_SUB,"BulletManager"}, {0x426500,MODE_SUB,"Player"},
    {0x405840,MODE_FRAME,"Bomb"}, {0x41ba00,MODE_SUB,"ItemManager"},
    {0x41c480,MODE_FRAME,"LaserManager"}, {0x415ae0,MODE_FRAME,"Gui"},
    {0x403050,MODE_FRAME,"Stage"},
    {0x4485d0,MODE_SUB,"AnmManagerWorld"}, {0x4485e0,MODE_SUB,"AnmManagerUI"},
    {0x40b050,MODE_FRAME,"Spellcard"}, {0x40d810,MODE_FRAME,"EnemyManager"},
    {0x4187c0,MODE_FRAME,"GameManager"},
};
/* Dimming classes (DEVNOTES_RUNTIME 3b): ItemManager 25, LaserManager 27, BulletManager 29; the
   world's sprite layers run from 11 to 40. bullet.anm carries items (layer 7), bullets (13) and
   the effects; the player's shots are pl0X.anm layers 8 and 13. */
static const struct DimRule th10_dim_rules[] = {
    { 25, 25, NULL,          -1, -1, -1, -1, DIM_ITEMS },
    { 27, 29, NULL,          -1, -1, -1, -1, DIM_NONE },
    { 14, 14, "stgenm*.anm", -1, -1, -1, -1, DIM_BACKGROUND },   /* the spell-card backgrounds, drawn by 0x409230 above the world */
    { 11, 40, "pl*.anm",      8, 13, -1, -1, DIM_PLAYER_SHOTS },
    { 11, 40, "pl*.anm",     -1, -1, -1, -1, DIM_NONE },
    { 11, 40, "enemy.anm",    4,  4, -1, -1, DIM_EFFECTS },      /* enemy.anm's effect layer under the enemies: spawn-in flashes, auras */
    { 11, 40, "enemy.anm",   -1, -1, -1, -1, DIM_NONE },
    { 11, 40, "bullet.anm",   7,  7, -1, -1, DIM_NONE },
    { 11, 40, "bullet.anm",  13, 13, 351, 352, DIM_NONE },     /* the player's hitbox: two sprites turning about the player while focused */
    { 11, 40, "bullet.anm",  -1, -1, -1, -1, DIM_EFFECTS },
};
static const struct GameProfile th10_profile = {
    .identity=&game_identities[GI_TH10],
    .addr={
        .speed=0x476f78,.update_runner=0x491be4,.device=0x491c30,.pp=0x491d0c,
        .frame_time=0x4923a0,.misc_flags=0x491ff4,
        .raw_input=0x474e30,.raw_pressed=0x474e36,.game_input=0x474e5c,
        .game_pressed=0x474e62,.game_released=0x474e64,
        .option_flags=0x491d78,.autofocus=0x474e5a,.poll_input=0x44a5f0,
        .replay_manager=0x477838,.frame_fn=0x439390,
        .enemy_manager=0x477704,.anm_manager=0x491c10,.anm_get_vm=0x4491c0,
        .remove_node=0x449f60,.crit=0x492274,.crit_count=0x49231c,
        .gm_callback=0x4187c0,.game_manager=0x477810,
        .record_callback=0x42a3d0,.playback_callback=0x42a3d0,
        .player_callback=0x426500,.player=0x477834,
        .frame_context_ptr=0x491fac,.frame_flag=0x491fb0,.frame_context_value=0x491e94,
        .cleanup_fn=0x44c150,.cleanup_this=0x492254,
        .frame_calls={0x438d31},.runner_fn=0x449c00,.runner_ret=0x449d0e,
        .screenshot_fn=0x420670,.screenshot_call=0x4392c1,
    },
    .layout={
        .replay_stage=0x1d0,.replay_frame=0x1c8,.replay_stages=0x1c,
        .player_timer=0x47c,.gm_pause_flags=0x58,
        .input_size=0x6a,.input_width=2,.focus_mask=4,
    },
    .speed_sites=th10_speed_sites,.speed_site_count=sizeof th10_speed_sites/sizeof *th10_speed_sites,
    .classes=th10_classes,.class_count=sizeof th10_classes/sizeof *th10_classes,
    .runner_stack_arg=1,.mask_minor_player_edges=1,.d3dx="d3dx9_31.dll",
    .draw = { .dispatch = 0x449da3, .dispatch_len = 6, .node_reg = R_ESI, .prio_off = 0, .flush_fn = 0x442f50, .flush_reg = R_ESI, .flush_this = 0x491c10, .world_prio = 11, .rules = th10_dim_rules, .rule_count = sizeof th10_dim_rules / sizeof *th10_dim_rules, .special_name = NULL,
              .vm_draw = 0x4451c0, .vm_draw_len = 6, .vm_reg = R_EAX, .vm_anm_off = 0x308, .vm_layer_off = 0x20, .vm_script_off = 0x38a },
    .install_sites=th10_install_sites,
    .provisional=0, /* validated: full patch plan in the harness, and a live run under Wine
                       that boots, plays gameplay at 240 Hz with 0 repeated frames, records and
                       replays HFR replays with per-tick input, borderless + resize + filters */
};
