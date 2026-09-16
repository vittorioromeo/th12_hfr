static int test_frame_calls,test_sub_calls,test_result,test_edges;
static int __thiscall fake_frame(void* arg) {++test_frame_calls;return test_result;}
static int __thiscall fake_sub(void* arg) {
    ++test_sub_calls;
    assert(G_GAME_SPEED==g_logical*g_dt);
    test_edges=input_read(g_game->addr.game_pressed);
    return 1;
}
/* These tests drive the runner through the profile's own globals, which is the point: the
   macros that read them are part of what is under test. A game whose structures are not
   described yet has zeroes there, so the fields get somewhere real to point and the described
   path is still exercised end to end.

   This is a fixture convenience and nothing more. It is emphatically NOT the answer to "what
   happens when a profile leaves an address out" -- that is test_runner_undescribed() below,
   and handing the fixture an address instead of writing that test is how TH14 shipped a build
   that page-faulted on its first tick. */
static uint8_t test_missing_globals[7][8];
static void test_fill_missing(struct GameProfile* g) {
    uintptr_t* fields[] = { &g->addr.speed, &g->addr.player, &g->addr.enemy_manager, &g->addr.game_manager,
                            &g->addr.game_input, &g->addr.game_pressed, &g->addr.game_released };
    for (size_t i=0;i<sizeof fields/sizeof *fields;++i)
        if (!*fields[i]) *fields[i]=(uintptr_t)test_missing_globals[i];
}
static void test_runner(void) {
    const struct GameProfile* real=g_game;
    struct GameProfile game=*real;
    struct node_class classes[]={{(uintptr_t)fake_frame,MODE_FRAME,"Frame"},{(uintptr_t)fake_sub,MODE_SUB,"Sub"}};
    game.classes=classes;game.class_count=2;game.addr.player_callback=(uintptr_t)fake_sub;
    test_fill_missing(&game);
    g_game=&game;g_sub_enabled[0]=g_sub_enabled[1]=1;
    uint8_t runner[0x60]={0},manager[0x80]={0};
    /* The game initialises its critical section at startup; the fixture is bare memory. TH11
       and TH12 only enter it when misc_flags has 0x8000 set, which this test never sets, so
       they never noticed. TH10's runner enters it unconditionally -- confirmed at the
       instruction level, `push 0x492274; call EnterCriticalSection` with no flag test -- and
       EnterCriticalSection on a zeroed section waits forever. */
    InitializeCriticalSection((LPCRITICAL_SECTION)game.addr.crit);
    struct UpdateFunc frame={0},sub={0};
    frame.flags=sub.flags=2;frame.func=fake_frame;sub.func=fake_sub;
    struct ListNode second={&sub,NULL,NULL},first={&frame,&second,NULL};
    *(struct ListNode**)(runner+0x18)=&first;G_UPDATE_RUNNER=runner;
    G_MISC_FLAGS=0;G_REPLAY_MANAGER=NULL;G_ENEMY_MANAGER=NULL;*(void**)game.addr.player=NULL;
    cfg.substep=1;cfg.subtick_input=0;g_major=1;g_skip_update=0;g_dt=0.25f;g_logical=1;
    input_write(game.addr.game_pressed,2);input_write(game.addr.game_released,4);set_game_input(2);
    test_result=1;test_frame_calls=test_sub_calls=0;
    assert(hfr_runner(runner)==2 && test_frame_calls==1 && test_sub_calls==1 && test_edges==2);
    g_major=0;assert(hfr_runner(runner)==2 && test_frame_calls==1 && test_sub_calls==2);
    assert(test_edges==(game.mask_minor_player_edges?0:2));
    assert(input_read(game.addr.game_pressed)==2 && input_read(game.addr.game_released)==4 && G_GAME_INPUT==2);
    assert(G_GAME_SPEED==1);
    g_skip_update=1;assert(hfr_runner(runner)==1 && test_sub_calls==2);g_skip_update=0;
    g_major=1;test_result=3;assert(hfr_runner(runner)==1 && test_sub_calls==2);
    g_major=0;assert(hfr_runner(runner)==1 && test_sub_calls==2);
    /* A new pause raised between boundary ticks must cut off the player. */
    g_stop_node=NULL;game.addr.gm_callback=(uintptr_t)fake_frame;
    *(void**)game.addr.game_manager=manager;*(uint32_t*)(manager+game.layout.gm_pause_flags)=0x10;
    assert(hfr_runner(runner)==1 && test_sub_calls==2);
    *(uint32_t*)(manager+game.layout.gm_pause_flags)=0;test_result=1;
    assert(hfr_runner(runner)==2 && test_sub_calls==3);
    DeleteCriticalSection((LPCRITICAL_SECTION)game.addr.crit);
    memset((void*)game.addr.crit, 0, sizeof(CRITICAL_SECTION));   /* keep the fixture dump comparable between runs */
    G_UPDATE_RUNNER=NULL;*(void**)game.addr.game_manager=NULL;g_game=real;g_stop_node=NULL;g_major=1;g_dt=1;
    puts("PASS: shared runner boundary/minor/duplicate ticks, input edges, list stop and immediate pause");
}

