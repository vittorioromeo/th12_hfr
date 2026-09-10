static void emit_factor(void) { E(0xd8, 0x0d); E32((uint32_t)(uintptr_t)&g_factor); }

/* Gate a straight-line block; preserve the incoming flags on both branches.
   timer < 0 selects the global frame boundary, otherwise use the object's timer. */
static void gate_prefix(uintptr_t skip, int reg, int timer) {
    E(0x9c);
    if (timer < 0) E_not_major(); else E_timer_unchanged(reg, timer, timer + 4);
    E(0x75, 0x06, 0x9d); EJMP(skip);
    E(0x9d);
}
static void gate_block(uintptr_t addr, size_t n, uintptr_t skip, int reg, int timer) {
    STUB_BEGIN(); gate_prefix(skip, reg, timer); ECOPY(addr, n); EJMP(addr + n); site_hook(addr, n);
}
static void gate_callback(uintptr_t addr, size_t n) {
    STUB_BEGIN(); E(0x9c); E_timer_unchanged(R_ECX, 0x5c, 0x60);
    E(0x75, 0x04, 0x9d, 0x31, 0xc0, 0xc3); /* unchanged: restore flags, return 0 */
    E(0x9d); ECOPY(addr, n); EJMP(addr + n); site_hook(addr, n);
}
static void gate_shot_callback(uintptr_t addr) {
    STUB_BEGIN(); E(0x9c); E_timer_unchanged(R_EDX, 0, 4);
    E(0x75,0x04,0x9d,0x31,0xc0,0xc3);
    E(0x9d); ECOPY(addr,6); EJMP(addr+6); site_hook(addr,6);
}
