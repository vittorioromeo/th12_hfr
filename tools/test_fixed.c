/* No game process: exercise the actual runtime against a private synthetic image. */
#define HFR_NO_UI
#include "../src/hfr64.c"
#include <assert.h>
void hfr_d3d11_overlay(void* p) {(void)p;}
static void test_clock(void) {
    const int rates[]={60,120,144,165,240,360,480,1000};
    for (unsigned r=0;r<sizeof rates/sizeof *rates;++r) {
        struct FixedClock c={0};unsigned updates=0;
        for (int i=0;i<rates[r]*10;++i) {
            double alpha;
            updates+=fixed_clock_step(&c,1000.0+i*60000000.0/rates[r],60000000.0,rates[r],&alpha);
            assert(alpha>=0 && alpha<=1);
        }
        assert(updates==600);
    }
    struct FixedClock c={0};unsigned updates=0;double alpha;
    /* A 360 FPS request blocked to 60 FPS by VSync still gets 60 updates/sec. */
    for (int i=0;i<600;++i) updates+=fixed_clock_step(&c,1000.0+i*1000000.0,60000000.0,360,&alpha);
    assert(updates==600);
    assert(fixed_clock_step(&c,1e12,60000000.0,360,&alpha));
    assert(!fixed_clock_step(&c,1e12+1,60000000.0,360,&alpha));
    puts("PASS: fixed clock 60..1000 FPS, VSync blocking, phase bounds and long stalls");
}
static void test_history(void) {
    struct FixedPose h={0};float p[3]={10,20,0},out[3];
    assert(!fixed_pose(&h,1,2,1,1,p,0,0,out)); /* birth */
    p[0]=20;
    assert(fixed_pose(&h,1,2,2,2,p,.5,0,out) && out[0]==15);
    assert(fixed_pose(&h,1,2,2,2,p,.75,0,out) && out[0]==17.5f); /* no accumulation */
    assert(p[0]==20);
    p[0]=500;
    assert(!fixed_pose(&h,1,2,3,3,p,.5,0,out)); /* teleport */
    p[0]=501;
    assert(!fixed_pose(&h,1,3,4,0,p,.5,0,out)); /* script replacement */
    assert(!fixed_pose(&h,1,3,7,3,p,.5,0,out)); /* absent frames */
    p[0]=502;assert(fixed_pose(&h,1,3,8,4,p,.5,0,out));
    p[0]=503;assert(!fixed_pose(&h,1,3,8,4,p,.5,0,out)); /* reused VM during draw */
    p[0]=NAN;assert(!fixed_pose(&h,1,3,9,5,p,.5,0,out));
    /* Prediction continues past the current position instead of approaching it. */
    struct FixedPose g={0};float q[3]={0,0,0},o[3];
    assert(!fixed_pose(&g,9,9,1,1,q,0,1,o));
    q[0]=10;assert(fixed_pose(&g,9,9,2,2,q,.5,0,o) && o[0]==5);
    assert(fixed_pose(&g,9,9,2,2,q,.5,1,o) && o[0]==15);
    assert(fixed_pose(&g,9,9,2,2,q,0,1,o)==0); /* at phase 0 prediction is the native position */
    ticks=20;
    struct FixedPose* slot=history_slot(1234,1);slot->key=1234;slot->valid=1;slot->tick=ticks;
    reset_vm((void*)1234);assert(!slot->key && !slot->valid);
    puts("PASS: pose history, births, teleports, gaps, repeated draws, VM reuse and nonfinite positions");
}
static void hex(FILE* f,const unsigned char* p,size_t n) {for(size_t i=0;i<n;++i)fprintf(f,"%02x",p[i]);}
static void test_patches(const char* output) {
    base=(uintptr_t)VirtualAlloc(NULL,game->image_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(base);
    for (size_t i=0;i<game->signature_count;++i) {
        const struct FixedSignature* s=&game->signatures[i];memcpy((void*)(base+s->rva),s->bytes,s->size);
    }
    /* Fail a late preflight: not even the earlier queued patches may commit. */
    unsigned char* bad=(void*)(base+game->post_update);bad[0]^=1;
    assert(!prepare_patches());assert(!patch_commit());
    assert(!memcmp((void*)(base+game->update_calls[0]),site_expected(base+game->update_calls[0],5),5));
    bad[0]^=1;
    VirtualFree(relay_page,0,MEM_RELEASE);relay_page=NULL;relay_used=0;
    assert(prepare_patches());assert(g_patch_count==14);
    for (size_t i=0;i<g_patch_count;++i) assert(!memcmp((void*)g_patches[i].addr,g_patches[i].before,g_patches[i].size));
    assert(patch_commit());
    for (size_t i=0;i<g_patch_count;++i) assert(!memcmp((void*)g_patches[i].addr,g_patches[i].after,g_patches[i].size));
    /* The emitted code must end up read-only and executable, but the two words the runtime
       and the relocated site write every frame must stay writable. Getting this wrong
       faults on the first native tick, so check the protections that were actually applied. */
    MEMORY_BASIC_INFORMATION code={0},data={0};
    assert(VirtualQuery(relay_page,&code,sizeof code)==sizeof code);
    assert(code.Protect==PAGE_EXECUTE_READ);
    assert(player_factor && player_ran);
    assert(VirtualQuery((void*)player_factor,&data,sizeof data)==sizeof data);
    assert(data.Protect==PAGE_READWRITE);
    assert((unsigned char*)player_ran>=(unsigned char*)data.BaseAddress &&
           (unsigned char*)player_ran<(unsigned char*)data.BaseAddress+data.RegionSize);
    *player_factor=0.0f;*player_ran=1;      /* what update_first and the relay do */
    assert(*player_factor==0.0f && *player_ran==1);
    *player_factor=1.0f;*player_ran=0;
    FILE* f=fopen(output,"w");assert(f);
    fprintf(f,"{\"base\":%llu,\"relay\":%llu,\"factor\":%llu,\"ran\":%llu,"
              "\"proj_minor\":%llu,\"proj_dt\":%llu,\"proj_ran\":%llu,\"relay_hex\":\"",
        (unsigned long long)base,(unsigned long long)(uintptr_t)relay_page,
        (unsigned long long)(uintptr_t)player_factor,(unsigned long long)(uintptr_t)player_ran,
        (unsigned long long)(uintptr_t)proj_minor,(unsigned long long)(uintptr_t)proj_dt,
        (unsigned long long)(uintptr_t)proj_ran);
    hex(f,relay_page,relay_used);fprintf(f,"\",\"patches\":[");
    for(size_t i=0;i<g_patch_count;++i) {
        fprintf(f,"%s{\"rva\":%llu,\"hex\":\"",i?",":"",(unsigned long long)(g_patches[i].addr-base));
        hex(f,g_patches[i].after,g_patches[i].size);fprintf(f,"\"}");
    }
    fprintf(f,"]}");fclose(f);
    VirtualFree(relay_page,0,MEM_RELEASE);VirtualFree((void*)base,0,MEM_RELEASE);
    puts("PASS: actual x64 patch transaction, failed preflight leaves code intact, relays emitted");
}
/* The sub-tick pass: slices sum to one frame, and it stands aside when it must. */
static void test_subtick(void) {
    struct SubtickPlayer s={0};double total=0;
    for (int i=1;i<=6;++i) total+=subtick_slice(&s,i/6.0,1);
    assert(fabs(total-1.0)<1e-12);
    assert(subtick_slice(&s,1.0,1)==0);        /* time did not advance */
    assert(subtick_slice(&s,1.5,0)==0);        /* not armed: the slice is consumed, not applied */
    assert(subtick_slice(&s,2.0,1)==0.5);      /* and the next one starts from there */
    assert(subtick_slice(&s,90.0,1)==0);       /* a stall belongs to the native tick */
    assert(subtick_slice(&s,90.5,1)==0.5);
    const float straight[2]={4,2},diagonal[2]={3,1.5f};float x,y;
    subtick_direction(0,straight,diagonal,&x,&y);assert(x==0&&y==0);
    subtick_direction(SUBTICK_UP,straight,diagonal,&x,&y);assert(x==0&&y==-4);
    subtick_direction(SUBTICK_DOWN|SUBTICK_FOCUS,straight,diagonal,&x,&y);assert(x==0&&y==2);
    subtick_direction(SUBTICK_LEFT,straight,diagonal,&x,&y);assert(x==-4&&y==0);
    subtick_direction(SUBTICK_LEFT|SUBTICK_RIGHT,straight,diagonal,&x,&y);assert(x==4); /* right wins */
    subtick_direction(SUBTICK_UP|SUBTICK_DOWN,straight,diagonal,&x,&y);assert(y==-4);   /* up wins */
    subtick_direction(SUBTICK_UP|SUBTICK_LEFT,straight,diagonal,&x,&y);assert(x==-3&&y==-3);
    subtick_direction(SUBTICK_DOWN|SUBTICK_RIGHT|SUBTICK_FOCUS,straight,diagonal,&x,&y);assert(x==1.5f&&y==1.5f);
    assert(subtick_clamp(5,10,100)==10);assert(subtick_clamp(200,10,100)==110);assert(subtick_clamp(50,10,100)==50);
    puts("PASS: sub-tick slices sum to one frame, stand aside when unarmed or stalled, direction and clamp");
}
/* The sub-step schedule: exactly 60 frames per second of ticks, every step dyadic, and
   the steps of each frame summing to exactly one frame with no tick straddling a boundary. */
static void test_substep(void) {
    const int rates[]={60,120,144,165,240,360,480,1000};
    for (unsigned r=0;r<sizeof rates/sizeof *rates;++r) {
        struct Substep s; substep_reset(&s);
        unsigned majors=0,ticks=0; double total=0,in_frame=0; int first=1;
        double last_phase=-1;
        for (int i=0;i<rates[r];++i) {
            struct SubstepTick t=substep_advance(&s,rates[r]);
            assert(t.phase>=0 && t.phase<1);
            assert(t.dt>0 && t.dt<=1.0f);
            /* dyadic: a whole number of 1/256 frames, so exact in float32 */
            assert((double)t.dt*256.0==(double)(long)((double)t.dt*256.0));
            if (t.major) {
                ++majors;
                if (!first) assert(in_frame==1.0);   /* the frame just ended exactly */
                first=0; in_frame=0; assert(t.phase==0);
            } else {
                assert(t.phase>last_phase);          /* phase advances inside a frame */
            }
            last_phase=t.phase; in_frame+=t.dt; total+=t.dt; ++ticks;
        }
        assert(in_frame==1.0);         /* the last frame of the second closed too */
        assert(majors==60);            /* 60 frames of logic per second, at every rate */
        assert(ticks==(unsigned)rates[r]);
        assert(total==60.0);           /* and no drift over the whole second */
    }
    /* At 60 the feature is inert: every tick is a boundary and carries a whole frame. */
    struct Substep s; substep_reset(&s);
    for (int i=0;i<10;++i) {
        struct SubstepTick t=substep_advance(&s,60);
        assert(t.major && t.dt==1.0f && t.phase==0);
    }
    /* A rate that is not a multiple of 60 deals uneven tick counts but exact frames. */
    struct Substep u; substep_reset(&u); unsigned seen[4]={0};
    for (int i=0;i<144;++i) {
        struct SubstepTick t=substep_advance(&u,144);
        if (t.major) { unsigned k=u.ticks_left+1; assert(k==2||k==3); seen[k]++; }
    }
    assert(seen[2] && seen[3]);
    puts("PASS: sub-step schedule, 60 frames/s at every rate, dyadic steps, exact frame sums");
}
int main(int argc,char** argv) {
    assert(argc==2);game=fixed_games[0];test_clock();test_history();test_subtick();test_substep();test_patches(argv[1]);return 0;
}
