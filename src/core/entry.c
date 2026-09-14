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

/* ------------------------------------------------------------------ the late entry point
   Two things have to happen after this DLL's DllMain but before the game asks for Direct3D,
   and one function covers both.

   The first is a Steam release. That is the same executable inside a DRM wrapper: the game's
   .text is encrypted, and a stub in an appended section decrypts it, in memory, when the
   process starts. The loader runs DllMain *before* the executable's entry point, so at the
   moment the patch normally identifies the game there is nothing to identify -- every frozen
   signature still reads as ciphertext -- and it correctly concludes it does not know this
   executable. It has to look again once the game itself is running.

   The second is another patch. thcrap takes this patch's imports at the entry point and does
   not chain to what it replaced (imports.c), so they have to be taken back afterwards.

   Both want the same thing: something the game calls early, that no other patch redirects.
   kernel32.dll!QueryPerformanceCounter is that. These games call it before they ask for
   Direct3D -- verified, and it is why this is not hung on the Direct3D import itself, which
   is exactly the one thcrap takes -- it is cheap, and nothing translates or rewrites it,
   because unlike almost everything else patches detour it has no ANSI and Unicode pair and
   says nothing to the player. The import table is never encrypted, since the loader has to
   read it to start the process at all, so this can be armed while the wrapper's stub is
   still the only thing that has run.

   Callers before the game are expected -- the C runtime and the wrapper both use this clock
   -- so a call that finds the code still unreadable simply passes through and tries again.
   The stub stands down once the game is drawing, since nothing new redirects an import after
   that, or after a few thousand fruitless calls on an executable that is never going to
   identify. */
static struct ImportRedirect g_late_redirect;
static BOOL (WINAPI *g_late_original)(LARGE_INTEGER*);

/* Returns non-zero once there is nothing left for the late entry point to do. */
static int late_entry_work(void) {
    if (!g_game) {
        uint8_t* base=(uint8_t*)GetModuleHandleA(NULL);
        MEMORY_BASIC_INFORMATION mbi;
        const IMAGE_NT_HEADERS32* nt=NULL;
        if ((uintptr_t)base==0x400000 && VirtualQuery(base,&mbi,sizeof mbi)) nt=image_header(base,mbi.RegionSize);
        if (!nt || !select_game(base,nt->OptionalHeader.SizeOfImage)) return 0;   /* still ciphertext */
        if (!install()) { LOG("Identified after start-up but installation failed; HFR inactive"); return 1; }
        LOG("Identified once the executable had started: %s", g_game->identity->name);
        return 0;                       /* stay armed: imports can still be taken from here */
    }
    reassert_imports();
    return g_frame_seen;                /* the game is drawing; nothing new will detour now */
}
static BOOL WINAPI late_entry(LARGE_INTEGER* count) {
    static LONG busy;
    static int done, calls;
    /* This clock is read from more than one thread. The work is not re-entrant -- it can
       install the whole patch -- so one caller does it and the rest read the time. */
    if (!done && InterlockedCompareExchange(&busy,1,0)==0) {
        if (late_entry_work() || ++calls>20000) {
            done=1;
            unredirect_import(&g_late_redirect,(void*)late_entry);
        }
        InterlockedExchange(&busy,0);
    }
    return g_late_original(count);
}
/* 1 armed, 0 there is no such import to arm from -- which would leave a wrapped executable
   unpatchable and an unwrapped one merely unable to take its imports back, so it is worth a
   log line either way. Every supported game imports this. */
static int arm_late_entry(void) {
    if (!redirect_import(&g_late_redirect,"kernel32.dll","QueryPerformanceCounter",(void*)late_entry)) return 0;
    g_late_original=(BOOL (WINAPI *)(LARGE_INTEGER*))g_late_redirect.real;
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
        LOG("Touhou HFR v0.5.2-test loading; fps=%d vsync=%d substep=%d subtick_input=%d d3d9ex=%d max_frame_latency=%d flipex=%d enemy_interp=%d",
            cfg.fps, cfg.vsync, cfg.substep, cfg.subtick_input, cfg.d3d9ex, cfg.max_frame_latency, cfg.flipex, cfg.enemy_interp);
        LOG("video: scaling=%d filter=%s resizable=%d window_scale=%d snap_aspect=%d fullscreen_mode=%d internal_scale=%d texture_scale=%d (%s) dim=%d/%d/%d/%d/%d sharpen=%s/%d cursor=%d",
            cfg.scaling, cfg.filter_name, cfg.resizable, cfg.window_scale, cfg.snap_aspect, cfg.fullscreen_mode, cfg.internal_scale, cfg.texture_scale, cfg.texture_filter_name, cfg.dim[0], cfg.dim[1], cfg.dim[2], cfg.dim[3], cfg.dim[4], cfg.sharpen_name, cfg.sharpen_strength, cfg.cursor);
        int installed = g_game && install();
        /* Armed whether or not that worked: it is how a wrapped executable is identified at
           all, and how the imports are taken back from a patch that arrives later. */
        int armed = arm_late_entry();
        if (!installed) {
            uint8_t* img=(uint8_t*)GetModuleHandleA(NULL);
            MEMORY_BASIC_INFORMATION m;
            const IMAGE_NT_HEADERS32* h=NULL;
            if ((uintptr_t)img==0x400000 && VirtualQuery(img,&m,sizeof m)) h=image_header(img,m.RegionSize);
            int wrapped = !g_game && h && wrapped_executable(img,h->OptionalHeader.SizeOfImage);
            if (wrapped && armed)
                LOG("This executable's code is not readable yet, which is what a Steam release "
                    "looks like before its own start-up code has run. Trying again once the game is running.");
            else if (wrapped)
                LOG("This executable is wrapped and its code is not readable yet, but it does not import "
                    "kernel32.dll!QueryPerformanceCounter, so there is nothing to try again from; "
                    "HFR inactive. Please report this.");
            else
                LOG("Unsupported/modified executable or installation failure; HFR inactive (proxy forwarding available)");
        } else if (!armed)
            LOG("kernel32.dll!QueryPerformanceCounter is not imported; if another patch takes this "
                "one's imports later there will be no chance to take them back");
    }
    return TRUE;
}
