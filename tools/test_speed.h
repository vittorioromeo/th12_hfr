/* The stubs that stand in for the game's own writes to the game speed.

   A speed stub is not a function call: it replaces one instruction, and the instruction after
   it belongs to the game, which expects every register it had. TH14 is where that stops being
   theoretical -- its stores are SSE (`movss [speed],xmm0`, and eleven immediate stores) sitting
   in the middle of SSE code, while the operation behind the stub is C compiled with
   `-mfpmath=sse`. So the stub saves the eight XMM registers as well as the general ones, and
   this test is what says so: it loads all eight with sentinels, calls the patched site, and
   checks that every one came back.

   It runs against real emitted code through the real installer, so it also covers the two new
   source forms -- an immediate (nothing to capture) and an XMM register (captured with a
   `movss` whose register number comes from the profile). */
float g_test_xmm_in[8][4] __attribute__((aligned(16),used));
float g_test_xmm_out[8][4] __attribute__((aligned(16),used));
uint32_t g_test_gpr_out[3] __attribute__((used));
void (*g_test_speed_site)(void) __attribute__((used));
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _test_call_speed_site\n_test_call_speed_site:\n"
    "  pushad\n"
    "  movaps xmm0,[_g_test_xmm_in+0x00]\n  movaps xmm1,[_g_test_xmm_in+0x10]\n"
    "  movaps xmm2,[_g_test_xmm_in+0x20]\n  movaps xmm3,[_g_test_xmm_in+0x30]\n"
    "  movaps xmm4,[_g_test_xmm_in+0x40]\n  movaps xmm5,[_g_test_xmm_in+0x50]\n"
    "  movaps xmm6,[_g_test_xmm_in+0x60]\n  movaps xmm7,[_g_test_xmm_in+0x70]\n"
    "  mov ebx,0x11111111\n  mov esi,0x22222222\n  mov edi,0x33333333\n"
    "  call dword ptr [_g_test_speed_site]\n"
    "  mov [_g_test_gpr_out+0],ebx\n  mov [_g_test_gpr_out+4],esi\n  mov [_g_test_gpr_out+8],edi\n"
    "  movaps [_g_test_xmm_out+0x00],xmm0\n  movaps [_g_test_xmm_out+0x10],xmm1\n"
    "  movaps [_g_test_xmm_out+0x20],xmm2\n  movaps [_g_test_xmm_out+0x30],xmm3\n"
    "  movaps [_g_test_xmm_out+0x40],xmm4\n  movaps [_g_test_xmm_out+0x50],xmm5\n"
    "  movaps [_g_test_xmm_out+0x60],xmm6\n  movaps [_g_test_xmm_out+0x70],xmm7\n"
    "  popad\n  ret\n"
    ".att_syntax\n"
);
extern void test_call_speed_site(void);

static void test_speed_registers_intact(const char* what) {
    for (int r = 0; r < 8; ++r)
        for (int l = 0; l < 4; ++l)
            if (g_test_xmm_in[r][l] != g_test_xmm_out[r][l]) {
                printf("FAIL: %s clobbered xmm%d lane %d (%f -> %f)\n",
                       what, r, l, g_test_xmm_in[r][l], g_test_xmm_out[r][l]);
                abort();
            }
    assert(g_test_gpr_out[0] == 0x11111111 && g_test_gpr_out[1] == 0x22222222 &&
           g_test_gpr_out[2] == 0x33333333);
}

