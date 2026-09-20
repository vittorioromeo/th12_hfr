/* The early x86 engines use D3D8. Translate its interfaces, then use the same
 * device hooks as D3D9 games. Never reinterpret a D3D8 pointer as D3D9. */
#ifdef HFR_NO_UI
static void* WINAPI hfr_bridge_create8(void* object) { (void)object; return NULL; }
#else
extern void* WINAPI hfr_bridge_create8(void* object);
#endif
static void* (WINAPI *orig_Direct3DCreate8)(UINT);
static void* WINAPI hook_Direct3DCreate8(UINT sdk) {
    /* The bridge builds the Direct3D 8 object itself, on top of Direct3D 9, so whoever else has
       taken this import is not called -- the same thing that happens to TH10-13's chain when
       this patch goes through Direct3DCreate9Ex (README, "Translation patches"). The
       alternative is to call them and lose the whole video path, scaling and F11 included, to
       a patch that in practice is there for the text. Said once, so it can be found. */
    {
        HMODULE native = GetModuleHandleA("d3d8.dll");
        void* factory = native ? (void*)GetProcAddress(native, "Direct3DCreate8") : NULL;
        static int told;
        if ((void*)orig_Direct3DCreate8 != factory && !told) {
            told = 1;
            LOG("D3D8: another patch has taken Direct3DCreate8; the bridge steps over it (its own Direct3D 8 hooks, if it has any, do not run)");
        }
    }
    HMODULE d9 = LoadLibraryA("d3d9.dll");
    orig_Direct3DCreate9 = d9 ? (Direct3DCreate9Fn)GetProcAddress(d9, "Direct3DCreate9") : NULL;
    if (!orig_Direct3DCreate9) return orig_Direct3DCreate8(sdk);
    void* result = hfr_bridge_create8(hook_Direct3DCreate9(D3D_SDK_VERSION));
    if (!result) {
        LOG("D3D8 bridge unavailable; keeping native D3D8 (no scaling, filters or F11 menu)");
        return orig_Direct3DCreate8(sdk);
    }
    LOG("D3D8 bridge active: shared D3D9 scaling, window and F11 menu");
    return result;
}