/* A profile that describes the scheduler and nothing else. Every optional address is zero and
   the ini has both sub-step switches on, which is exactly the state TH14 shipped in: the pass
   must run, take its minor ticks, and touch none of them. The crash this exists for was
   set_factor writing the game speed through a null addr.speed, on the first tick, inside the
   first call the frame function makes. */
static int test_bare_calls;
static int __thiscall fake_bare(void* arg) { ++test_bare_calls; return 1; }
static void test_runner_undescribed(void) {
    const struct GameProfile* real=g_game;
    struct GameProfile game=*real;
    game.classes=NULL;game.class_count=0;game.place_enemy=NULL;
    game.addr.speed=0;game.addr.player=0;game.addr.player_callback=0;
    game.addr.enemy_manager=0;game.addr.anm_manager=0;
    game.addr.game_manager=0;game.addr.gm_callback=0;
    game.addr.game_input=0;game.addr.game_pressed=0;game.addr.game_released=0;
    game.addr.poll_input=0;game.addr.record_callback=0;game.addr.playback_callback=0;
    g_game=&game;
    InitializeCriticalSection((LPCRITICAL_SECTION)game.addr.crit);
    uint8_t runner[0x60]={0};
    struct UpdateFunc a={0},b={0};
    a.flags=b.flags=2;a.func=fake_bare;b.func=fake_bare;
    struct ListNode second={&b,NULL,NULL},first={&a,&second,NULL};
    *(struct ListNode**)(runner+0x18)=&first;G_UPDATE_RUNNER=runner;
    G_MISC_FLAGS=0;G_REPLAY_MANAGER=NULL;
    cfg.substep=1;cfg.subtick_input=1;g_skip_update=0;g_dt=0.25f;g_logical=1;
    /* A boundary tick runs both nodes; every callback is unclassified, so both are MODE_FRAME. */
    g_major=1;test_bare_calls=0;assert(hfr_runner(runner)==2 && test_bare_calls==2);
    /* And a minor tick runs none of them, and must reach the end rather than a null write. */
    g_major=0;test_bare_calls=0;assert(hfr_runner(runner)==2 && test_bare_calls==0);
    /* The catch-up tick is the other thing the frame hook calls on its own initiative, and
       the second TH14 crash: it sets up the game's frame context by hand through three
       addresses and may call the game's cleanup through two more. It has to decline, not
       fault, and the frame hook has to stop asking for extra ticks once it does. */
    game.addr.frame_context_ptr=0;game.addr.frame_flag=0;game.addr.frame_context_value=0;
    game.addr.cleanup_fn=0;game.addr.cleanup_this=0;
    assert(!catchup_available());
    g_major=1;test_bare_calls=0;
    assert(update_only_tick()==0 && test_bare_calls==0);
    DeleteCriticalSection((LPCRITICAL_SECTION)game.addr.crit);
    memset((void*)game.addr.crit,0,sizeof(CRITICAL_SECTION));
    G_UPDATE_RUNNER=NULL;g_game=real;g_major=1;g_dt=1;g_stop_node=NULL;
    /* ... and the answer for the real profile is whatever that profile actually says, which
       is the point: TH10-13 describe these and TH14 does not, and both are legitimate. */
    assert(catchup_available() == (real->addr.frame_context_ptr && real->addr.frame_flag &&
                                   real->addr.frame_context_value && real->addr.cleanup_fn &&
                                   real->addr.cleanup_this ? 1 : 0));
    puts("PASS: a profile that describes only the scheduler survives a pass and a catch-up tick, writing through none of the addresses it lacks");
}

