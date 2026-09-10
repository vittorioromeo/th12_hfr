/* ------------------------------------------------------------------ mini assembler for guard stubs */
static uint8_t* g_stub_mem; static size_t g_stub_used;
static uint8_t* stub_begin(void) {
    if (!g_stub_mem) g_stub_mem = (uint8_t*)VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    return g_stub_mem ? g_stub_mem + g_stub_used : NULL;
}
static uint8_t* g_p;
#define E(...) do { const uint8_t b_[] = { __VA_ARGS__ }; memcpy(g_p, b_, sizeof b_); g_p += sizeof b_; } while (0)
static void E32(uint32_t v) { memcpy(g_p, &v, 4); g_p += 4; }
static void EJMP(uintptr_t target) { *g_p++ = 0xE9; E32((uint32_t)(target - ((uintptr_t)g_p + 4))); }
static void EJCC(uint8_t cc, uintptr_t target) { *g_p++ = 0x0F; *g_p++ = cc; E32((uint32_t)(target - ((uintptr_t)g_p + 4))); } /* cc: 0x84 je, 0x85 jne, 0x8e jle ... */
static void ECALL(uintptr_t target) { *g_p++ = 0xE8; E32((uint32_t)(target - ((uintptr_t)g_p + 4))); }
static void ECOPY(uintptr_t src, size_t n) { memcpy(g_p, (void*)src, n); g_p += n; }
static void stub_end(void) { g_stub_used = (size_t)(g_p - g_stub_mem); }
/* redirect addr (n bytes, n>=5) to the stub being built at g_p */
static uint8_t* g_stub_start;
#define STUB_BEGIN() (g_stub_start = g_p)
static void hook_site(uintptr_t addr, size_t n, const void* expect) {
    uint8_t b[32]; memset(b, 0x90, sizeof b); b[0] = 0xE9; int32_t rel = (int32_t)((uintptr_t)g_stub_start - (addr + 5)); memcpy(b + 1, &rel, 4);
    if (!patch_bytes(addr, b, n, expect)) LOG("site patch @%08x failed", (unsigned)addr);
    g_stub_start = NULL;
}
/* emit "push eax; mov eax,[reg+prev]; cmp eax,[reg+cur]; pop eax" — ZF=1 if the timer's integer part did not change */
static void E_timer_unchanged(uint8_t reg_modrm_base, int32_t prev_off, int32_t cur_off) {
    E(0x50);                                            /* push eax */
    E(0x8B, 0x80 | reg_modrm_base); E32(prev_off);      /* mov eax,[reg+prev] */
    E(0x3B, 0x80 | reg_modrm_base); E32(cur_off);       /* cmp eax,[reg+cur]  */
    E(0x58);                                            /* pop eax */
}
/* emit "cmp dword [g_major],0" — ZF=1 on sub-ticks that are not frame boundaries */
static void E_not_major(void) { E(0x83, 0x3D); E32((uint32_t)(uintptr_t)&g_major); E(0x00); }
enum { R_EAX = 0, R_ECX = 1, R_EDX = 2, R_EBX = 3, R_EBP = 5, R_ESI = 6, R_EDI = 7 };
