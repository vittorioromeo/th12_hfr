static const struct GameProfile* const game_profiles[] = {&th10_profile,&th11_profile,&th12_profile,&th13_profile};
static int select_game(const uint8_t* image, size_t size) {
    const struct GameIdentity* id=identify_image(image,size);
    g_game=NULL;
    for (size_t i=0;i<sizeof game_profiles/sizeof *game_profiles;++i)
        if (game_profiles[i]->identity==id && game_profiles[i]->class_count<=MAX_NODE_CLASSES) { g_game=game_profiles[i];return 1; }
    return 0;
}
/* First-chance report of a fatal exception, naming the module it came from. A crash inside
   a windowed process is otherwise silent, and the patch's log is the only thing a tester
   can send back. Purely diagnostic: the exception is always passed on unchanged. */
static LONG CALLBACK hfr_exception_report(EXCEPTION_POINTERS* ep) {
    static int reported;
    DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    int fatal = code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
                code == EXCEPTION_PRIV_INSTRUCTION || code == EXCEPTION_INT_DIVIDE_BY_ZERO ||
                code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_IN_PAGE_ERROR;
    if (fatal && reported < 4) {
        ++reported;
        void* addr = ep->ExceptionRecord->ExceptionAddress;
        char name[MAX_PATH] = "?";
        HMODULE mod = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)addr, &mod);
        if (mod) GetModuleFileNameA(mod, name, MAX_PATH);
        LOG("EXCEPTION %08lx at %p  (%s + 0x%x)", (unsigned long)code, addr, name,
            (unsigned)((uintptr_t)addr - (uintptr_t)mod));
        /* Registers, and every stack slot that points into the game's code or into one of
           our stubs -- a poor man's backtrace, which is the difference between "it crashed
           at 0x42b1e0" and knowing who called it with what. */
        CONTEXT* c = ep->ContextRecord;
        if (c) {
            LOG("  eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx",
                c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
            char line[512]; int n = 0;
            const uintptr_t* sp = (const uintptr_t*)(c->Esp & ~3u);
            for (int i = 0; i < 512 && n < 400; ++i) {
                uintptr_t v;
                if (IsBadReadPtr(sp + i, 4)) break;
                v = sp[i];
                int in_game = v >= 0x401000 && v < 0x500000;   /* the game is always at 0x400000 */
                int in_stub = g_stub_mem && v >= (uintptr_t)g_stub_mem && v < (uintptr_t)g_stub_mem + g_stub_used;
                if (in_game || in_stub)
                    n += snprintf(line + n, sizeof line - n, " [%x]=%08lx%s", i * 4, (unsigned long)v, in_stub ? "s" : "");
            }
            LOG("  stack slots into code:%s", line);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
/* Every engine uses the same speed operations; only the overwritten instruction differs. */
static void install_speed_sites(void) {
    void* ops[] = {speed_set_one_perm, speed_set_one_temp, speed_pause_set_c,
                   speed_pause_restore_c, speed_set_ecl_c};
    void* stubs[5][2] = {{0}};
    g_p = stub_begin();
    for (size_t i = 0; i < g_game->speed_site_count; ++i) {
        const struct SpeedSite* s = &g_game->speed_sites[i];
        if (s->op >= 5 || s->pop_float > 1 || s->size < 5) { g_patch_failed = 1; break; }
        void** stub = &stubs[s->op][s->pop_float];
        if (!*stub) {
            *stub = g_p;
            if (s->pop_float) { E(0xd9,0x1d); E32((uint32_t)(uintptr_t)&g_fpu_tmp); }
            E(0x9c,0x60); ECALL((uintptr_t)ops[s->op]); E(0x61,0x9d,0xc3);
        }
        patch_call_n(s->addr,*stub,s->size,site_expected(s->addr,s->size));
    }
    stub_end();
}
/* The harness sets this to validate a provisional game's patch plan -- every signature, every
   expected byte, no overlaps -- which is exactly the evidence needed to take provisional off.
   The game itself never sets it, so a provisional profile stays inert in the game. */
static int g_validate_provisional;
static int install(void) {
    if (!g_game) return 0;
    if (g_game->provisional && !g_validate_provisional) {
        LOG("%s is recognised but support for it is not finished, so nothing has been patched.",
            g_game->identity->name);
        LOG("  The game runs exactly as it would without this patch. See src/games for what is known.");
        return 0;
    }
    if (conflict_found(0)) return 0;   /* another patch already owns the frame loop */
    g_p=stub_begin();
    if (!g_stub_mem) { LOG("Cannot allocate hook stubs; no hooks applied");return 0; }
    patch_begin();
    /* A profile need not describe the simulation at all. Everything about the picture -- the
       scaling modes, the filters, the resizable window, the menu -- is the same code for every
       game and needs no address from it, so a game whose engine has not been worked out yet
       can still have all of that while the high frame rate waits. Each part below installs
       only if the profile has the addresses for it, and says so when it does not. */
    int sim = g_game->addr.runner_fn && g_game->addr.frame_calls[0];
    if (sim) {
        install_speed_sites();
        if (g_game->install_sites) g_game->install_sites();
    } else {
        LOG("this game's simulation is not described yet: no high frame rate or sub-stepping.");
        LOG("  scaling, filters, window resizing and the menu do not depend on it and are active.");
    }
    /* Wrap the game's screenshot routine so the back buffer hook knows when the caller is
       going to lock what it gets. The filename arrives in EAX, so the stub must not touch it;
       "mov dword [flag], imm" and a relative call do not. */
    if (g_game->addr.screenshot_call && g_game->addr.screenshot_fn) {
        uint8_t* stub=g_p;
        E(0xC7,0x05);E32((uint32_t)(uintptr_t)&g_in_screenshot);E32(1);
        ECALL(g_game->addr.screenshot_fn);
        E(0xC7,0x05);E32((uint32_t)(uintptr_t)&g_in_screenshot);E32(0);
        E(0xC3);
        stub_end();          /* account for these bytes: they are flushed from the I-cache below */
        site_call(g_game->addr.screenshot_call,stub);
    } else LOG("screenshot routine not known for this game; its screenshots are unsupported");
    /* Higher internal resolution: the sprite builder's whole-pixel snapping goes (d3d9.c says why). */
    if (cfg.internal_scale > 1 && g_game->sprite_round_count) {
        static const uint8_t nop2[2] = {0x90,0x90};
        for (size_t i = 0; i < g_game->sprite_round_count; ++i)
            patch_bytes(g_game->sprite_round_sites[i], nop2, 2, site_expected(g_game->sprite_round_sites[i], 2));
        LOG("internal resolution: sprite corners no longer snapped to whole pixels (%u sites)", (unsigned)g_game->sprite_round_count);
    } else if (cfg.internal_scale > 1) LOG("internal resolution: this game's sprite snapping is not known; sprites stay on whole pixels");
    /* Dimming needs to know which draw callback each draw call belongs to (dimming.c). */
    if (g_game->draw.dispatch) dim_install(); else LOG("dimming: this game's draw runner is not described; dim_background/dim_items are inert");
    if (sim) {
        for (int i=0;i<4;++i) if (g_game->addr.replay_saves[i]) site_call(g_game->addr.replay_saves[i],hfr_replay_save);
        if (g_game->addr.replay_load_call) site_call(g_game->addr.replay_load_call,hfr_replay_load);
        if (g_game->addr.latency_cmp) {
            uint8_t latency[7]; memcpy(latency,site_expected(g_game->addr.latency_cmp,7),7);latency[6]=0x7f;
            patch_bytes(g_game->addr.latency_cmp,latency,7,site_expected(g_game->addr.latency_cmp,7));
        }
        patch_jmp(g_game->addr.runner_fn,g_game->runner_stack_arg ? (void*)hfr_runner_stack_entry : (void*)hfr_runner_entry,site_expected(g_game->addr.runner_fn,5));
        for (int i=0;i<3;++i) if (g_game->addr.frame_calls[i]) site_call(g_game->addr.frame_calls[i],hfr_frame);
        g_frame_hook_installed = 1;
    }
    if (!hook_iat("d3d9.dll","Direct3DCreate9",hook_Direct3DCreate9,(void**)&orig_Direct3DCreate9)) {
        LOG("Required Direct3D import is unavailable; no hooks applied");return 0;
    }
    if (cfg.d3d9ex) {
        int a=hook_iat(g_game->d3dx,"D3DXCreateTexture",hook_D3DXCreateTexture,(void**)&orig_D3DXCreateTexture);
        int b=hook_iat(g_game->d3dx,"D3DXCreateTextureFromFileInMemoryEx",hook_D3DXCreateTextureFromFileInMemoryEx,(void**)&orig_D3DXCreateTextureFromFileInMemoryEx);
        if (!a || !b) {cfg.d3d9ex=0;LOG("D3DX hooks unavailable (%d,%d): using D3D9",a,b);}
    }
    if (cfg.internal_scale > 1 &&
        !hook_iat(g_game->d3dx,"D3DXLoadSurfaceFromSurface",hook_D3DXLoadSurfaceFromSurface,(void**)&orig_D3DXLoadSurfaceFromSurface))
        LOG("internal resolution: D3DXLoadSurfaceFromSurface not imported; screen captures will show the top-left quarter");
    if (cfg.texture_scale > 1) {
        hook_iat(g_game->d3dx,"D3DXLoadSurfaceFromMemory",hook_D3DXLoadSurfaceFromMemory,(void**)&orig_D3DXLoadSurfaceFromMemory);
        hook_iat(g_game->d3dx,"D3DXLoadSurfaceFromFileInMemory",hook_D3DXLoadSurfaceFromFileInMemory,(void**)&orig_D3DXLoadSurfaceFromFileInMemory);
        if (!orig_D3DXLoadSurfaceFromSurface)
            hook_iat(g_game->d3dx,"D3DXLoadSurfaceFromSurface",hook_D3DXLoadSurfaceFromSurface,(void**)&orig_D3DXLoadSurfaceFromSurface);
    }
    /* Sub-tick input feeds the simulation, so it belongs with the rest of it. */
    hook_iat("winmm.dll","joyGetPosEx",hook_joyGetPosEx,(void**)&orig_joyGetPosEx);   /* always: the real call stalls the game thread (input.c) */
    FlushInstructionCache(GetCurrentProcess(),g_stub_mem,g_stub_used);
    if (!patch_commit()) { LOG("Patch transaction failed; no code/import hooks applied");return 0; }
    AddVectoredExceptionHandler(1, hfr_exception_report);
    QueryPerformanceFrequency(&g_qpf);timeBeginPeriod(1);
    recompute_rate(cfg.fps>0?cfg.fps:detect_refresh(NULL));
    LOG("Installed %s: %u verified signatures, %u code/import patches",g_game->identity->name,
        (unsigned)g_game->identity->signature_count,(unsigned)g_patch_count);
    return 1;
}