static void test_speed_sites(void) {
    const struct GameProfile* real = g_game;
    struct GameProfile game = *real;
    struct GameIdentity id = *real->identity;

    /* Three sites in executable memory, each the instruction shape a real profile names, each
       followed by a `ret` so the harness can call it. Site 0 and 1 are the immediate store
       TH14 has eleven of; site 2 is its `movss [speed],xmm3` -- xmm3 rather than xmm0 so that
       a stub which captured the wrong register would be caught. */
    uint8_t* code = (uint8_t*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    assert(code);
    float speed_global = 0;
    const uint32_t sp = (uint32_t)(uintptr_t)&speed_global;
    uint8_t imm[10] = {0xc7,0x05,0,0,0,0,0x00,0x00,0x80,0x3f};   /* mov dword [speed],1.0f */
    memcpy(imm + 2, &sp, 4);
    uint8_t mvs[8] = {0xf3,0x0f,0x11,0x1d,0,0,0,0};              /* movss [speed],xmm3 */
    memcpy(mvs + 4, &sp, 4);
    memcpy(code + 0x000, imm, 10); code[0x00a] = 0xc3;
    memcpy(code + 0x020, imm, 10); code[0x02a] = 0xc3;
    memcpy(code + 0x040, mvs,  8); code[0x048] = 0xc3;

    struct GameSignature sigs[3] = {
        {(uintptr_t)code + 0x000, 10, {0}},
        {(uintptr_t)code + 0x020, 10, {0}},
        {(uintptr_t)code + 0x040,  8, {0}},
    };
    memcpy(sigs[0].bytes, imm, 10); memcpy(sigs[1].bytes, imm, 10); memcpy(sigs[2].bytes, mvs, 8);
    id.signatures = sigs; id.signature_count = 3;
    struct SpeedSite sites[3] = {
        {(uintptr_t)code + 0x000, 10, SPEED_ONE_PERM, SPEED_SRC_NONE},
        {(uintptr_t)code + 0x020, 10, SPEED_ONE_TEMP, SPEED_SRC_NONE},
        {(uintptr_t)code + 0x040,  8, SPEED_ECL,      SPEED_SRC_XMM3},
    };
    game.identity = &id; game.speed_sites = sites; game.speed_site_count = 3;
    game.addr.speed = (uintptr_t)&speed_global;
    g_game = &game;

    patch_begin();
    install_speed_sites();
    assert(patch_commit());
    /* Each site is now five bytes of call and the rest nops, so calling it runs the stub. */
    assert(code[0x000] == 0xe8 && code[0x005] == 0x90 && code[0x009] == 0x90);
    assert(code[0x040] == 0xe8 && code[0x047] == 0x90);

    for (int r = 0; r < 8; ++r)
        for (int l = 0; l < 4; ++l) g_test_xmm_in[r][l] = (float)(r * 4 + l) + 0.5f;
    g_test_xmm_in[3][0] = 0.25f;                  /* what the ECL site is about to store */

    float saved_logical = g_logical, saved_factor = g_factor;
    g_logical = 7; g_factor = 0.5f;

    /* A permanent store of 1.0: the logical speed becomes 1.0 and the global gets the factor. */
    g_test_speed_site = (void (*)(void))(code + 0x000);
    test_call_speed_site();
    assert(g_logical == 1.0f && speed_global == 0.5f);
    test_speed_registers_intact("the permanent store's stub");

    /* A temporary store of 1.0: the global gets the factor, the logical speed is untouched,
       because the game will put the raw value it saved back itself. */
    g_logical = 7;
    g_test_speed_site = (void (*)(void))(code + 0x020);
    test_call_speed_site();
    assert(g_logical == 7 && speed_global == 0.5f);
    test_speed_registers_intact("the temporary store's stub");

    /* And a store from a register: the value is the new logical speed, scaled on the way in. */
    g_test_speed_site = (void (*)(void))(code + 0x040);
    test_call_speed_site();
    assert(g_logical == 0.25f && speed_global == 0.125f);
    test_speed_registers_intact("the register store's stub");

    g_logical = saved_logical; g_factor = saved_factor; g_game = real;
    VirtualFree(code, 0, MEM_RELEASE);
    puts("PASS: speed stubs capture an immediate, an XMM register, and leave every register intact");
}

/* The fixed-point movement stub, which is the other piece of hand-written SSE in the patch and
   the one with the most ways to be quietly wrong. It was: the memory operand is re-emitted from
   the original instruction's modrm byte, and the first version took that byte unchanged, so a
   site whose destination was ECX loaded the velocity into xmm1 and truncated whatever the game
   had left in xmm0. The stubs disassemble to something plausible either way; only running them
   says which. */
uint32_t g_test_move_obj __attribute__((used));
uint32_t g_test_move_out __attribute__((used));
void (*g_test_move_site)(void) __attribute__((used));
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _test_call_move_site\n_test_call_move_site:\n"
    "  pushad\n"
    "  mov edi,[_g_test_move_obj]\n"
    "  movaps xmm0,[_g_test_xmm_in+0x00]\n  movaps xmm1,[_g_test_xmm_in+0x10]\n"
    "  call dword ptr [_g_test_move_site]\n"
    "  mov [_g_test_move_out],ecx\n"
    "  movaps [_g_test_xmm_out+0x00],xmm0\n  movaps [_g_test_xmm_out+0x10],xmm1\n"
    "  popad\n  ret\n"
    ".att_syntax\n"
);
extern void test_call_move_site(void);

