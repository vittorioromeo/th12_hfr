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
    assert(prepare_patches());assert(g_patch_count==7);
    for (size_t i=0;i<g_patch_count;++i) assert(!memcmp((void*)g_patches[i].addr,g_patches[i].before,g_patches[i].size));
    assert(patch_commit());
    for (size_t i=0;i<g_patch_count;++i) assert(!memcmp((void*)g_patches[i].addr,g_patches[i].after,g_patches[i].size));
    FILE* f=fopen(output,"w");assert(f);
    fprintf(f,"{\"base\":%llu,\"relay\":%llu,\"relay_hex\":\"",(unsigned long long)base,(unsigned long long)(uintptr_t)relay_page);
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
int main(int argc,char** argv) {
    assert(argc==2);game=fixed_games[0];test_clock();test_history();test_subtick();test_patches(argv[1]);return 0;
}
