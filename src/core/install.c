static const struct GameProfile* const game_profiles[] = {&th08_profile,&th10_profile,&th11_profile,&th12_profile,&th13_profile,&th14_profile,&th15_profile,&th18_profile,&th20_profile};
/* A profile is written for the preferred base. When the image is somewhere else, the runtime
   works from a copy with every address moved: the identity's signatures (addresses, and the
   dwords inside their bytes that the loader fixed up) and conflict sites, the profile's whole
   `addr` block -- which is nothing but addresses, and is walked as an array for that reason --
   and the address-bearing tables it points at. Offsets, masks and flags are untouched. An
   adapter's own literals are its own business: it wraps them in HFR_VA(). */
static const struct GameProfile* relocate_profile(const struct GameProfile* src, const uint8_t* image, size_t size) {
    static struct GameProfile prof; static struct GameIdentity ident;
    const uint32_t delta=(uint32_t)(g_image_base-HFR_PREFERRED_BASE);
    if (!delta) return src;
    const struct GameIdentity* id=src->identity;
    struct GameSignature* sig=(struct GameSignature*)calloc(id->signature_count?id->signature_count:1,sizeof *sig);
    struct ConflictSite* con=(struct ConflictSite*)calloc(id->conflict_count?id->conflict_count:1,sizeof *con);
    struct SpeedSite* sp=(struct SpeedSite*)calloc(src->speed_site_count?src->speed_site_count:1,sizeof *sp);
    struct node_class* cl=(struct node_class*)calloc(src->class_count?src->class_count:1,sizeof *cl);
    uintptr_t* rs=(uintptr_t*)calloc(src->sprite_round_count?src->sprite_round_count:1,sizeof *rs);
    if (!sig||!con||!sp||!cl||!rs) return NULL;
    for (size_t i=0;i<id->signature_count;++i) {
        sig[i]=id->signatures[i];
        if (reloc_adjust(image,size,sig[i].addr,sig[i].size,sig[i].bytes,delta)<0) return NULL;
        sig[i].addr+=delta;
    }
    for (size_t i=0;i<id->conflict_count;++i) {
        con[i]=id->conflicts[i];
        if (reloc_adjust(image,size,con[i].addr,con[i].size,con[i].bytes,delta)<0) return NULL;
        con[i].addr+=delta;
    }
    ident=*id; ident.signatures=sig; ident.conflicts=id->conflicts?con:NULL;
    prof=*src; prof.identity=&ident;
    uintptr_t* a=(uintptr_t*)&prof.addr;
    for (size_t i=0;i<sizeof prof.addr/sizeof *a;++i) if (a[i]) a[i]+=delta;
    for (size_t i=0;i<src->speed_site_count;++i) { sp[i]=src->speed_sites[i]; sp[i].addr+=delta; }
    for (size_t i=0;i<src->class_count;++i) { cl[i]=src->classes[i]; cl[i].func+=delta; }
    for (size_t i=0;i<src->sprite_round_count;++i) rs[i]=src->sprite_round_sites[i]+delta;
    prof.speed_sites=sp; prof.classes=cl; prof.sprite_round_sites=src->sprite_round_sites?rs:NULL;
    if (prof.draw.dispatch) prof.draw.dispatch+=delta;
    if (prof.draw.flush_fn) prof.draw.flush_fn+=delta;
    if (prof.draw.flush_this) prof.draw.flush_this+=delta;
    if (prof.draw.vm_draw) prof.draw.vm_draw+=delta;
    return &prof;
}
static int select_game(const uint8_t* image, size_t size) {
    const struct GameIdentity* id=identify_image(image,size);
    g_game=NULL;
    for (size_t i=0;i<sizeof game_profiles/sizeof *game_profiles;++i)
        if (game_profiles[i]->identity==id && game_profiles[i]->class_count<=MAX_NODE_CLASSES) {
            g_game=relocate_profile(game_profiles[i],image,size);
            return g_game!=NULL;
        }
    return 0;
}
/* First-chance report of a fatal exception, naming the module it came from. A crash inside
   a windowed process is otherwise silent, and the patch's log is the only thing a tester
   can send back. Purely diagnostic: the exception is always passed on unchanged. */