/* ---------------------------------------------------------------- the runner's ending
   The replacement runner is entered by a jump written over the game's own, so the whole
   function is ours from that point -- including its last instruction, where thprac puts its
   menu hook in all four games. It must therefore finish on the game's instruction and not on
   one of its own. Three things to establish: the profile points at a real ending, the choice
   falls back rather than jumping somewhere arbitrary when it does not, and the thunk actually
   lands there with the stack the calling convention promises and the return value intact. */
int test_tail_reached, test_tail_esp;
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _test_tail_pad\n_test_tail_pad:\n"       /* stands in for a game's `ret` ... */
    "  inc dword ptr [_test_tail_reached]\n  ret\n"
    ".globl _test_tail_pad4\n_test_tail_pad4:\n"     /* ... and for TH10's `ret 4` */
    "  inc dword ptr [_test_tail_reached]\n  ret 4\n"
    /* The three ways the game enters the runner: its argument in EBX, on the stack, or -- TH14
       on -- in ECX. Each is a separate thunk in update_runner.c and so a separate caller here;
       an untested one is how a game gets a runner that reads the wrong pointer every frame. */
    ".globl _test_call_runner_ecx_entry\n_test_call_runner_ecx_entry:\n"
    "  mov ecx, [esp+4]\n"
    "  mov [_test_tail_esp], esp\n  call _hfr_runner_ecx_entry\n  sub [_test_tail_esp], esp\n"
    "  ret\n"
    /* The two ways the game enters the runner: its argument in EBX, or on the stack. */
    ".globl _test_call_runner_entry\n_test_call_runner_entry:\n"
    "  push ebx\n  mov ebx, [esp+8]\n"
    "  mov [_test_tail_esp], esp\n  call _hfr_runner_entry\n  sub [_test_tail_esp], esp\n"
    "  pop ebx\n  ret\n"
    ".globl _test_call_runner_stack_entry\n_test_call_runner_stack_entry:\n"
    "  push dword ptr [esp+4]\n"
    "  mov [_test_tail_esp], esp\n  call _hfr_runner_stack_entry\n  sub [_test_tail_esp], esp\n"
    "  ret\n"
    ".att_syntax\n"
);
extern int test_call_runner_entry(void* runner);
extern int test_call_runner_stack_entry(void* runner);
extern int test_call_runner_ecx_entry(void* runner);
extern void test_tail_pad(void), test_tail_pad4(void);

