static int test_frame_calls,test_sub_calls,test_result,test_edges;
static int __thiscall fake_frame(void* arg) {++test_frame_calls;return test_result;}
static int __thiscall fake_sub(void* arg) {
    ++test_sub_calls;
    assert(G_GAME_SPEED==g_logical*g_dt);
    test_edges=*(uint32_t*)g_game->addr.game_pressed;
    return 1;
}
static void test_runner(void) {
    const struct GameProfile* real=g_game;
    struct GameProfile game=*real;
    struct node_class classes[]={{(uintptr_t)fake_frame,MODE_FRAME,"Frame"},{(uintptr_t)fake_sub,MODE_SUB,"Sub"}};
    game.classes=classes;game.class_count=2;game.addr.player_callback=(uintptr_t)fake_sub;
    g_game=&game;g_sub_enabled[0]=g_sub_enabled[1]=1;
    uint8_t runner[0x60]={0},manager[0x80]={0};
    struct UpdateFunc frame={0},sub={0};
    frame.flags=sub.flags=2;frame.func=fake_frame;sub.func=fake_sub;
    struct ListNode second={&sub,NULL,NULL},first={&frame,&second,NULL};
    *(struct ListNode**)(runner+0x18)=&first;G_UPDATE_RUNNER=runner;
    G_MISC_FLAGS=0;G_REPLAY_MANAGER=NULL;G_ENEMY_MANAGER=NULL;*(void**)game.addr.player=NULL;
    cfg.substep=1;cfg.subtick_input=0;g_major=1;g_skip_update=0;g_dt=0.25f;g_logical=1;
    *(uint32_t*)game.addr.game_pressed=2;*(uint32_t*)game.addr.game_released=4;G_GAME_INPUT=2;
    test_result=1;test_frame_calls=test_sub_calls=0;
    assert(hfr_runner(runner)==2 && test_frame_calls==1 && test_sub_calls==1 && test_edges==2);
    g_major=0;assert(hfr_runner(runner)==2 && test_frame_calls==1 && test_sub_calls==2);
    assert(test_edges==(game.mask_minor_player_edges?0:2));
    assert(*(uint32_t*)game.addr.game_pressed==2 && *(uint32_t*)game.addr.game_released==4 && G_GAME_INPUT==2);
    assert(G_GAME_SPEED==1);
    g_skip_update=1;assert(hfr_runner(runner)==1 && test_sub_calls==2);g_skip_update=0;
    g_major=1;test_result=3;assert(hfr_runner(runner)==1 && test_sub_calls==2);
    g_major=0;assert(hfr_runner(runner)==1 && test_sub_calls==2);
    /* A new pause raised between boundary ticks must cut off the player. */
    g_stop_node=NULL;game.addr.gm_callback=(uintptr_t)fake_frame;
    *(void**)game.addr.game_manager=manager;*(uint32_t*)(manager+0x60)=0x10;
    assert(hfr_runner(runner)==1 && test_sub_calls==2);
    *(uint32_t*)(manager+0x60)=0;test_result=1;
    assert(hfr_runner(runner)==2 && test_sub_calls==3);
    G_UPDATE_RUNNER=NULL;*(void**)game.addr.game_manager=NULL;g_game=real;g_stop_node=NULL;g_major=1;g_dt=1;
    puts("PASS: shared runner boundary/minor/duplicate ticks, input edges, list stop and immediate pause");
}
