static void emit_factor(void) { E(0xd8, 0x0d); E32((uint32_t)(uintptr_t)&g_factor); }
/* Carry the extra truncation introduced by dividing fixed-point player movement.
   Keep the game's exact conversion at stock speed, including native slow motion. */
static void movement_ftol(uintptr_t addr, uintptr_t ftol, unsigned axis) {
    uint8_t* st = g_p;
    E(0x81,0x3d); E32((uint32_t)(uintptr_t)&g_factor); E32(0x3f800000);
    E(0x75,0x05); EJMP(ftol);
    E(0xd8,0x05); E32((uint32_t)(uintptr_t)&g_move_residual[axis]); E(0xd9,0xc0);
    ECALL(ftol); E(0x50,0xdb,0x04,0x24,0x58,0xde,0xe9);
    E(0xd9,0x1d); E32((uint32_t)(uintptr_t)&g_move_residual[axis]); E(0xc3);
    site_call(addr,st);
}

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

/* The SSE form of movement_ftol, for the builds whose compiler emits `cvttss2si reg, dword
   [base+disp]` inline instead of loading the FPU and calling the game's ftol.

   The problem is the same: a position kept in fixed-point integers, advanced by the truncation
   of a float velocity that the game has already multiplied by the game speed. At one tick a
   frame the velocity is a whole number of fixed-point units and the truncation loses nothing;
   at a sixth of a frame it loses the fraction six times, and a velocity under six units a frame
   truncates to zero and the player does not move at all. So the fraction is carried: the
   residual is added back before the truncation and what the truncation dropped is kept for the
   next tick. With no sub-stepping the site runs exactly the instruction it replaced, because
   the game's own slow-motion truncates the same way it always did and that is not ours to fix.

   `src_modrm` is the modrm byte and disp32 of the original instruction's memory operand; it is
   re-emitted unchanged, so the operand must not be esp- or eip-relative (these sites are
   [edi+disp32]). The two XMM registers used are saved to static scratch rather than the stack
   so that the operand's base+disp stays valid. */
static float g_xmm_scratch[8] __attribute__((aligned(16)));
static void movement_cvttss(uintptr_t addr, uint8_t src_modrm, uint32_t src_disp,
                            unsigned dst_reg, unsigned axis) {
    uint8_t* st = g_p;
    E(0x9c);                                                       /* pushfd */
    E(0x81, 0x3d); E32((uint32_t)(uintptr_t)&g_factor); E32(0x3f800000);
    uint8_t* to_plain = g_p; E(0x74, 0x00);                        /* je plain */
    E(0x0f, 0x11, 0x05); E32((uint32_t)(uintptr_t)&g_xmm_scratch[0]);   /* movups [s0],xmm0 */
    E(0x0f, 0x11, 0x0d); E32((uint32_t)(uintptr_t)&g_xmm_scratch[4]);   /* movups [s1],xmm1 */
    /* The same memory operand, but into xmm0: the modrm is re-used for its mod and rm bits and
       its reg field forced to 0, because the byte came from an instruction whose destination
       was a general register. Taking it unchanged puts the value in whichever XMM register
       matches that general register's number, which for the first of these two sites is xmm1
       and leaves xmm0 holding whatever the game left there. */
    E(0xf3, 0x0f, 0x10, (uint8_t)(src_modrm & 0xc7)); E32(src_disp);   /* movss xmm0,[base+disp] */
    E(0xf3, 0x0f, 0x58, 0x05); E32((uint32_t)(uintptr_t)&g_move_residual[axis]);
    E(0xf3, 0x0f, 0x2c, (uint8_t)(0xc0 | (dst_reg << 3)));         /* cvttss2si dst,xmm0 */
    E(0xf3, 0x0f, 0x2a, (uint8_t)(0xc8 | dst_reg));                /* cvtsi2ss xmm1,dst */
    E(0xf3, 0x0f, 0x5c, 0xc1);                                     /* subss xmm0,xmm1 */
    E(0xf3, 0x0f, 0x11, 0x05); E32((uint32_t)(uintptr_t)&g_move_residual[axis]);
    E(0x0f, 0x10, 0x05); E32((uint32_t)(uintptr_t)&g_xmm_scratch[0]);
    E(0x0f, 0x10, 0x0d); E32((uint32_t)(uintptr_t)&g_xmm_scratch[4]);
    E(0x9d); E(0xc3);                                              /* popfd; ret */
    to_plain[1] = (uint8_t)(g_p - (to_plain + 2));
    ECOPY(addr, 8); E(0x9d); E(0xc3);                              /* plain: the original; popfd; ret */
    patch_call_n(addr, st, 8, site_expected(addr, 8));
}

/* A handful of counters a game's own site hooks can increment, printed on the debug stats line.
   The update and draw censuses answer "what is there"; this answers "which of this game's own
   guards is rejecting, and how often", which is the question left when a system is sub-stepped,
   looks right and quietly does nothing. Debug-only: the counting stubs are installed only when
   the ini asks for them, so a normal build has no extra code in the game's hot loops. */
enum { SITE_CENSUS = 8 };
static unsigned g_site_census[SITE_CENSUS];
static const char* g_site_census_name[SITE_CENSUS];
/* emit "inc dword [&g_site_census[i]]" -- 6 bytes, clobbers the flags */
static void E_count(unsigned i, const char* name) {
    g_site_census_name[i] = name;
    E(0xff, 0x05); E32((uint32_t)(uintptr_t)&g_site_census[i]);
}
static void site_census_report(void) {
    char line[256]; int n = 0, any = 0;
    if (!cfg.debug) return;
    for (unsigned i = 0; i < SITE_CENSUS; ++i) if (g_site_census_name[i]) {
        any = 1;
        n += snprintf(line + n, sizeof line - (size_t)n, "%s%s=%u",
                      n ? " " : "", g_site_census_name[i], g_site_census[i]);
        g_site_census[i] = 0;
    }
    if (any) LOG("site census: %s", line);
}
