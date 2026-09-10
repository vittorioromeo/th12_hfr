/* ------------------------------------------------------------------ init */
static void read_config(void) {
    char ini[MAX_PATH]; GetModuleFileNameA(NULL, ini, MAX_PATH);
    char* p = strrchr(ini, '\\'); if (p) strcpy(p + 1, "touhou_hfr.ini"); else strcpy(ini, "touhou_hfr.ini");
    if (GetFileAttributesA(ini)==INVALID_FILE_ATTRIBUTES && g_game) {
        if (p) strcpy(p+1,g_game->identity->legacy_ini); else strcpy(ini,g_game->identity->legacy_ini);
    }
    cfg.fps = GetPrivateProfileIntA("hfr", "fps", 0, ini);
    cfg.vsync = GetPrivateProfileIntA("hfr", "vsync", 1, ini);
    cfg.substep = GetPrivateProfileIntA("hfr", "substep", 1, ini);
    cfg.log = GetPrivateProfileIntA("hfr", "log", 1, ini);
    cfg.fullscreen_refresh = GetPrivateProfileIntA("hfr", "fullscreen_refresh", 0, ini);
    cfg.enemy_interp = GetPrivateProfileIntA("hfr", "enemy_interp", 1, ini);
    cfg.debug = GetPrivateProfileIntA("hfr", "debug", 0, ini);
    cfg.subtick_input = GetPrivateProfileIntA("hfr", "subtick_input", 1, ini);
    cfg.d3d9ex = GetPrivateProfileIntA("hfr", "d3d9ex", 1, ini);
    cfg.max_frame_latency = GetPrivateProfileIntA("hfr", "max_frame_latency", 1, ini);
    cfg.flipex = GetPrivateProfileIntA("hfr", "flipex", 0, ini);
    cfg.scaling = GetPrivateProfileIntA("video", "scaling", 1, ini);
    cfg.filter = GetPrivateProfileIntA("video", "filter", 2, ini);
    cfg.resizable = GetPrivateProfileIntA("video", "resizable", 1, ini);
    cfg.snap_aspect = GetPrivateProfileIntA("video", "snap_aspect", 1, ini);
    cfg.fullscreen_mode = GetPrivateProfileIntA("video", "fullscreen_mode", 1, ini);
    for (size_t i = 0; g_game && i < g_class_count; i++) {
        char key[64]; snprintf(key, sizeof key, "sub_%s", g_classes[i].name);
        g_sub_enabled[i] = GetPrivateProfileIntA("systems", key, g_classes[i].mode == MODE_SUB, ini);
    }
}



/* ------------------------------------------------------------------ dinput8.dll proxy mode */
typedef HRESULT (WINAPI *DirectInput8CreateFn)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static DirectInput8CreateFn real_DirectInput8Create;
__declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD ver, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
    if (!real_DirectInput8Create) {
        char path[MAX_PATH]; GetSystemDirectoryA(path, MAX_PATH); strcat(path, "\\dinput8.dll");
        HMODULE m = LoadLibraryA(path);
        if (m) real_DirectInput8Create = (DirectInput8CreateFn)GetProcAddress(m, "DirectInput8Create");
    }
    if (!real_DirectInput8Create) return E_FAIL;
    return real_DirectInput8Create(hinst, ver, riid, out, outer);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID res) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        /* only one instance may patch (the DLL can be loaded as touhou_hfr.dll and as dinput8.dll) */
        char mutex_name[80]; snprintf(mutex_name, sizeof mutex_name, "touhou_hfr_%lu", GetCurrentProcessId());
        HANDLE mtx = CreateMutexA(NULL, FALSE, mutex_name);
        if (!mtx) return FALSE;
        if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mtx);return TRUE; }
        uint8_t* base=(uint8_t*)GetModuleHandleA(NULL);
        MEMORY_BASIC_INFORMATION mbi;
        /* The loader has validated these headers; detection still checks all bounds. */
        const IMAGE_NT_HEADERS32* nt=NULL;
        if ((uintptr_t)base==0x400000 && VirtualQuery(base,&mbi,sizeof mbi)) nt=image_header(base,mbi.RegionSize);
        if (nt) select_game(base,nt->OptionalHeader.SizeOfImage);
        read_config();
        if (cfg.log) { char path[MAX_PATH]; GetModuleFileNameA(NULL, path, MAX_PATH); char* p = strrchr(path, '\\'); if (p) strcpy(p + 1, "touhou_hfr.log"); g_log = fopen(path, "w"); }
        LOG("Touhou HFR v0.3.0-test loading; fps=%d vsync=%d substep=%d subtick_input=%d d3d9ex=%d max_frame_latency=%d flipex=%d enemy_interp=%d",
            cfg.fps, cfg.vsync, cfg.substep, cfg.subtick_input, cfg.d3d9ex, cfg.max_frame_latency, cfg.flipex, cfg.enemy_interp);
        LOG("video: scaling=%d filter=%d resizable=%d snap_aspect=%d fullscreen_mode=%d",
            cfg.scaling, cfg.filter, cfg.resizable, cfg.snap_aspect, cfg.fullscreen_mode);
        if (!g_game || !install()) LOG("Unsupported/modified executable or installation failure; HFR inactive (proxy forwarding available)");
    }
    return TRUE;
}