static LONG CALLBACK hfr_exception_report(EXCEPTION_POINTERS* ep) {
    /* A budget, because a process that is going down can raise the same fault repeatedly and a
       log full of one address helps nobody. But the budget must not be spendable on *one* place:
       a game that throws a first-chance access violation somewhere harmless -- TH14 does, four
       times inside KERNEL32 before the title screen -- would use it up and then be silent about
       the crash that matters. So each distinct address reports once, and there is room for
       several. This cost a diagnosis: the replay crash below produced no report at all. */
    enum { REPORT_SLOTS = 12 };
    static void* seen[REPORT_SLOTS];
    static int seen_n;
    DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    int fatal = code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
                code == EXCEPTION_PRIV_INSTRUCTION || code == EXCEPTION_INT_DIVIDE_BY_ZERO ||
                code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_IN_PAGE_ERROR;
    if (fatal) {
        void* at = ep->ExceptionRecord->ExceptionAddress;
        for (int i = 0; i < seen_n; ++i) if (seen[i] == at) return EXCEPTION_CONTINUE_SEARCH;
        if (seen_n >= REPORT_SLOTS) return EXCEPTION_CONTINUE_SEARCH;
        seen[seen_n++] = at;
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
                if (!mem_readable(sp + i, 4)) break;
                v = sp[i];
                int in_game = v >= g_image_base + 0x1000 && v < g_image_base + (g_game ? g_game->identity->image_size : 0x100000);
                int in_stub = g_stub_mem && v >= (uintptr_t)g_stub_mem && v < (uintptr_t)g_stub_mem + g_stub_used;
                if (in_game || in_stub)
                    n += snprintf(line + n, sizeof line - n, " [%x]=%08lx%s", i * 4, (unsigned long)v, in_stub ? "s" : "");
            }
            LOG("  stack slots into code:%s", line);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
/* Every engine uses the same speed operations; only the overwritten instruction differs, so
   the stub is (capture the value the instruction was going to store) + (call the operation),
   and one stub is shared by every site with the same operation and the same source.

   The stub stands in for a single instruction, not for a call, so nothing about it may be
   caller-saved: the game continues into the next instruction expecting every register it had.
   `pushad`/`popfd` cover the general registers and the flags. The eight XMM registers are
   saved too, because on TH14 the patched instruction sits in the middle of SSE code and the
   operation is C -- whatever the compiler decides to do with a float multiply is not something
   this file gets to assume. The x87 stack needs no saving: the only thing done to it is the
   `fstp` that pops the value the instruction was about to store. */
static void install_speed_sites(void) {
    void* ops[] = {speed_set_one_perm, speed_set_one_temp, speed_pause_set_c,
                   speed_pause_restore_c, speed_set_ecl_c};
    void* stubs[5][SPEED_SRC_COUNT] = {{0}};
    g_p = stub_begin();
    for (size_t i = 0; i < g_game->speed_site_count; ++i) {
        const struct SpeedSite* s = &g_game->speed_sites[i];
        if (s->op >= 5 || s->src >= SPEED_SRC_COUNT || s->size < 5) { g_patch_failed = 1; break; }
        void** stub = &stubs[s->op][s->src];
        if (!*stub) {
            *stub = g_p;
            if (s->src == SPEED_SRC_FPU) {                       /* fstp dword [g_fpu_tmp] */
                E(0xd9,0x1d); E32((uint32_t)(uintptr_t)&g_fpu_tmp);
            } else if (s->src != SPEED_SRC_NONE) {               /* movss [g_fpu_tmp],xmmN */
                unsigned n = s->src - SPEED_SRC_XMM0;
                E(0xf3,0x0f,0x11,(uint8_t)(0x05 | (n << 3))); E32((uint32_t)(uintptr_t)&g_fpu_tmp);
            }
            E(0x81,0xec,0x80,0x00,0x00,0x00);                    /* sub esp,0x80 */
            for (unsigned n = 0; n < 8; ++n)                     /* movups [esp+n*16],xmmN */
                E(0x0f,0x11,(uint8_t)(0x44 | (n << 3)),0x24,(uint8_t)(n * 16));
            E(0x9c,0x60); ECALL((uintptr_t)ops[s->op]); E(0x61,0x9d);
            for (unsigned n = 0; n < 8; ++n)                     /* movups xmmN,[esp+n*16] */
                E(0x0f,0x10,(uint8_t)(0x44 | (n << 3)),0x24,(uint8_t)(n * 16));
            E(0x81,0xc4,0x80,0x00,0x00,0x00);                    /* add esp,0x80 */
            E(0xc3);
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
    /* What each part of the run path dereferences without asking. Two crashes in TH14's first
       two builds were an address this list would have named -- found by starting the game,
       reading a fault address and disassembling our own DLL, which is an expensive way to
       learn that a struct field is zero. So the profile is audited here, once, by name, and
       what cannot be supported is switched off rather than left to fault later.
       Everything in `sim` above is required; the rest each disable one thing. */
    if (sim) {
        static const struct { size_t off; const char* name; const char* needed_for; } wants[] = {
            {offsetof(struct GameProfile,addr.update_runner), "update_runner", "the update pass"},
            {offsetof(struct GameProfile,addr.frame_fn),      "frame_fn",      "the frame hook"},
            {offsetof(struct GameProfile,addr.remove_node),   "remove_node",   "removing a finished node"},
            {offsetof(struct GameProfile,addr.crit),          "crit",          "the runner's lock"},
            {offsetof(struct GameProfile,addr.speed),         "speed",         "scaling the simulation"},
            {offsetof(struct GameProfile,addr.frame_context_ptr), "frame_context_ptr", "the catch-up tick"},
            {offsetof(struct GameProfile,addr.cleanup_fn),    "cleanup_fn",    "the catch-up tick"},
            {offsetof(struct GameProfile,addr.poll_input),    "poll_input",    "sub-tick input"},
            {offsetof(struct GameProfile,addr.game_input),    "game_input",    "the input word in the debug log and sub-tick input"},
            {offsetof(struct GameProfile,addr.pp),            "pp",            "resetting the game's own swap chain"},
            {offsetof(struct GameProfile,addr.player),        "player",        "the player's state timer"},
        };
        for (size_t i=0;i<sizeof wants/sizeof *wants;++i)
            if (!*(const uintptr_t*)((const uint8_t*)g_game + wants[i].off) &&
                !(g_game->update_only && !strcmp(wants[i].needed_for, "the catch-up tick")) &&   /* the profile makes that tick itself */
                !(g_game->poll_raw && !strcmp(wants[i].name, "poll_input")) &&                     /* ... and that poll */
                !(g_game->runner_wrap && (!strcmp(wants[i].name, "remove_node") || !strcmp(wants[i].name, "crit"))))   /* the game's own runner does both */
                LOG("profile: %s is not described; %s is unavailable", wants[i].name, wants[i].needed_for);
        /* Some of these are not independent. The game speed is one: the runtime writes it every
           tick, and the game writes it too -- for its own slow-motion, for a pause, for the ECL
           speed instruction -- so the two only compose because every one of the game's writes is
           a described speed site that folds our factor in. A profile with `speed` and no sites
           would have the runtime overwrite the game's own speed once a tick and hold it at 1.0,
           which is not a crash and not a message, just a game that never slows down. */
        if (g_game->addr.speed && !g_game->speed_site_count && !g_game->speed_sites_own)
            LOG("profile: speed is described but none of its write sites are; the game's own speed changes would be overwritten. Describe the sites or leave speed out.");
    }
    if (sim) {
        install_speed_sites();
        if (g_game->install_sites) g_game->install_sites();
        /* A game can have its scheduler described and its systems not. Nothing is
           mis-stepped in that state -- node_mode answers MODE_FRAME for a callback it does
           not know -- but the frame rate is the only thing on offer, and saying so here is
           cheaper than someone wondering why the sub-step switches do nothing. */
        /* ... and the settings that depend on them are switched off rather than left on and
           inert. An inert setting is not harmless: with substep on, the logic rate leaves 60
           and the runner starts taking minor ticks that no system can use, and the log claims
           a sub-stepping that is not happening. Turn them off here, where the profile is
           known, so the rate, the log and the menu all say the same thing. */
        if (!g_class_count) {
            cfg.substep = 0;
            /* Say what this looks like, not what it is. Presenting at the display's rate with
               nothing moving between 60 Hz ticks means the same picture six times in a row at
               360 Hz, which on screen is indistinguishable from the stock game -- and "the
               frame rate is raised" reads like a promise that it will not look that way. */
            LOG("this game's systems are not classified yet: nothing moves between 60 Hz ticks, so motion still looks exactly like the unmodified game.");
            LOG("  the window, scaling, filters, dimming and the menu are all active; the frame rate is not yet worth anything on its own.");
        } else {
            /* Which systems actually move between 60 Hz frames, by name. A game part way
               through classification -- TH14 has its two sprite managers and nothing else --
               looks smooth in the menus and the HUD and stays at 60 Hz in play, and the
               difference between that and "high frame rate" is a question this line answers
               before it is asked. */
            char names[256]; int n = 0, subs = 0;
            for (size_t i = 0; i < g_class_count; ++i) if (g_classes[i].mode == MODE_SUB) {
                ++subs;
                if (n < (int)sizeof names - 1)
                    n += snprintf(names + n, sizeof names - (size_t)n, "%s%s", n ? ", " : "", g_classes[i].name);
            }
            LOG("sub-stepping: %d of %u identified systems step with the display (%s).",
                subs, (unsigned)g_class_count, subs ? names : "none");
            LOG("  everything else steps once per 60 Hz frame, so what it draws moves in 60 Hz steps however high the frame rate is.");
        }
        if ((!g_game->addr.poll_input && !g_game->poll_raw) || !g_game->addr.game_input) {
            if (cfg.subtick_input) LOG("this game's input path is not described: sub-tick input is off.");
            cfg.subtick_input = 0;
        }
    } else if (!g_game->install_presentation) {
        LOG("this game's simulation is not described yet: no high frame rate or sub-stepping.");
        LOG("  scaling, filters, window resizing and the menu do not depend on it and are active.");
    }
    /* Wrap the game's screenshot routine so the back buffer hook knows when the caller is
       going to lock what it gets. Through TH13 the filename arrives in EAX, so the stub must
       not touch it; "mov dword [flag], imm" and a relative call do not. */
    if (g_game->addr.screenshot_call && g_game->addr.screenshot_fn) {
        uint8_t* stub=g_p;
        E(0xC7,0x05);E32((uint32_t)(uintptr_t)&g_in_screenshot);E32(1);
        /* TH14 on: the filename is pushed and the routine pops it (stdcall), so the stub has
           to hand the argument on and clean it up itself. Otherwise the routine would be
           given the stub's own return address. */
        if (g_game->screenshot_stack_arg) E(0xFF,0x74,0x24,0x04);   /* push dword [esp+4] */
        ECALL(g_game->addr.screenshot_fn);
        E(0xC7,0x05);E32((uint32_t)(uintptr_t)&g_in_screenshot);E32(0);
        if (g_game->screenshot_stack_arg) E(0xC2,0x04,0x00); else E(0xC3);
        stub_end();          /* account for these bytes: they are flushed from the I-cache below */
        site_call(g_game->addr.screenshot_call,stub);
    } else if (!g_game->install_presentation) LOG("screenshot routine not known for this game; its screenshots are unsupported");
    /* Higher internal resolution: the sprite builder's whole-pixel snapping goes (d3d9.c says why). */
    if (cfg.internal_scale > 1 && g_game->sprite_round_count) {
        static const uint8_t nop2[2] = {0x90,0x90};
        for (size_t i = 0; i < g_game->sprite_round_count; ++i)
            patch_bytes(g_game->sprite_round_sites[i], nop2, 2, site_expected(g_game->sprite_round_sites[i], 2));
        LOG("internal resolution: sprite corners no longer snapped to whole pixels (%u sites)", (unsigned)g_game->sprite_round_count);
    } else if (cfg.internal_scale > 1) LOG("internal resolution: this game's sprite snapping is not known; sprites stay on whole pixels");
    /* Dimming needs to know which draw callback each draw call belongs to (dimming.c). */
    if (g_game->draw.dispatch) dim_install(); else if (!g_game->install_presentation) LOG("dimming: this game's draw runner is not described; dim_background/dim_items are inert");
    if (sim) {
        for (int i=0;i<4;++i) if (g_game->addr.replay_saves[i]) site_call(g_game->addr.replay_saves[i],hfr_replay_save);
        /* Both of the loader's play sites, not just the first. The game dispatches a replay
           start on a mode of 1 or 2 -- the two differ only in that mode 1 also stores the manager
           in a global -- and hooking only mode 1 left a replay started the other way playing back
           without its recorded rate. The loader's remaining call sites build a throwaway manager
           to read a file's header for the menu list and must not be hooked at all. */
        for (int i=0;i<2;++i) if (g_game->addr.replay_load_calls[i]) site_call(g_game->addr.replay_load_calls[i],hfr_replay_load);
        if (g_game->addr.latency_cmp) {
            uint8_t latency[7]; memcpy(latency,site_expected(g_game->addr.latency_cmp,7),7);latency[6]=0x7f;
            patch_bytes(g_game->addr.latency_cmp,latency,7,site_expected(g_game->addr.latency_cmp,7));
        }
        if (!g_game->runner_wrap) {
            runner_tail_select();   /* where the replacement runner ends (update_runner.c) */
            patch_jmp(g_game->addr.runner_fn,runner_entry_thunk(),site_expected(g_game->addr.runner_fn,5));
        }
        void* frame_hook=g_game->frame_ctx_ecx ? (void*)hfr_frame_ecx : (void*)hfr_frame;
        for (int i=0;i<3;++i) if (g_game->addr.frame_calls[i]) site_call(g_game->addr.frame_calls[i],frame_hook);
        g_frame_hook_installed = 1;
    }
    for (size_t i = 0; i < g_game->toggle_count; ++i) {
        const struct GameToggle* g = &g_game->toggles[i];
        *g->value = GetPrivateProfileIntA("game", g->key, g->def, g_ini_path) != 0;
        if (*g->value != g->def) LOG("game option %s=%d", g->key, *g->value);
    }
    if (g_game->install_presentation) g_game->install_presentation();
    /* Through the D3D8 bridge 9Ex is opt-in ([fixed60] d3d9ex=1) until it has been played on
       real hardware: the bridge asks for managed resources, which 9Ex does not have, and relies
       on the device hooks converting them. */
    if (g_game->d3d8) cfg.d3d9ex = GetPrivateProfileIntA("fixed60", "d3d9ex", 0, g_ini_path) != 0;
    if (!(g_game->d3d8
          ? hook_import("d3d8.dll","Direct3DCreate8",hook_Direct3DCreate8,(void**)&orig_Direct3DCreate8)
          : hook_import("d3d9.dll","Direct3DCreate9",hook_Direct3DCreate9,(void**)&orig_Direct3DCreate9))) {
        LOG("Required Direct3D import is unavailable; no hooks applied");return 0;
    }
    if (cfg.d3d9ex && !g_game->d3d8) {   /* a D3D8 game has no D3DX9 imports: its statically linked D3DX8 reaches the device hooks */
        int a=hook_import(g_game->d3dx,"D3DXCreateTexture",hook_D3DXCreateTexture,(void**)&orig_D3DXCreateTexture);
        /* A game that never imports the second one (TH20 creates every texture with the first
           and fills it with D3DXLoadSurfaceFromFileInMemory) has nothing there to convert. */
        int b=!iat_slot(g_game->d3dx,"D3DXCreateTextureFromFileInMemoryEx") ||
              hook_import(g_game->d3dx,"D3DXCreateTextureFromFileInMemoryEx",hook_D3DXCreateTextureFromFileInMemoryEx,(void**)&orig_D3DXCreateTextureFromFileInMemoryEx);
        if (!a || !b) {cfg.d3d9ex=0;LOG("D3DX hooks unavailable (%d,%d): using D3D9",a,b);}
    }
    if (cfg.internal_scale > 1 &&
        !hook_import(g_game->d3dx,"D3DXLoadSurfaceFromSurface",hook_D3DXLoadSurfaceFromSurface,(void**)&orig_D3DXLoadSurfaceFromSurface))
        LOG("internal resolution: D3DXLoadSurfaceFromSurface not imported; screen captures will show the top-left quarter");
    if (cfg.texture_scale > 1) {
        hook_import(g_game->d3dx,"D3DXLoadSurfaceFromMemory",hook_D3DXLoadSurfaceFromMemory,(void**)&orig_D3DXLoadSurfaceFromMemory);
        hook_import(g_game->d3dx,"D3DXLoadSurfaceFromFileInMemory",hook_D3DXLoadSurfaceFromFileInMemory,(void**)&orig_D3DXLoadSurfaceFromFileInMemory);
        if (!orig_D3DXLoadSurfaceFromSurface)
            hook_import(g_game->d3dx,"D3DXLoadSurfaceFromSurface",hook_D3DXLoadSurfaceFromSurface,(void**)&orig_D3DXLoadSurfaceFromSurface);
    }
    /* Sub-tick input feeds the simulation, so it belongs with the rest of it. */
    hook_import("winmm.dll","joyGetPosEx",hook_joyGetPosEx,(void**)&orig_joyGetPosEx);   /* always: the real call stalls the game thread (input.c) */
    /* What is typed into the menu stays out of the game's keyboard (typing_block.c). Through
       the proxy, DirectInput8Create is this DLL's own export and wraps the interface already;
       the import hook covers the launcher, and chains to the export otherwise. */
    hook_import("user32.dll","GetKeyboardState",hook_GetKeyboardState,(void**)&orig_GetKeyboardState);
    hook_import("dinput8.dll","DirectInput8Create",hook_import_DirectInput8Create,(void**)&orig_import_DirectInput8Create);
    /* The pointer in borderless fullscreen (window.c): both calls or neither, since one without the other leaves it half hidden. */
    if (hook_import("user32.dll","ShowCursor",hook_ShowCursor,(void**)&orig_ShowCursor) && hook_import("user32.dll","SetCursor",hook_SetCursor,(void**)&orig_SetCursor)) g_cursor_hooked = 1;
    else LOG("window: the game's cursor calls are not in its import table; the pointer stays as the game leaves it");
    FlushInstructionCache(GetCurrentProcess(),g_stub_mem,g_stub_used);
    if (!patch_commit()) { LOG("Patch transaction failed; no code/import hooks applied");return 0; }
    AddVectoredExceptionHandler(1, hfr_exception_report);
    QueryPerformanceFrequency(&g_qpf);timeBeginPeriod(1);
    recompute_rate(cfg.fps>0?cfg.fps:detect_refresh(NULL));
    LOG("Installed %s: %u verified signatures, %u code/import patches",g_game->identity->name,
        (unsigned)g_game->identity->signature_count,(unsigned)g_patch_count);
    return 1;
}