static void test_runner_tail(void) {
    int stack = g_game->runner_arg == RUNNER_ARG_STACK;
    uintptr_t tail = g_game->addr.runner_ret;
    /* The profile's ending is inside the game, past the runner's entry, and is the return
       instruction that matches how the runner takes its argument. */
    assert(tail > g_game->addr.runner_fn && tail < 0x400000 + g_game->identity->image_size);
    assert(*(uint8_t*)tail == (stack ? 0xc2 : 0xc3));
    if (stack) assert(*(uint16_t*)(tail + 1) == 4);
    /* Someone else's breakpoint on that instruction is the case this exists for, not a
       reason to stop using it. Anything that is not an ending at all is. */
    uint8_t real = *(uint8_t*)tail;
    assert(runner_tail_usable(tail, stack));
    *(uint8_t*)tail = 0xcc; assert(runner_tail_usable(tail, stack));
    *(uint8_t*)tail = 0x90; assert(!runner_tail_usable(tail, stack));
    runner_tail_select(); assert(g_runner_tail == (stack ? (void*)hfr_runner_tail_ret4 : (void*)hfr_runner_tail_ret));
    *(uint8_t*)tail = real;
    runner_tail_select(); assert(g_runner_tail == (void*)tail);
    assert(!runner_tail_usable(0, stack));

    /* And now run it. The same fixture test_runner() uses: two nodes, both called, so the
       pass returns 2 and a tail that never ran would be visible as a missing count. */
    const struct GameProfile* real_game = g_game;
    struct GameProfile game = *real_game;
    struct node_class classes[] = {{(uintptr_t)fake_frame,MODE_FRAME,"Frame"},{(uintptr_t)fake_sub,MODE_SUB,"Sub"}};
    game.classes=classes;game.class_count=2;game.addr.player_callback=(uintptr_t)fake_sub;
    test_fill_missing(&game);
    g_game=&game;g_sub_enabled[0]=g_sub_enabled[1]=1;
    InitializeCriticalSection((LPCRITICAL_SECTION)game.addr.crit);
    uint8_t runner[0x60]={0};
    struct UpdateFunc frame={0},sub={0};
    frame.flags=sub.flags=2;frame.func=fake_frame;sub.func=fake_sub;
    struct ListNode second={&sub,NULL,NULL},first={&frame,&second,NULL};
    *(struct ListNode**)(runner+0x18)=&first;G_UPDATE_RUNNER=runner;
    G_MISC_FLAGS=0;G_REPLAY_MANAGER=NULL;G_ENEMY_MANAGER=NULL;*(void**)game.addr.player=NULL;
    cfg.substep=1;cfg.subtick_input=0;g_major=1;g_skip_update=0;g_dt=0.25f;g_logical=1;
    test_result=1;test_frame_calls=test_sub_calls=0;test_tail_reached=0;

    /* A stand-in ending, which records that it ran. The stack it leaves behind is the whole
       point: nothing but the game's own epilogue may have moved it. */
    g_runner_tail = stack ? (void*)test_tail_pad4 : (void*)test_tail_pad;
    int r = stack ? test_call_runner_stack_entry(runner)
          : real_game->runner_arg == RUNNER_ARG_ECX ? test_call_runner_ecx_entry(runner)
          : test_call_runner_entry(runner);
    assert(test_tail_reached == 1);              /* the pass ended on it, not on a ret of ours */
    assert(r == 2);                              /* ... and the return value survived the jump */
    assert(test_tail_esp == (stack ? -4 : 0));   /* cdecl leaves the argument, `ret 4` takes it */

    /* And now the game's own bytes. The harness maps the game as inert data and never
       executes it in place, so they are copied somewhere executable and jumped to there --
       which is the same three bytes reaching the same processor, and would catch an ending
       that returns but does not unwind the stack the way the game's callers expect. */
    uint8_t* copy = VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    assert(copy);
    memcpy(copy, (void*)tail, 3);
    g_runner_tail = copy;
    test_frame_calls=test_sub_calls=0;
    r = stack ? test_call_runner_stack_entry(runner) : test_call_runner_entry(runner);
    assert(r == 2 && test_tail_esp == (stack ? -4 : 0));
    VirtualFree(copy, 0, MEM_RELEASE);

    DeleteCriticalSection((LPCRITICAL_SECTION)game.addr.crit);
    memset((void*)game.addr.crit, 0, sizeof(CRITICAL_SECTION));
    G_UPDATE_RUNNER=NULL;g_game=real_game;g_stop_node=NULL;g_major=1;g_dt=1;g_runner_tail=NULL;
    printf("PASS: the update pass ends on the game's own instruction at 0x%06x, stack and result intact\n",
           (unsigned)tail);
}
