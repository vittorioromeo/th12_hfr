/* ------------------------------------------------------------- imported functions
 * Two patches in one process do not fight over the game's code -- thcrap and this one write
 * to entirely different addresses -- they fight over its import address table, and for a
 * while this patch was losing silently.
 *
 * thcrap injects by letting the loader finish and stopping the game's thread at the
 * executable's entry point, so this DLL's DllMain, and with it the whole install, has already
 * run when thcrap's own code starts. thcrap then walks the import table, matches by *name*,
 * overwrites whatever it finds ("we can override any existing patches", says the comment in
 * its source) and chains to GetProcAddress(dll, func) -- the library's real function, not the
 * pointer it replaced. Anything already hooked there is not chained to; it is simply gone.
 * That removed this patch's Direct3D hook, which is how it obtains the device, so the patch
 * logged a successful install and then did nothing at all.
 *
 * The answer is not to do the same thing back. A hook that replaces an import records what
 * the slot held and calls it, whoever put it there, so that a patch arriving later is nested
 * inside rather than discarded -- which is all thcrap had to do. This patch does that, and
 * then simply takes its imports back once, from a function the game calls after the entry
 * point (entry.c: the late entry point). Whoever detoured in between ends up inside this
 * patch's hook instead of in place of it, and both run.
 *
 * What this deliberately does *not* do is redirect the exporting module's export table, which
 * would make GetProcAddress hand out this patch's address and so catch anyone chaining that
 * way without needing to take the import back at all. It is a wider change than it looks: an
 * import slot belongs to the game, but an export table belongs to the whole process, and
 * handing a foreign address to every module in it breaks software that has nothing to do with
 * the game. It was tried, and it crashed the game outright under the Steam overlay.
 */

/* The game's resolved import slot for dll!func, or NULL when it does not import it. */
static void** iat_slot(const char* dll, const char* func) {
    uint8_t* base=(uint8_t*)0x400000;   /* required by the selected executable profile */
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

/* Every import this patch has taken, so it can tell whether it still has them. Each entry is
   the hook that should be in the slot and where the pointer it chains to is kept, which is
   what has to be updated when the slot has changed hands. */
struct HookedImport { const char* dll; const char* func; void* hook; void** orig; };
static struct HookedImport g_hooked[16];
static size_t g_hooked_count;

/* Redirect the game's import of dll!func to hook, and report what the call now chains to --
   which is whatever the slot held, not necessarily the library's own function. Queued through
   the patch transaction with every other write, so a failure anywhere leaves the game
   untouched. */
static int hook_import(const char* dll, const char* func, void* hook, void** orig) {
    void** slot=iat_slot(dll,func);
    if (!slot) return 0;
    if (*slot==hook) return 1;                     /* already ours */
    if (!*slot) return 0;
    *orig=*slot;
    if (!patch_memory((uintptr_t)slot,&hook,sizeof hook,NULL)) return 0;
    if (g_hooked_count<sizeof g_hooked/sizeof *g_hooked) {
        struct HookedImport* h=&g_hooked[g_hooked_count++];
        h->dll=dll;h->func=func;h->hook=hook;h->orig=orig;
    }
    return 1;
}

/* Take back every import something else has redirected since, chaining to it rather than
   discarding it -- the courtesy this patch wanted and did not get. Returns how many changed
   hands, which is worth a log line: it is the one visible sign that another patch is here. */
static int reassert_imports(void) {
    int retaken=0;
    for (size_t i=0;i<g_hooked_count;++i) {
        struct HookedImport* h=&g_hooked[i];
        void** slot=iat_slot(h->dll,h->func);
        if (!slot || *slot==h->hook || !*slot) continue;
        *h->orig=*slot;                            /* they are now what we call */
        if (!iat_write(slot,h->hook)) continue;
        LOG("another patch had taken %s!%s; taken back, and it is now called from inside this one",
            h->dll,h->func);
        ++retaken;
    }
    return retaken;
}

/* The same redirection outside the patch transaction, and its reversal, for the triggers
   entry.c arms before there is a game to install into. */
struct ImportRedirect { void** slot; void* real; };
static int redirect_import(struct ImportRedirect* r, const char* dll, const char* func, void* hook) {
    memset(r,0,sizeof *r);
    void** slot=iat_slot(dll,func);
    if (!slot || !*slot || *slot==hook) return 0;
    r->real=*slot;
    if (!iat_write(slot,hook)) return 0;
    r->slot=slot;
    return 1;
}
static void unredirect_import(struct ImportRedirect* r, void* hook) {
    if (r->slot && *r->slot==hook) iat_write(r->slot,r->real);
    r->slot=NULL;
}
