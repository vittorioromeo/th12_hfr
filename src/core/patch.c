/* ------------------------------------------------------------------ patch utils */
/* Queue every game-code write first. Preflight and memory protection must all
   succeed before any instruction is replaced. Imports are patched separately. */
struct CodePatch { uintptr_t addr; size_t size; uint8_t before[32], after[32]; DWORD protection; };
static struct CodePatch g_patches[128];
static size_t g_patch_count;
static int g_patch_failed;
static const uint8_t* site_expected(uintptr_t addr, size_t n);
static void patch_begin(void) {g_patch_count=0;g_patch_failed=0;}
static int patch_memory(uintptr_t addr, const void* data, size_t n, const void* expect) {
    if (n>32 || g_patch_count>=128 || (expect && memcmp((void*)addr,expect,n))) {
        LOG("Patch preflight failed @%08x",(unsigned)addr);g_patch_failed=1;return 0;
    }
    for (size_t i=0;i<g_patch_count;++i) if (addr<g_patches[i].addr+g_patches[i].size && g_patches[i].addr<addr+n) {
        LOG("Overlapping code patches @%08x",(unsigned)addr);g_patch_failed=1;return 0;
    }
    struct CodePatch* p=&g_patches[g_patch_count++];p->addr=addr;p->size=n;
    memcpy(p->before,(void*)addr,n);memcpy(p->after,data,n);return 1;
}
static int patch_bytes(uintptr_t addr, const void* data, size_t n, const void* expect) {
    const uint8_t* frozen=site_expected(addr,n);
    if (!frozen || memcmp((void*)addr,frozen,n)) {g_patch_failed=1;return 0;}
    return patch_memory(addr,data,n,expect);
}
static int patch_commit(void) {
    if (g_patch_failed) return 0;
    size_t i=0;
    for (;i<g_patch_count;++i) {
        struct CodePatch* p=&g_patches[i];
        if (memcmp((void*)p->addr,p->before,p->size) ||
            !VirtualProtect((void*)p->addr,p->size,PAGE_EXECUTE_READWRITE,&p->protection)) break;
    }
    int ok=i==g_patch_count;
    if (ok) for (size_t j=0;j<g_patch_count;++j) memcpy((void*)g_patches[j].addr,g_patches[j].after,g_patches[j].size);
    /* Reverse order matters when two patches occupy the same page. */
    while (i) {struct CodePatch* p=&g_patches[--i];DWORD ignored;VirtualProtect((void*)p->addr,p->size,p->protection,&ignored);}
    if (ok) FlushInstructionCache(GetCurrentProcess(),NULL,0);
    return ok;
}
static int patch_call(uintptr_t addr, void* target, const void* expect5) {
    uint8_t b[5]; b[0] = 0xE8; int32_t rel = (int32_t)((uintptr_t)target - (addr + 5)); memcpy(b + 1, &rel, 4);
    return patch_bytes(addr, b, 5, expect5);
}
static int patch_jmp(uintptr_t addr, void* target, const void* expect5) {
    uint8_t b[5]; b[0] = 0xE9; int32_t rel = (int32_t)((uintptr_t)target - (addr + 5)); memcpy(b + 1, &rel, 4);
    return patch_bytes(addr, b, 5, expect5);
}
/* replace an n-byte instruction sequence (n>=5) with call target + nops */
static int patch_call_n(uintptr_t addr, void* target, size_t n, const void* expect) {
    uint8_t b[16]; memset(b, 0x90, sizeof b);
    b[0] = 0xE8; int32_t rel = (int32_t)((uintptr_t)target - (addr + 5)); memcpy(b + 1, &rel, 4);
    return patch_bytes(addr, b, n, expect);
}
