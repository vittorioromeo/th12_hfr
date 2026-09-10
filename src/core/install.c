static const struct GameProfile* const game_profiles[] = {&th11_profile,&th12_profile};
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
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static int install(void) {
    if (!g_game) return 0;
    if (conflict_found(0)) return 0;   /* another patch already owns the frame loop */
    g_p=stub_begin();
    if (!g_stub_mem) { LOG("Cannot allocate hook stubs; no hooks applied");return 0; }
    patch_begin();
    g_game->install_speed();
    g_game->install_sites();
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
    for (int i=0;i<4;++i) site_call(g_game->addr.replay_saves[i],hfr_replay_save);
    site_call(g_game->addr.replay_load_call,hfr_replay_load);
    uint8_t latency[7]; memcpy(latency,site_expected(g_game->addr.latency_cmp,7),7);latency[6]=0x7f;
    patch_bytes(g_game->addr.latency_cmp,latency,7,site_expected(g_game->addr.latency_cmp,7));
    patch_jmp(g_game->addr.runner_fn,hfr_runner_entry,site_expected(g_game->addr.runner_fn,5));
    for (int i=0;i<3;++i) site_call(g_game->addr.frame_calls[i],hfr_frame);
    if (!hook_iat("d3d9.dll","Direct3DCreate9",hook_Direct3DCreate9,(void**)&orig_Direct3DCreate9)) {
        LOG("Required Direct3D import is unavailable; no hooks applied");return 0;
    }
    if (cfg.d3d9ex) {
        int a=hook_iat(g_game->d3dx,"D3DXCreateTexture",hook_D3DXCreateTexture,(void**)&orig_D3DXCreateTexture);
        int b=hook_iat(g_game->d3dx,"D3DXCreateTextureFromFileInMemoryEx",hook_D3DXCreateTextureFromFileInMemoryEx,(void**)&orig_D3DXCreateTextureFromFileInMemoryEx);
        if (!a || !b) {cfg.d3d9ex=0;LOG("D3DX hooks unavailable (%d,%d): using D3D9",a,b);}
    }
    if (cfg.subtick_input) hook_iat("winmm.dll","joyGetPosEx",hook_joyGetPosEx,(void**)&orig_joyGetPosEx);
    FlushInstructionCache(GetCurrentProcess(),g_stub_mem,g_stub_used);
    if (!patch_commit()) { LOG("Patch transaction failed; no code/import hooks applied");return 0; }
    AddVectoredExceptionHandler(1, hfr_exception_report);
    QueryPerformanceFrequency(&g_qpf);timeBeginPeriod(1);
    recompute_rate(cfg.fps>0?cfg.fps:detect_refresh(NULL));
    LOG("Installed %s: %u verified signatures, %u code/import patches",g_game->identity->name,
        (unsigned)g_game->identity->signature_count,(unsigned)g_patch_count);
    return 1;
}