static void test_movement_residual(void) {
    const struct GameProfile* real = g_game;
    struct GameProfile game = *real;
    struct GameIdentity id = *real->identity;

    uint8_t* code = (uint8_t*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    assert(code);
    /* cvttss2si ecx, dword [edi+0x604] -- the shape of TH14's player movement, ECX rather than
       EAX on purpose, because that is the case the modrm bug got wrong. */
    static const uint8_t site[8] = {0xf3,0x0f,0x2c,0x8f,0x04,0x06,0x00,0x00};
    memcpy(code, site, 8); code[8] = 0xc3;

    struct GameSignature sig = {(uintptr_t)code, 8, {0}};
    memcpy(sig.bytes, site, 8);
    id.signatures = &sig; id.signature_count = 1;
    game.identity = &id;
    g_game = &game;

    static uint8_t obj[0x700];
    g_test_move_obj = (uint32_t)(uintptr_t)obj;
    g_test_move_site = (void (*)(void))code;
    for (int r = 0; r < 8; ++r)
        for (int l = 0; l < 4; ++l) g_test_xmm_in[r][l] = (float)(r * 4 + l) + 0.5f;

    g_p = stub_begin(); patch_begin();
    movement_cvttss((uintptr_t)code, 0x8f, 0x604, R_ECX, 0);
    stub_end(); assert(patch_commit());

    float saved_factor = g_factor;
    /* Six ticks at a sixth of a frame, a velocity of 0.4 fixed-point units per tick. Truncated
       on its own that is six zeroes and a player who never moves; carried, it is the 2 units
       the whole frame was worth. */
    *(float*)(obj + 0x604) = 0.4f;
    g_factor = 1.0f / 6.0f; g_move_residual[0] = 0;
    int sum = 0;
    for (int i = 0; i < 6; ++i) { test_call_move_site(); sum += (int)g_test_move_out; }
    assert(sum == 2);
    /* The wrapper only loads and reads back the two registers this stub touches. */
    for (int r = 0; r < 2; ++r)
        for (int l = 0; l < 4; ++l)
            if (g_test_xmm_in[r][l] != g_test_xmm_out[r][l]) {
                printf("FAIL: the movement stub clobbered xmm%d lane %d (%f -> %f)\n",
                       r, l, g_test_xmm_in[r][l], g_test_xmm_out[r][l]);
                abort();
            }

    /* And with no sub-stepping it must be the instruction it replaced, residual or not. */
    g_factor = 1.0f; g_move_residual[0] = 0.9f;
    *(float*)(obj + 0x604) = 3.5f;
    test_call_move_site();
    assert(g_test_move_out == 3 && g_move_residual[0] == 0.9f);

    g_factor = saved_factor; g_move_residual[0] = 0; g_game = real;
    VirtualFree(code, 0, MEM_RELEASE);
    puts("PASS: fixed-point movement carries its truncation residual across sub-steps");
}
