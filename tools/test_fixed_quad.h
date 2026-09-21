/* Pure geometry regression coverage shared by the native-60 adapters. */
static void test_fixed_quad(void) {
    struct FixedQuadHistory h = {0};
    float p[4][3]={{-2,-2,0},{2,-2,0},{-2,2,0},{2,2,0}}, out[4][3];
    assert(!fixed_quad_pose(&h,1,2,1,1,p,0.5,0,out));
    for (int i=0;i<4;++i) p[i][0]+=8;
    assert(fixed_quad_pose(&h,1,2,2,2,p,0.5,0,out));
    assert(fabsf(out[0][0]-2)<1e-5f);
    assert(fixed_quad_pose(&h,1,2,2,2,p,0.5,1,out));
    assert(fabsf(out[0][0]-10)<1e-5f);
    /* One VM reused for two glyphs must stay untracked across subsequent ticks. */
    p[0][0]+=1;
    assert(!fixed_quad_pose(&h,1,2,2,2,p,0.5,0,out));
    p[0][0]+=1;
    assert(!fixed_quad_pose(&h,1,2,3,3,p,0.5,0,out));
    assert(!fixed_quad_pose(&h,1,3,4,0,p,0.5,0,out)); /* new script */
    assert(!fixed_quad_pose(&h,3,3,6,1,p,0.5,0,out)); /* key/gap */
    for(int i=0;i<4;++i) p[i][0]+=100;
    assert(!fixed_quad_pose(&h,3,3,7,2,p,0.5,0,out)); /* teleport */
    p[0][0]=NAN;
    assert(!fixed_quad_pose(&h,3,3,8,3,p,0.5,0,out));
    /* A rigid rotation keeps its radius rather than shrinking along a chord. */
    memset(&h,0,sizeof h);
    for(int i=0;i<4;++i) {h.previous[i][0]=(i&1)?2:-2;h.previous[i][1]=(i&2)?2:-2;
        h.current[i][0]=h.previous[i][0]*cosf(0.4f)-h.previous[i][1]*sinf(0.4f);
        h.current[i][1]=h.previous[i][0]*sinf(0.4f)+h.previous[i][1]*cosf(0.4f);}
    for(int predict=0;predict<2;++predict) {
        fixed_quad_shape(&h,0.5,predict,out);
        for(int i=0;i<4;++i) assert(fabsf(out[i][0]*out[i][0]+out[i][1]*out[i][1]-8)<1e-4f);
    }
    puts("PASS: shared quad interpolation/prediction, rotation, VM reuse, script reset, gaps and teleports");
}
/* These replace native callees with inert, generated RET stubs. No game code runs. */
static void test_th18_clock(void) {
    if (g_game != &th18_profile) return;
    const uintptr_t addrs[]={0x4012e0,0x41b330,0x402b30};
    unsigned char saved[3][6]; DWORD protection[3];
    for(int i=0;i<3;++i) assert(VirtualProtect((void*)addrs[i],6,PAGE_EXECUTE_READWRITE,&protection[i]));
    for(int i=0;i<3;++i) memcpy(saved[i],(void*)addrs[i],6);
    unsigned char success[]={0xb8,1,0,0,0,0xc3};
    memcpy((void*)addrs[0],success,6);
    memcpy((void*)addrs[1],"\xc2\x04\x00",3);
    memcpy((void*)addrs[2],"\xc3",1);
    FlushInstructionCache(GetCurrentProcess(),NULL,0);
    uint64_t oldtick=th18_tick; int oldskip=g_skip_update;
    int oldmajor=th18_present_major;
    for (int rate=60;rate<=360;rate+=(rate==60?84:rate==144?21:rate==165?75:120)) {
        cfg.substep=0;g_refresh=rate;set_logic_rate(60);
        th18_tick=0;
        for(int i=0;i<rate;++i) {g_skip_update=ticks_for_slot()==0;assert(th18_update(NULL)==1);}
        assert(th18_tick==60);
    }
    for(int r=-1;r<=1;++r) {
        memcpy((void*)(addrs[0]+1),&r,4);
        FlushInstructionCache(GetCurrentProcess(),NULL,0);
        uint64_t tick=th18_tick;
        assert(th18_update_only()==(r<0?2:r==0?1:0));assert(th18_tick==tick+1);
        g_skip_update=1;assert(th18_update(NULL)==1);assert(th18_tick==tick+1);
    }
    for(int i=0;i<3;++i) { DWORD ignored; memcpy((void*)addrs[i],saved[i],6);
        assert(VirtualProtect((void*)addrs[i],6,protection[i],&ignored)); }
    FlushInstructionCache(GetCurrentProcess(),NULL,0);
    th18_tick=oldtick;g_skip_update=oldskip;th18_present_major=oldmajor;
    puts("PASS: TH18 exactly 60 native updates at 60/144/165/240/360 Hz; catch-up/exit/skip semantics");
}
