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

static float th18_test_camera_x;
static int th18_test_camera_inactive;
static int __attribute__((fastcall)) th18_test_stage(uint8_t* stage) {
    th18_test_camera_x=*(float*)(stage+0x230);
    if (!th18_test_camera_inactive)
        for(int i=0;i<13;++i) memcpy((void*)(0x4cd478+th18_camera_offsets[i]-0x230),stage+th18_camera_offsets[i],4);
    *(int*)(stage+0x3468)=123; /* Unrelated native draw side effects must survive. */
    return 1;
}
static void test_th18_camera(void) {
    if (g_game!=&th18_profile) return;
    struct Th18CameraHistory h={0};
    float p[13]={0,0,-600, 0,1,0, 0,0,1, 0,0,0, 1},out[13];
    assert(!th18_camera_pose(&h,1,2,1,1,p,0.5,out));
    p[0]=12;
    assert(th18_camera_pose(&h,1,2,2,2,p,0.5,out));assert(out[0]==6);
    assert(th18_camera_pose(&h,1,2,2,2,p,1,out));assert(out[0]==12);
    assert(!th18_camera_pose(&h,1,2,4,4,p,0.5,out)); /* catch-up gap */
    p[0]=1024;assert(!th18_camera_pose(&h,1,2,5,5,p,0.5,out)); /* cut */
    assert(!th18_camera_pose(&h,1,2,6,0,p,0.5,out)); /* stage restart */
    assert(!th18_camera_pose(&h,1,3,7,1,p,0.5,out)); /* new script */
    p[8]=-1;assert(!th18_camera_pose(&h,1,3,8,2,p,0.5,out)); /* flip */
    p[0]=NAN;assert(!th18_camera_pose(&h,1,3,9,3,p,0.5,out));
    const uintptr_t addresses[]={0x41c290,0x41c700,0x409750};
    uint8_t bytes[3][8];DWORD protections[3];
    for(int i=0;i<3;++i) {
        memcpy(bytes[i],(void*)addresses[i],8);
        assert(VirtualProtect((void*)addresses[i],8,PAGE_EXECUTE_READWRITE,&protections[i]));
    }
    uint8_t jump[]={0xb8,0,0,0,0,0xff,0xe0};uintptr_t fn=(uintptr_t)th18_test_stage;
    memcpy(jump+1,&fn,4);
    memcpy((void*)addresses[0],jump,sizeof jump);memcpy((void*)addresses[1],jump,sizeof jump);
    memcpy((void*)addresses[2],"\xff\x49\x04\xc2\x04\x00",6);
    FlushInstructionCache(GetCurrentProcess(),NULL,0);
    uint8_t* stage=calloc(1,0x34a0);assert(stage);
    p[0]=0;p[8]=1;
    for(int i=0;i<13;++i) memcpy(stage+th18_camera_offsets[i],p+i,4);
    struct Th18CameraHistory oldcamera=th18_camera;
    uint64_t oldtick=th18_tick;double oldphase=th18_phase;
    int oldinterp=cfg.enemy_interp,oldrefresh=g_refresh,oldmajor=th18_present_major;
    memset(&th18_camera,0,sizeof th18_camera);cfg.enemy_interp=1;g_refresh=360;th18_tick=1;th18_phase=0.5;
    assert(th18_stage_first(stage)==1);
    th18_tick=2;*(float*)(stage+0x230)=12;
    assert(th18_stage_first(stage)==1);assert(th18_test_camera_x==6);
    assert(th18_stage_second(stage)==1);assert(th18_test_camera_x==6);
    assert(*(float*)(stage+0x230)==12 && *(float*)0x4cd478==12);
    assert(*(int*)(stage+0x3468)==123 && !th18_in_stage_draw);
    th18_test_camera_inactive=1;*(float*)0x4cd478=777;
    assert(th18_stage_second(stage)==1);assert(*(float*)0x4cd478==777);
    assert(*(float*)(stage+0x230)==12);th18_test_camera_inactive=0;
    cfg.enemy_interp=0;assert(th18_stage_second(stage)==1);assert(th18_test_camera_x==12);
    cfg.enemy_interp=1;g_refresh=60;assert(th18_stage_second(stage)==1);assert(th18_test_camera_x==12);
    int timer[4]={0,100,0,0};
    for(int i=0;i<360;++i) {th18_present_major=i%6==0;th18_stage_timer(timer,NULL,NULL);}
    assert(timer[1]==40);
    free(stage);th18_camera=oldcamera;th18_tick=oldtick;th18_phase=oldphase;
    cfg.enemy_interp=oldinterp;g_refresh=oldrefresh;th18_present_major=oldmajor;
    for(int i=2;i>=0;--i) {DWORD ignored;memcpy((void*)addresses[i],bytes[i],8);
        assert(VirtualProtect((void*)addresses[i],8,protections[i],&ignored));}
    FlushInstructionCache(GetCurrentProcess(),NULL,0);
    puts("PASS: TH18 camera interpolation/cuts/restarts; both stage passes restore native inputs; transition timer remains 60 Hz");
}
