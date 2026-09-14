/* ------------------------------------------------------------------ init */
/* Windows only strips ";" comments at the start of a line, so a value written as
   "filter=mmpx   ; a comment" arrives with the comment attached. Integer keys survive it;
   string keys have to be trimmed. */
static void ini_trim(char* s) {
    char* cut = s;
    for (char* p = s; *p; ++p) if (*p == ';' || *p == '#') { cut = p; break; } else cut = p + 1;
    while (cut > s && (cut[-1] == ' ' || cut[-1] == '\t')) --cut;
    *cut = 0;
    char* start = s;
    while (*start == ' ' || *start == '\t') ++start;
    if (start != s) memmove(s, start, strlen(start) + 1);
}
static void read_config(void) {
    char ini[MAX_PATH]; GetModuleFileNameA(NULL, ini, MAX_PATH);
    char* p = strrchr(ini, '\\'); if (p) strcpy(p + 1, "touhou_hfr.ini"); else strcpy(ini, "touhou_hfr.ini");
    if (GetFileAttributesA(ini)==INVALID_FILE_ATTRIBUTES && g_game && g_game->identity->legacy_ini) {
        if (p) strcpy(p+1,g_game->identity->legacy_ini); else strcpy(ini,g_game->identity->legacy_ini);
    }
    snprintf(g_ini_path, sizeof g_ini_path, "%s", ini);
    cfg.fps = GetPrivateProfileIntA("hfr", "fps", 0, ini);
    cfg.vsync = GetPrivateProfileIntA("hfr", "vsync", 1, ini);
    cfg.substep = GetPrivateProfileIntA("hfr", "substep", 1, ini);
    cfg.log = GetPrivateProfileIntA("hfr", "log", 1, ini);
    cfg.fullscreen_refresh = GetPrivateProfileIntA("hfr", "fullscreen_refresh", 0, ini);
    cfg.enemy_interp = GetPrivateProfileIntA("hfr", "enemy_interp", 1, ini);
    cfg.debug = GetPrivateProfileIntA("hfr", "debug", 0, ini); g_log_lazy = cfg.debug >= 3;
    cfg.subtick_input = GetPrivateProfileIntA("hfr", "subtick_input", 1, ini);
    cfg.d3d9ex = GetPrivateProfileIntA("hfr", "d3d9ex", 1, ini);
    cfg.max_frame_latency = GetPrivateProfileIntA("hfr", "max_frame_latency", 1, ini);
    cfg.flipex = GetPrivateProfileIntA("hfr", "flipex", 0, ini);
    cfg.scaling = GetPrivateProfileIntA("video", "scaling", 1, ini);
    GetPrivateProfileStringA("video", "filter", "sharp-bilinear", cfg.filter_name, sizeof cfg.filter_name, ini);
    ini_trim(cfg.filter_name);
    GetPrivateProfileStringA("video", "sharpen", "none", cfg.sharpen_name, sizeof cfg.sharpen_name, ini);
    ini_trim(cfg.sharpen_name);
    cfg.sharpen_strength = GetPrivateProfileIntA("video", "sharpen_strength", 50, ini);
    if (cfg.sharpen_strength < 0) cfg.sharpen_strength = 0;
    if (cfg.sharpen_strength > 100) cfg.sharpen_strength = 100;
    cfg.cursor = GetPrivateProfileIntA("video", "cursor", 2, ini);
    if (cfg.cursor < 0 || cfg.cursor > 2) cfg.cursor = 2;
    cfg.resizable = GetPrivateProfileIntA("video", "resizable", 1, ini);
    cfg.window_scale = GetPrivateProfileIntA("video", "window_scale", 0, ini);
    cfg.snap_aspect = GetPrivateProfileIntA("video", "snap_aspect", 0, ini);
    cfg.internal_scale = GetPrivateProfileIntA("video", "internal_scale", 1, ini);
    cfg.texture_scale = GetPrivateProfileIntA("video", "texture_scale", 0, ini);
    for (int i = 0; i < DIM_COUNT; ++i) { char key[32]; snprintf(key, sizeof key, "dim_%s", DIM_NAMES[i]); cfg.dim[i] = GetPrivateProfileIntA("video", key, 0, ini); }
    GetPrivateProfileStringA("video", "texture_filter", "xbr-lv2", cfg.texture_filter_name, sizeof cfg.texture_filter_name, ini);
    ini_trim(cfg.texture_filter_name);
    cfg.fullscreen_mode = GetPrivateProfileIntA("video", "fullscreen_mode", 1, ini);
    cfg.menu_key = GetPrivateProfileIntA("video", "menu_key", VK_F11, ini);
    cfg.size_cycle_key = GetPrivateProfileIntA("video", "size_cycle_key", VK_F10, ini);
    cfg.own_present = GetPrivateProfileIntA("video", "own_present", -1, ini);
    cfg.warn_wrapper = GetPrivateProfileIntA("video", "warn_wrapper", 1, ini);
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

/* ---------------------------------------------------------------- deferred installation
   A Steam release of these games is the same executable inside a DRM wrapper: the game's
   .text is encrypted and a stub in an appended section decrypts it, in memory, when the
   process starts. The loader runs this DLL's DllMain *before* the executable's entry point,
   so at the moment the patch normally identifies the game there is nothing to identify --
   every frozen signature still reads as ciphertext, and the patch correctly concludes it does
   not know this executable.

   The answer is to look again later, from something only the game itself calls. The import
   table is not encrypted (the loader has to read it to start the process at all), so the
   entry for d3d9.dll!Direct3DCreate9 can be redirected here while the stub is still the only
   thing that has run. By the time the game asks for Direct3D the code is plain, and that call
   still comes before the device, the window and the frame loop, so nothing the patch installs
   arrives late.

   Unwrapped installations never reach any of this: they identify in DllMain as they always
   have, and this is armed only when identification failed on an image that looks wrapped. */
static void** iat_slot(const char* dll, const char* func) {
    uint8_t* base=(uint8_t*)0x400000;
    const IMAGE_DOS_HEADER* dos=(const void*)base;
    const IMAGE_NT_HEADERS32* nt=(const void*)(base+dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return NULL;
    IMAGE_IMPORT_DESCRIPTOR* imp=(void*)(base+dir.VirtualAddress);
    for (;imp->Name;imp++) {
        if (_stricmp((const char*)(base+imp->Name),dll)) continue;
        if (!imp->OriginalFirstThunk) return NULL;
        IMAGE_THUNK_DATA* thunk=(void*)(base+imp->FirstThunk);
        IMAGE_THUNK_DATA* oth=(void*)(base+imp->OriginalFirstThunk);
        for (;oth->u1.AddressOfData;thunk++,oth++) {
            if (oth->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            const IMAGE_IMPORT_BY_NAME* ibn=(const void*)(base+oth->u1.AddressOfData);
            if (!strcmp((const char*)ibn->Name,func)) return (void**)&thunk->u1.Function;
        }
    }
    return NULL;
}
static int iat_write(void** slot, void* value) {
    DWORD old;
    if (!VirtualProtect(slot,sizeof *slot,PAGE_READWRITE,&old)) return 0;
    *slot=value;
    VirtualProtect(slot,sizeof *slot,old,&old);
    return 1;
}
static Direct3DCreate9Fn g_deferred_original;
static IDirect3D9* __stdcall deferred_install(UINT sdk) {
    void** slot=iat_slot("d3d9.dll","Direct3DCreate9");
    /* Put the real function back before installing, so the patch's own hook on this same
       slot records that and not this function -- which would otherwise call itself. */
    if (slot) iat_write(slot,(void*)g_deferred_original);
    uint8_t* base=(uint8_t*)GetModuleHandleA(NULL);
    MEMORY_BASIC_INFORMATION mbi;
    const IMAGE_NT_HEADERS32* nt=NULL;
    if ((uintptr_t)base==0x400000 && VirtualQuery(base,&mbi,sizeof mbi)) nt=image_header(base,mbi.RegionSize);
    if (nt && select_game(base,nt->OptionalHeader.SizeOfImage) && install())
        LOG("Identified once the executable had started: %s", g_game->identity->name);
    else
        LOG("Still not a supported executable after start-up; HFR inactive (proxy forwarding available)");
    /* Whatever owns the slot now is what the game meant to call: the patch's hook if it
       installed one, the real function if it did not. */
    Direct3DCreate9Fn go=slot?(Direct3DCreate9Fn)*slot:NULL;
    if (!go || go==deferred_install) go=g_deferred_original;
    return go?go(sdk):NULL;
}
/* 1 armed, 0 this is simply not an executable we know, -1 wrapped but there is no import to
   arm from. The last is worth telling apart: it is the one way a wrapper could defeat this,
   and a log line naming it is the difference between diagnosing that in a minute and guessing.
   Every supported game imports d3d9.dll!Direct3DCreate9, and the Steam wrapper leaves the
   import table alone, so -1 has never been observed. */
static int arm_deferred_install(void) {
    uint8_t* base=(uint8_t*)GetModuleHandleA(NULL);
    MEMORY_BASIC_INFORMATION mbi;
    if ((uintptr_t)base!=0x400000 || !VirtualQuery(base,&mbi,sizeof mbi)) return 0;
    const IMAGE_NT_HEADERS32* nt=image_header(base,mbi.RegionSize);
    if (!nt || !wrapped_executable(base,nt->OptionalHeader.SizeOfImage)) return 0;
    void** slot=iat_slot("d3d9.dll","Direct3DCreate9");
    if (!slot) return -1;
    g_deferred_original=(Direct3DCreate9Fn)*slot;
    if (!g_deferred_original || !iat_write(slot,(void*)deferred_install)) return -1;
    return 1;
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
        LOG("Touhou HFR v0.5.1-test loading; fps=%d vsync=%d substep=%d subtick_input=%d d3d9ex=%d max_frame_latency=%d flipex=%d enemy_interp=%d",
            cfg.fps, cfg.vsync, cfg.substep, cfg.subtick_input, cfg.d3d9ex, cfg.max_frame_latency, cfg.flipex, cfg.enemy_interp);
        LOG("video: scaling=%d filter=%s resizable=%d window_scale=%d snap_aspect=%d fullscreen_mode=%d internal_scale=%d texture_scale=%d (%s) dim=%d/%d/%d/%d/%d sharpen=%s/%d cursor=%d",
            cfg.scaling, cfg.filter_name, cfg.resizable, cfg.window_scale, cfg.snap_aspect, cfg.fullscreen_mode, cfg.internal_scale, cfg.texture_scale, cfg.texture_filter_name, cfg.dim[0], cfg.dim[1], cfg.dim[2], cfg.dim[3], cfg.dim[4], cfg.sharpen_name, cfg.sharpen_strength, cfg.cursor);
        if (!g_game || !install()) {
            int deferred = g_game ? 0 : arm_deferred_install();
            if (deferred>0)
                LOG("This executable's code is not readable yet, which is what a Steam release "
                    "looks like before its own start-up code has run. Trying again when the game asks for Direct3D.");
            else if (deferred<0)
                LOG("This executable is wrapped and its code is not readable yet, but it does not import "
                    "d3d9.dll!Direct3DCreate9, so there is nothing to try again from; HFR inactive. Please report this.");
            else
                LOG("Unsupported/modified executable or installation failure; HFR inactive (proxy forwarding available)");
        }
    }
    return TRUE;
}
