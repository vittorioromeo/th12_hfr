/* Native x86 regression harness. Maps a local game image as inert test data;
   never starts the game or invokes its imports. The linker reserves .fixture
   at 0x400000 so Windows cannot place the test process's heap/stack there. */
#define HFR_NO_UI
#include "../src/hfr.c"
#include <assert.h>
#include "test_replay.h"
#include "test_runner.h"
#include "test_scaler.h"
static uint8_t test_fixture[0x200000] __attribute__((section(".fixture")));


static void test_schedule(void) {
    cfg.substep=1;
    for (int rate=60;rate<=1000;++rate) {
        set_logic_rate(rate);
        unsigned major=0, units=0;
        for(int i=0;i<rate*10;++i) {
            advance_tick(); major+=g_major; units+=(unsigned)(g_dt*256);
            assert(g_dt>0 && g_dt<=1);
        }
        assert(major==600 && units==600*256);
    }
    const int rates[]={60,120,144,165,240,360,1000};
    for(size_t a=0;a<sizeof rates/sizeof *rates;++a)
        for(size_t b=0;b<sizeof rates/sizeof *rates;++b) {
            g_refresh=rates[a];set_logic_rate(rates[b]);
            unsigned ticks=0;
            for(int i=0;i<g_refresh;++i)ticks+=ticks_for_slot();
            assert(ticks==(unsigned)g_logic_rate);
        }
    cfg.substep=0;recompute_rate(360);
    assert(g_logic_rate==60 && g_refresh==360);
    cfg.substep=1;set_logic_rate(144);advance_tick();
    unsigned first=g_last_units;advance_tick();advance_tick();schedule_reset_here();
    assert(g_last_units==first && g_units_total==first && g_phase==0);
    g_replay_playing=1;g_replay_rate=144;
    advance_tick();unsigned tick=g_tick,acc=g_units_acc;
    recompute_rate(360);
    assert(g_logic_rate==144 && g_tick==tick && g_units_acc==acc);
    g_replay_playing=0;g_replay_rate=0;
    puts("PASS: scheduler at every integer rate 60..1000, cross-rate presentation, stage reset, stock mode");
}
static void test_replay_parser(void) {
    uint8_t chunk[40]={0};memcpy(chunk+12,"HFRI",4);
    uint16_t ver=1,rate=360;memcpy(chunk+16,&ver,2);memcpy(chunk+18,&rate,2);
    chunk[20]=1;chunk[24]=1;chunk[28]=5;chunk[32]=2;
    chunk[36]=3;chunk[37]=2;chunk[38]=17;chunk[39]=3;
    replay_parse_input_chunk(chunk,sizeof chunk);
    assert(g_play[1].n==5 && g_play[1].d[0]==3 && g_play[1].d[4]==17);
    chunk[32]=0xff;chunk[33]=0xff;chunk[34]=0xff;chunk[35]=0xff;
    replay_parse_input_chunk(chunk,sizeof chunk);assert(g_play[1].n==0);
    memset(chunk+32,0,4);chunk[32]=2;chunk[39]=4;
    replay_parse_input_chunk(chunk,sizeof chunk);assert(g_play[1].n==0);
    chunk[39]=3;chunk[36]=255;
    replay_parse_input_chunk(chunk,sizeof chunk);assert(g_play[1].n==0);
    puts("PASS: replay RLE decoding and malformed lengths/runs/input bits");
}
static void dump(const char* prefix,const char* suffix,const void* data,size_t n) {
    char name[MAX_PATH];snprintf(name,sizeof name,"%s%s",prefix,suffix);
    FILE*f=fopen(name,"wb");assert(f);assert(fwrite(data,1,n,f)==n);fclose(f);
}
int main(int argc,char**argv) {
    assert(argc==3);
    uint8_t*base=test_fixture;assert(base==(void*)0x400000);
    FILE*f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    uint8_t* file=malloc(n);assert(file && fread(file,1,n,f)==(size_t)n);fclose(f);
    IMAGE_NT_HEADERS*nt=(void*)(file+((IMAGE_DOS_HEADER*)file)->e_lfanew);
    assert(nt->OptionalHeader.ImageBase==0x400000);
    assert(nt->OptionalHeader.SizeOfImage<=sizeof test_fixture);
    memcpy(base,file,nt->OptionalHeader.SizeOfHeaders);
    IMAGE_SECTION_HEADER*s=IMAGE_FIRST_SECTION(nt);
    for(int i=0;i<nt->FileHeader.NumberOfSections;++i)
        if(s[i].SizeOfRawData)memcpy(base+s[i].VirtualAddress,file+s[i].PointerToRawData,s[i].SizeOfRawData);
    assert(select_game(base,nt->OptionalHeader.SizeOfImage));
    const struct GameIdentity* id=g_game->identity;
    for(size_t i=0;i<id->signature_count;++i) {
        uint8_t* p=(uint8_t*)id->signatures[i].addr;
        *p^=1;assert(!identify_image(base,nt->OptionalHeader.SizeOfImage));*p^=1;
    }
    assert(!identify_image(base,64));
    puts("PASS: executable detection rejects every individually modified signature and truncated headers");

    /* The guard against another patch owning the frame loop. The clean executable that was
       just identified must not trip it -- a false positive here would refuse to install for
       someone whose game is fine -- and every guarded site must be noticed on its own. */
    assert(conflict_scan(id,base,nt->OptionalHeader.SizeOfImage)==-1);
    for (size_t i=0;i<id->conflict_count;++i) {
        uint8_t* p=(uint8_t*)id->conflicts[i].addr;
        for (size_t b=0;b<id->conflicts[i].size;++b) {
            p[b]^=0xff;
            assert(conflict_scan(id,base,nt->OptionalHeader.SizeOfImage)>=0);
            p[b]^=0xff;
        }
        /* vpatch writes a jump over the site; that is what this must catch in practice. */
        uint8_t saved[8];memcpy(saved,p,id->conflicts[i].size);
        p[0]=0xe9;assert(conflict_scan(id,base,nt->OptionalHeader.SizeOfImage)==(int)i);
        memcpy(p,saved,id->conflicts[i].size);
    }
    assert(conflict_scan(id,base,nt->OptionalHeader.SizeOfImage)==-1);
    /* Identification must not depend on these sites: a game patched by vpatch is still the
       game, and must be reported as a conflict rather than as an unsupported executable. */
    for (size_t i=0;i<id->conflict_count;++i) {
        uint8_t* p=(uint8_t*)id->conflicts[i].addr;
        uint8_t saved=*p;*p=0xe9;
        assert(identify_image(base,nt->OptionalHeader.SizeOfImage)==id);
        *p=saved;
    }
    /* A game with no conflict sites recorded yet must pass, not fail. */
    for (size_t g=0;g<GAME_COUNT;++g)
        if (!game_identities[g].conflicts)
            assert(conflict_scan(&game_identities[g],base,nt->OptionalHeader.SizeOfImage)==-1);
    printf("PASS: %u frame-loop sites guarded against another patch, and a clean executable trips none\n",
        (unsigned)id->conflict_count);
    test_schedule();test_replay_parser();test_replay_roundtrip();test_runner();test_scale_rect();test_snap_client();test_menu_key();
    /* A failed patch transaction must leave all game code unchanged. */
    patch_begin();uint8_t changed[6]={0};
    uintptr_t addr=id->signatures[0].addr;
    assert(patch_bytes(addr,changed,5,NULL));
    assert(!patch_bytes(addr+1,changed,5,NULL));
    assert(!patch_commit());assert(!memcmp((void*)addr,id->signatures[0].bytes,5));
    cfg.subtick_input=1;cfg.d3d9ex=1;
    assert(install() && !g_patch_failed);
    assert(orig_Direct3DCreate9 && orig_D3DXCreateTexture && orig_D3DXCreateTextureFromFileInMemoryEx && orig_joyGetPosEx);
    puts("PASS: complete patch plan has frozen signatures, no overlaps; failed transaction leaves code intact");
    dump(argv[2],".game",base,nt->OptionalHeader.SizeOfImage);
    dump(argv[2],".stubs",g_stub_mem,g_stub_used);
    char info[1024];snprintf(info,sizeof info,
        "{\"stub_base\":%u,\"major\":%u,\"factor\":%u,\"logical\":%u,\"residual\":%u,\"ptf_prev\":%u,\"ptf_cur\":%u}",
        (unsigned)g_stub_mem,(unsigned)&g_major,(unsigned)&g_factor,(unsigned)&g_logical,
        (unsigned)g_move_residual,(unsigned)&g_ptf_prev,(unsigned)&g_ptf_cur);
    dump(argv[2],".json",info,strlen(info));
    printf("PASS: %u executable signatures; emitted %u bytes of real hook stubs\n",
        (unsigned)id->signature_count,(unsigned)g_stub_used);
    free(file);return 0;
}
