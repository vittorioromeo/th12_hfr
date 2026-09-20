/* Native x86 regression harness. Maps a local game image as inert test data;
   never starts the game or invokes its imports. The linker reserves .fixture
   at 0x400000 so Windows cannot place the test process's heap/stack there. */
#define HFR_NO_UI
#include "../src/hfr.c"
#include <assert.h>
#include "test_replay.h"
#include "test_runner.h"
#include "test_speed.h"
#include "test_scaler.h"
static uint8_t test_fixture[0x1600000] __attribute__((section(".fixture")));


/* The scheduler is game-independent, but recompute_rate now asks the profile whether anything
   can be sub-stepped at all -- correctly, since a rate above 60 is meaningless otherwise. So
   give it a profile that says yes, and test the scheduler rather than the profile. */
static const struct node_class test_schedule_classes[] = {{0x1000, MODE_SUB, "Fake"}};
static void test_schedule(void) {
    const struct GameProfile* schedule_real = g_game;
    struct GameProfile schedule_game = *schedule_real;
    schedule_game.classes = test_schedule_classes; schedule_game.class_count = 1;
    g_game = &schedule_game;
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
    g_game = schedule_real;
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

/* The loader's import resolution, for the fixture. Libraries that are not present are left
   alone: the runtime already copes with an import it cannot reach. */
static void resolve_fixture_imports(uint8_t* base) {
    IMAGE_NT_HEADERS* nt=(void*)(base+((IMAGE_DOS_HEADER*)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return;
    for (IMAGE_IMPORT_DESCRIPTOR* imp=(void*)(base+dir.VirtualAddress); imp->Name; ++imp) {
        const char* dll=(const char*)(base+imp->Name);
        HMODULE mod=LoadLibraryA(dll);
        if (!mod || !imp->OriginalFirstThunk) continue;
        IMAGE_THUNK_DATA* t=(void*)(base+imp->FirstThunk);
        IMAGE_THUNK_DATA* o=(void*)(base+imp->OriginalFirstThunk);
        for (; o->u1.AddressOfData; ++t,++o) {
            FARPROC p = (o->u1.Ordinal & IMAGE_ORDINAL_FLAG)
                ? GetProcAddress(mod,(LPCSTR)(o->u1.Ordinal & 0xffff))
                : GetProcAddress(mod,(const char*)((IMAGE_IMPORT_BY_NAME*)(base+o->u1.AddressOfData))->Name);
            if (p) t->u1.Function=(uintptr_t)p;
        }
    }
}

/* ------------------------------------------------- surviving another patch's import detour
 * A translation patch that injects after this one -- thcrap stops the game's thread at the
 * executable's entry point, which is after the loader has run this DLL's DllMain -- rewrites
 * the same import table, matching by name and chaining to GetProcAddress rather than to the
 * pointer it replaced. This patch is then not called again, silently.
 *
 * What has to hold is that taking the import back afterwards puts the other patch *inside*
 * this one rather than throwing it away, so both still run; and that this patch never does to
 * anyone else what was done to it. Both are checked here against thcrap's actual algorithm,
 * reproduced on the fixture's own import table. */
static IDirect3D9* __stdcall other_patch_d3d9(UINT sdk) { (void)sdk; return NULL; }
static void* thcrap_style_detour(const char* dll, const char* func, void* theirs) {
    /* Their chain pointer, the way thcrap builds it: the library's own export, never the
       pointer they are replacing. */
    void* chain=(void*)GetProcAddress(GetModuleHandleA(dll),func);
    void** slot=iat_slot(dll,func);
    assert(slot);
    assert(iat_write(slot,theirs));   /* overwritten unconditionally, matched by name */
    return chain;
}
/* Calls into the game keep their calling convention. Each probe answers with where its argument
   arrived, so a call compiled with the wrong convention -- which is what GCC made of two calls
   through one pointer differing only in the convention attribute -- is an assertion here rather
   than a crash on TH14's first frame. */
extern int probe_ecx(void), probe_stack(void), probe_ecx_stack(void);
__asm__(".intel_syntax noprefix\n"
        ".globl _probe_ecx\n_probe_ecx:\n  mov eax, ecx\n  ret\n"
        ".globl _probe_stack\n_probe_stack:\n  mov eax, [esp+4]\n  ret 4\n"
        ".globl _probe_ecx_stack\n_probe_ecx_stack:\n  mov eax, ecx\n  xor eax, [esp+4]\n  ret 4\n"
        ".att_syntax\n");
static void test_calling_conventions(void) {
    const struct GameProfile* real = g_game; struct GameProfile game = {0};
    g_game = &game;
    game.addr.frame_fn = (uintptr_t)probe_ecx; game.frame_ctx_ecx = 1;
    assert(frame_call_original((void*)0x1234567) == 0x1234567);
    game.addr.frame_fn = (uintptr_t)probe_stack; game.frame_ctx_ecx = 0;
    assert(frame_call_original((void*)0x7654321) == 0x7654321);
    assert(call_this0((uintptr_t)probe_ecx, (void*)0x1111) == 0x1111);
    assert(call_this1((uintptr_t)probe_ecx_stack, (void*)0x1100, (void*)0x0011) == 0x1111);
    g_game = real;
    puts("PASS: stdcall and ECX calls into the game keep their conventions");
}
static void test_import_redirection(void) {
    /* The Direct3D factory the game imports: Direct3DCreate9, or Direct3DCreate8 for an engine
       that reaches Direct3D 9 through the bridge. Same slot discipline either way. */
    const char* dll = g_game->d3d8 ? "d3d8.dll" : "d3d9.dll";
    const char* func = g_game->d3d8 ? "Direct3DCreate8" : "Direct3DCreate9";
    void* ours = g_game->d3d8 ? (void*)hook_Direct3DCreate8 : (void*)hook_Direct3DCreate9;
    void** orig = g_game->d3d8 ? (void**)&orig_Direct3DCreate8 : (void**)&orig_Direct3DCreate9;
    void** slot=iat_slot(dll,func);
    assert(slot && *slot==ours);                                 /* the import was taken */
    assert(*orig && *orig!=ours);

    /* They take it, and this patch is out of the call path entirely. */
    void* theirs_chain=thcrap_style_detour(dll,func,(void*)other_patch_d3d9);
    assert(*slot==(void*)other_patch_d3d9);
    assert(theirs_chain!=ours);

    /* Taking it back must not repeat their mistake: they have to end up as what this patch
       calls, not as something discarded. */
    assert(reassert_imports()>=1);
    assert(*slot==ours);                                         /* ours again */
    assert(*orig==(void*)other_patch_d3d9);                      /* and they are next */

    /* Idempotent: nothing to take back when nothing has changed hands. */
    assert(reassert_imports()==0);
    assert(*orig==(void*)other_patch_d3d9);

    /* Nothing outside the game's own import table is touched. The process's view of these
       libraries -- which is what every other module in it resolves, overlays included -- has
       to be exactly what it was; redirecting an export instead was tried, and it crashed the
       game under the Steam overlay. */
    struct { const char* dll; const char* func; void* hook; } exports[] = {
        {"d3d9.dll","Direct3DCreate9",(void*)hook_Direct3DCreate9},
        {"winmm.dll","joyGetPosEx",(void*)hook_joyGetPosEx},
        {"user32.dll","ShowCursor",(void*)hook_ShowCursor},
    };
    for (size_t i=0;i<sizeof exports/sizeof *exports;++i) {
        HMODULE m=GetModuleHandleA(exports[i].dll);
        if (m) assert((void*)GetProcAddress(m,exports[i].func)!=exports[i].hook);
    }
    puts("PASS: an import taken by another patch is taken back with that patch chained to,\n"
         "      and no library's exports are altered for the rest of the process");
}

int main(int argc,char**argv) {
    /* Unbuffered, so that a run which hangs still shows how far it got. Under Wine stdout is
       block-buffered into a pipe and a hang otherwise prints nothing at all. */
    setvbuf(stdout, NULL, _IONBF, 0);
    g_log = stdout;   /* the patch's own log lines are the diagnosis when install() refuses */
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
    /* Resolve the fixture's imports the way the loader does before any DLL sees the process.
       Without this the import slots hold the raw file's unresolved thunks, and every check
       about who owns an import -- which is the whole question when another patch is present
       -- would be answered against values no running game ever has. */
    resolve_fixture_imports(base);
    assert(select_game(base,nt->OptionalHeader.SizeOfImage));
    const struct GameIdentity* id=g_game->identity;
    for(size_t i=0;i<id->signature_count;++i) {
        uint8_t* p=(uint8_t*)id->signatures[i].addr;
        *p^=1;assert(!identify_image(base,nt->OptionalHeader.SizeOfImage));*p^=1;
    }
    assert(!identify_image(base,64));
    puts("PASS: executable detection rejects every individually modified signature and truncated headers");

    /* A Steam release is this same executable wrapped: an extra section holds a stub, the
       entry point moves into it, and .text is encrypted until the stub runs. Identification
       must survive the parts that are not the code -- the bigger image, the extra section --
       and must still refuse while the code is unreadable, because at that moment it really
       cannot tell which game this is. Getting the first of those wrong is what made every
       Steam copy of these games unpatchable. */
    {
        /* The headers that matter are the ones in the mapped fixture, not in the file buffer:
           that is what both the runtime and this test read. */
        IMAGE_NT_HEADERS* bnt=(void*)(base+((IMAGE_DOS_HEADER*)base)->e_lfanew);
        IMAGE_SECTION_HEADER* sec=IMAGE_FIRST_SECTION(bnt);
        unsigned count=bnt->FileHeader.NumberOfSections;
        uint32_t plain_size=bnt->OptionalHeader.SizeOfImage, plain_entry=bnt->OptionalHeader.AddressOfEntryPoint;
        IMAGE_SECTION_HEADER stub=sec[count-1];
        memcpy(stub.Name,".bind\0\0",8);
        stub.VirtualAddress=plain_size; stub.Misc.VirtualSize=0x2000;
        assert(!wrapped_executable(base,plain_size));
        assert(identify_image(base,plain_size)==id);

        sec[count]=stub; bnt->FileHeader.NumberOfSections=count+1;
        bnt->OptionalHeader.SizeOfImage=plain_size+0x2000;
        bnt->OptionalHeader.AddressOfEntryPoint=stub.VirtualAddress+0x10;
        assert(bnt->OptionalHeader.SizeOfImage<=sizeof test_fixture);

        /* Wrapped, but the code is in the clear: this is the state the runtime reaches after
           the stub has decrypted, and it must identify exactly as the unwrapped build does. */
        assert(wrapped_executable(base,bnt->OptionalHeader.SizeOfImage));
        assert(identify_image(base,bnt->OptionalHeader.SizeOfImage)==id);

        /* Wrapped with the code still encrypted: refuse, and say it is the wrapper. The
           whole code section goes, as the real wrapper encrypts it -- scrambling only the
           start would leave games whose first signature sits further in still identifying. */
        uint8_t* code=base+sec[0].VirtualAddress;
        uint32_t code_size=sec[0].Misc.VirtualSize;
        for (uint32_t i=0;i<code_size;++i) code[i]^=0xa5;
        assert(!identify_image(base,bnt->OptionalHeader.SizeOfImage));
        assert(wrapped_executable(base,bnt->OptionalHeader.SizeOfImage));
        for (uint32_t i=0;i<code_size;++i) code[i]^=0xa5;

        /* A stub section under a name nobody has seen before is still a stub, because the
           entry point of a wrapped image is outside the game's own code section. */
        memcpy(sec[count].Name,".zzz\0\0\0",8);
        assert(wrapped_executable(base,bnt->OptionalHeader.SizeOfImage));

        /* An image smaller than the build we know is never that build. The floor is the
           profile's own image size, not this file's: an English or Steam build carries extra
           sections of its own, so shrinking it by a fixed amount can still leave it above the
           floor and identifying correctly -- which is how this test used to pass everywhere
           except on th13e.exe, the one executable that has a section the JP build does not. */
        bnt->OptionalHeader.SizeOfImage=id->image_size-1;
        assert(!identify_image(base,plain_size));

        bnt->FileHeader.NumberOfSections=count;
        bnt->OptionalHeader.SizeOfImage=plain_size;
        bnt->OptionalHeader.AddressOfEntryPoint=plain_entry;
        memset(&sec[count],0,sizeof sec[count]);
        assert(!wrapped_executable(base,plain_size));
        assert(identify_image(base,plain_size)==id);
        puts("PASS: a DRM-wrapped image identifies once its code is readable, and is named as wrapped while it is not");
    }

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
    /* thprac's launcher stamps four bytes into the DOS header padding before the PE header,
       which is how this patch knows who started the game. A game as it ships has zeroes
       there, so a clean image must not be mistaken for one thprac launched. */
    {
        uint32_t lfanew=*(uint32_t*)(base+0x3c);
        uint8_t saved[4];memcpy(saved,base+lfanew-4,4);
        assert(!thprac_present());
        memcpy(base+lfanew-4,"PRAC",4);assert(thprac_present());
        memcpy(base+lfanew-4,"PRAX",4);assert(!thprac_present());
        memcpy(base+lfanew-4,saved,4);assert(!thprac_present());
        puts("PASS: thprac's launcher marker is recognised; a clean executable does not carry it");
    }
    /* A game with no conflict sites recorded yet must pass, not fail. */
    for (size_t g=0;g<GAME_COUNT;++g)
        if (!game_identities[g].conflicts)
            assert(conflict_scan(&game_identities[g],base,nt->OptionalHeader.SizeOfImage)==-1);
    printf("PASS: %u frame-loop sites guarded against another patch, and a clean executable trips none\n",
        (unsigned)id->conflict_count);
    /* Some of these drive the game's own structures through the profile's addresses. A profile
       that does not describe the simulation has none, so run what does not depend on one and
       say which were skipped -- the same split install() makes. */
    int sim = g_game->addr.runner_fn && g_game->addr.frame_calls[0];
    /* And a third state between "no simulation" and "everything": the scheduler is described
       but the UpdateFunc class table is not, so the game runs at the display's rate with its
       simulation at 60 Hz. The runner tests build their own class table and hold either way;
       the replay round-trip drives a real sub-step switch and needs a real class. */
    int subs = sim && g_class_count;
    test_schedule();test_calling_conventions();test_speed_sites();test_movement_residual();test_replay_parser();test_scale_rect();test_snap_client();test_menu_key();
    if (sim) { test_runner();test_runner_undescribed();test_runner_tail(); }
    else puts("SKIP: the shared runner (this game's simulation is not described)");
    if (subs) test_replay_roundtrip();
    else puts("SKIP: replay round-trip (this game has no sub-steppable systems described)");
    /* A failed patch transaction must leave all game code unchanged. */
    patch_begin();uint8_t changed[6]={0};
    uintptr_t addr=id->signatures[0].addr;
    assert(patch_bytes(addr,changed,5,NULL));
    /* addr+1 is deliberately not a frozen site, so this must be refused. The
       "INTERNAL ERROR: missing signature" line it prints is that refusal working. */
    assert(!patch_bytes(addr+1,changed,5,NULL));
    assert(!patch_commit());assert(!memcmp((void*)addr,id->signatures[0].bytes,5));
    /* The harness links these but never calls into them, so nothing has pulled them in. The
       game always has them loaded by the time the patch installs; load them here so that the
       import redirection runs against the real modules and can be checked afterwards. */
    LoadLibraryA("d3d9.dll");LoadLibraryA("d3d8.dll");LoadLibraryA("winmm.dll");
    cfg.subtick_input=1;cfg.d3d9ex=1;
    /* A profile may install extra sites only when the ini asks for debug -- counters inside the
       game's own guards, for working out why a sub-stepped system does nothing. Those are code
       written into the game like any other patch, so they are validated here rather than being
       a path only the person testing ever runs. */
    cfg.debug=1;
    if (g_game->provisional && !getenv("HFR_VALIDATE_PROVISIONAL")) {
        assert(!install());          /* a provisional game must be left completely alone */
        puts("SKIP: hook installation (this game is provisional; the patch does not touch it)");
        printf("PASS: %u executable signatures verified; nothing patched\n", (unsigned)id->signature_count);
        free(file); return 0;
    }
    if (g_game->provisional) { g_validate_provisional=1; puts("NOTE: validating a provisional profile's patch plan"); }
    assert(install() && !g_patch_failed);
    /* the frame hook exists exactly when the profile describes one -- through the TH10-15
       runner, or through an older engine's own presentation hooks */
    assert(g_frame_hook_installed == (sim || g_game->install_presentation != NULL));
    if (g_game->d3d8) assert(orig_Direct3DCreate8);   /* the bridge resolves Direct3DCreate9 itself, at run time */
    else assert(orig_Direct3DCreate9 && orig_D3DXCreateTexture && orig_D3DXCreateTextureFromFileInMemoryEx);
    assert(!sim || orig_joyGetPosEx);   /* sub-tick input only where there is a simulation */
    cfg.debug=0;
    puts("PASS: complete patch plan has frozen signatures, no overlaps; failed transaction leaves code intact");
    dump(argv[2],".game",base,nt->OptionalHeader.SizeOfImage);
    dump(argv[2],".stubs",g_stub_mem,g_stub_used);
    /* Every byte this patch writes into the game, so a tool can check it against another
       patch's own list of addresses (tools/check_patch_overlap.py). */
    {
        char* w=malloc(128+(g_patch_count+id->signature_count+id->conflict_count)*32);size_t n=0;
        n+=sprintf(w+n,"{\"game\":\"%s\",\"patches\":[",id->name);
        for (size_t i=0;i<g_patch_count;++i)
            n+=sprintf(w+n,"%s[%u,%u]",i?",":"",(unsigned)g_patches[i].addr,(unsigned)g_patches[i].size);
        n+=sprintf(w+n,"],\"signatures\":[");
        for (size_t i=0;i<id->signature_count;++i)
            n+=sprintf(w+n,"%s[%u,%u]",i?",":"",(unsigned)id->signatures[i].addr,(unsigned)id->signatures[i].size);
        n+=sprintf(w+n,"],\"conflicts\":[");
        for (size_t i=0;i<id->conflict_count;++i)
            n+=sprintf(w+n,"%s[%u,%u]",i?",":"",(unsigned)id->conflicts[i].addr,(unsigned)id->conflicts[i].size);
        n+=sprintf(w+n,"]}");
        dump(argv[2],".patches.json",w,n);free(w);
    }
    char info[1024];snprintf(info,sizeof info,
        "{\"stub_base\":%u,\"major\":%u,\"factor\":%u,\"logical\":%u,\"residual\":%u,\"ptf_prev\":%u,\"ptf_cur\":%u}",
        (unsigned)g_stub_mem,(unsigned)&g_major,(unsigned)&g_factor,(unsigned)&g_logical,
        (unsigned)g_move_residual,(unsigned)&g_ptf_prev,(unsigned)&g_ptf_cur);
    dump(argv[2],".json",info,strlen(info));
    printf("PASS: %u executable signatures; emitted %u bytes of real hook stubs\n",
        (unsigned)id->signature_count,(unsigned)g_stub_used);
    test_import_redirection();
    free(file);return 0;
}
