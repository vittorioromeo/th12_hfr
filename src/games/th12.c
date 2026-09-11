/* TH12: reviewed game-specific hooks and object layout. */
static const struct SpeedSite th12_speed_sites[] = {
    {0x421d5f, 6, SPEED_ONE_PERM, 1},
    {0x4222a0, 6, SPEED_ONE_PERM, 1},
    {0x42f56d, 6, SPEED_ONE_PERM, 1},
    {0x436ed7, 6, SPEED_ONE_PERM, 1},
    {0x4653fc, 6, SPEED_ONE_PERM, 1},
    {0x4030fc, 6, SPEED_ONE_TEMP, 1},
    {0x455670, 6, SPEED_ONE_TEMP, 1},
    {0x432835, 6, SPEED_PAUSE_SET, 1},
    {0x43293c, 6, SPEED_PAUSE_SET, 1},
    {0x433853, 6, SPEED_PAUSE_SET, 1},
    {0x4339a2, 6, SPEED_PAUSE_SET, 1},
    {0x432988, 6, SPEED_PAUSE_RESTORE, 1},
    {0x433a1d, 6, SPEED_PAUSE_RESTORE, 1},
    {0x4348ed, 6, SPEED_PAUSE_RESTORE, 1},
    {0x4193e4, 6, SPEED_ECL, 1},
};
static const uint8_t FSTP_SPEED[6] = { 0xD9, 0x1D, 0xD0, 0x2E, 0x4B, 0x00 };



static void th12_install_sites(void) {
    g_p = stub_begin();
    /* fmul dword [g_factor] : scale a per-frame amount by the sub-step only (not by the ECL slow-motion factor,
       which the original code did not apply to these either) */
    uint8_t FMUL_SPEED[6] = { 0xD8, 0x0D, 0, 0, 0, 0 }; { uint32_t a = (uint32_t)(uintptr_t)&g_factor; memcpy(FMUL_SPEED + 2, &a, 4); }

    /* --- Player shots: pos += vel * speed (0x437016..0x43702a, 20 bytes) --- */
    { static const uint8_t ex[] = { 0xD9, 0x46, 0xE0, 0x8B, 0x44, 0x24, 0x10, 0xD8, 0x00, 0xD9, 0x18, 0xD9, 0x46, 0xE8, 0xD8, 0x46, 0xE4, 0xD9, 0x5E, 0xE4 };
      STUB_BEGIN();
      E(0xD9, 0x46, 0xE0); E(FMUL_SPEED[0], FMUL_SPEED[1], FMUL_SPEED[2], FMUL_SPEED[3], FMUL_SPEED[4], FMUL_SPEED[5]);
      E(0x8B, 0x44, 0x24, 0x10);          /* mov eax,[esp+0x10] (same frame: we jumped, not called) */
      E(0xD8, 0x00, 0xD9, 0x18);          /* fadd [eax]; fstp [eax] */
      E(0xD9, 0x46, 0xE8); E(FMUL_SPEED[0], FMUL_SPEED[1], FMUL_SPEED[2], FMUL_SPEED[3], FMUL_SPEED[4], FMUL_SPEED[5]);
      E(0xD8, 0x46, 0xE4, 0xD9, 0x5E, 0xE4);
      EJMP(0x43702a);
      hook_site(0x437016, 20, ex); }
    /* --- Player shots: angle += angular velocity * speed (0x436fe2: fld [esi+0x18]; push ecx; fadd [esi+0x14]) --- */
    { static const uint8_t ex[] = { 0xD9, 0x46, 0x18, 0x51, 0xD8, 0x46, 0x14 };
      STUB_BEGIN();
      E(0xD9, 0x46, 0x18); E(FMUL_SPEED[0], FMUL_SPEED[1], FMUL_SPEED[2], FMUL_SPEED[3], FMUL_SPEED[4], FMUL_SPEED[5]);
      E(0x51); E(0xD8, 0x46, 0x14);
      EJMP(0x436fe9);
      hook_site(0x436fe2, 7, ex); }
    /* --- Player: death particles once per frame: cmp [edi+0xa34],3 @0x436dd9 (jne @0x436de0 -> 0x436e91) --- */
    { static const uint8_t ex[] = { 0x83, 0xBF, 0x34, 0x0A, 0x00, 0x00, 0x03 };
      STUB_BEGIN();
      E(0x83, 0xBF, 0x34, 0x0A, 0x00, 0x00, 0x03);   /* cmp [edi+0xa34],3 */
      EJCC(0x85, 0x436de0);                           /* jne -> original jne (taken) */
      E_timer_unchanged(R_EDI, 0xa30, 0xa34);
      EJCC(0x84, 0x436e91);                           /* unchanged -> skip block */
      E(0x39, 0xFF);                                  /* cmp edi,edi -> ZF=1 */
      EJMP(0x436de0);
      hook_site(0x436dd9, 7, ex); }
    /* --- Player: state_timer % 60 == 0 counter once per frame @0x4374fc --- */
    { static const uint8_t ex[] = { 0x8B, 0x87, 0x34, 0x0A, 0x00, 0x00 };
      STUB_BEGIN();
      E(0x8B, 0x87, 0x34, 0x0A, 0x00, 0x00);          /* mov eax,[edi+0xa34] */
      E(0x3B, 0x87, 0x30, 0x0A, 0x00, 0x00);          /* cmp eax,[edi+0xa30] */
      EJCC(0x84, 0x43753d);
      EJMP(0x437502);
      hook_site(0x4374fc, 6, ex); }
    /* --- Player: option gather counter inc [edi+0xc418] once per frame @0x4368f7 --- */
    { static const uint8_t ex[] = { 0xFF, 0x87, 0x18, 0xC4, 0x00, 0x00 };
      STUB_BEGIN();
      E_timer_unchanged(R_EDI, 0xa30, 0xa34);
      EJCC(0x84, 0x4368fd);
      E(0xFF, 0x87, 0x18, 0xC4, 0x00, 0x00);
      EJMP(0x4368fd);
      hook_site(0x4368f7, 6, ex); }
    /* --- Bullet: per-frame counters [ebp+4]-- and [ebp+0x520]-- once per frame @0x409fdb..0x409ff7 --- */
    { static const uint8_t ex[] = { 0x8B, 0x45, 0x04, 0x85, 0xC0 };
      STUB_BEGIN();
      E_timer_unchanged(R_EBP, 0x4e4, 0x4e8);
      EJCC(0x84, 0x409ff7);
      E(0x8B, 0x45, 0x04, 0x85, 0xC0);
      EJMP(0x409fe0);
      hook_site(0x409fdb, 5, ex); }

    /* --- Items: "+= 0.2 per frame" (fadd qword [0x4a3fb8]) -> += 0.2 * speed --- */
    { static const uintptr_t sites[] = { 0x425fc5, 0x426080, 0x426218 };
      static const uint8_t ex[] = { 0xDC, 0x05, 0xB8, 0x3F, 0x4A, 0x00 };
      for (int i = 0; i < 3; i++) {
          STUB_BEGIN();
          E(0xDD, 0x05, 0xB8, 0x3F, 0x4A, 0x00);        /* fld qword [0x4a3fb8] */
          E(0xD8, 0x0D, 0xD0, 0x2E, 0x4B, 0x00);        /* fmul dword [speed] */
          E(0xDE, 0xC1);                                /* faddp st(1),st */
          EJMP(sites[i] + 6);
          hook_site(sites[i], 6, ex);
      } }
    /* --- Items: UFO attraction acceleration: scale increment before "fadd [edi+0x9bc]" @0x426926 --- */
    { static const uint8_t ex[] = { 0xD8, 0x87, 0xBC, 0x09, 0x00, 0x00 };
      STUB_BEGIN();
      E(0xD8, 0x0D, 0xD0, 0x2E, 0x4B, 0x00);            /* fmul dword [speed] */
      E(0xD8, 0x87, 0xBC, 0x09, 0x00, 0x00);
      EJMP(0x42692c);
      hook_site(0x426926, 6, ex); }
    /* --- Items: state-5 countdown [edi+0x9c0]-- once per frame @0x425c5c (jns @0x425c63 -> 0x426f53) --- */
    { static const uint8_t ex[] = { 0x83, 0x87, 0xC0, 0x09, 0x00, 0x00, 0xFF };
      STUB_BEGIN();
      E_not_major();                                    /* (state-5 items do not tick their timer) */
      EJCC(0x84, 0x426f53);                             /* not a frame tick: behave as "not negative yet" */
      E(0x83, 0x87, 0xC0, 0x09, 0x00, 0x00, 0xFF);
      EJMP(0x425c63);
      hook_site(0x425c5c, 7, ex); }

    /* --- Lasers: ex wait counter [laser+0x44c]-- once per frame (timer +0x14/+0x18 ticked by the manager) --- */
    { struct { uintptr_t addr; uint8_t reg; uintptr_t skip, cont; } L[] = {
          { 0x42979a, R_EDI, 0x4297ab, 0x4297a0 },   /* LaserLine  */
          { 0x42c90c, R_ESI, 0x42c91d, 0x42c912 },   /* LaserCurve */
          { 0x42adef, R_EDI, 0x42ae00, 0x42adf5 } }; /* LaserBeam  */
      for (int i = 0; i < 3; i++) {
          uint8_t ex[6] = { 0x8B, (uint8_t)(0x80 | L[i].reg), 0x4C, 0x04, 0x00, 0x00 };
          STUB_BEGIN();
          E_timer_unchanged(L[i].reg, 0x14, 0x18);
          EJCC(0x84, L[i].skip);
          E(0x8B, (uint8_t)(0x80 | L[i].reg), 0x4C, 0x04, 0x00, 0x00);
          EJMP(L[i].cont);
          hook_site(L[i].addr, 6, ex);
      } }
    /* --- Lasers: graze every 3 frames -> gate on the graze timer's integer change --- */
    { struct { uintptr_t addr; uint8_t reg; int32_t cur, prev; uintptr_t skip, cont; } G[] = {
          { 0x429a55, R_EDI, 0x2c, 0x28, 0x429a76, 0x429a5e },   /* LaserLine  */
          { 0x42cbf7, R_ESI, 0x2c, 0x28, 0x42cc1d, 0x42cc00 },   /* LaserCurve */
          { 0x42b068, R_EDI, 0x18, 0x14, 0x42b089, 0x42b071 } }; /* LaserBeam  */
      for (int i = 0; i < 3; i++) {
          uint8_t ex[9] = { 0x8B, (uint8_t)(0x40 | G[i].reg), (uint8_t)G[i].cur, 0x99, 0xB9, 0x03, 0x00, 0x00, 0x00 };
          STUB_BEGIN();
          E(0x8B, (uint8_t)(0x40 | G[i].reg), (uint8_t)G[i].cur);      /* mov eax,[reg+cur] */
          E(0x3B, (uint8_t)(0x40 | G[i].reg), (uint8_t)G[i].prev);     /* cmp eax,[reg+prev] */
          EJCC(0x84, G[i].skip);
          E(0x99, 0xB9, 0x03, 0x00, 0x00, 0x00);                        /* cdq; mov ecx,3 */
          EJMP(G[i].cont);
          hook_site(G[i].addr, 9, ex);
      } }

    /* --- Stage: spell/bomb background distortion (uses RNG every frame) only on frame ticks @0x403145 --- */
    { static const uint8_t ex[] = { 0x8B, 0x8B, 0xDC, 0x35, 0x00, 0x00, 0x33, 0xFF, 0x3B, 0xCF };
      STUB_BEGIN();
      E(0x8B, 0x8B, 0xDC, 0x35, 0x00, 0x00);          /* mov ecx,[ebx+0x35dc] */
      E(0x33, 0xFF, 0x3B, 0xCF);                      /* xor edi,edi; cmp ecx,edi */
      EJCC(0x84, 0x4036dd);
      E_not_major();
      EJCC(0x84, 0x4036dd);
      EJMP(0x403155);
      hook_site(0x403145, 10, ex); }
    /* --- Stage: distortion frame counter [ebx+0x35d8]++ only on frame ticks @0x4036dd --- */
    { static const uint8_t ex[] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0x01, 0x83, 0xD8, 0x35, 0x00, 0x00 };
      STUB_BEGIN();
      E(0xB8, 0x01, 0x00, 0x00, 0x00);                /* mov eax,1 (also the return value) */
      E_not_major();
      EJCC(0x84, 0x4036e8);
      E(0x01, 0x83, 0xD8, 0x35, 0x00, 0x00);          /* add [ebx+0x35d8],eax */
      EJMP(0x4036e8);
      hook_site(0x4036dd, 11, ex); }

    /* --- Player movement: fixed-point step = ftol(vel*speed) loses up to 1 unit per tick; carry the residual --- */
    {
      static const uint8_t ex[] = { 0xE8, 0x11, 0xCA, 0x05, 0x00 };
      static const uint8_t ex2[] = { 0xE8, 0xFE, 0xC9, 0x05, 0x00 };
      for (int i = 0; i < 2; i++) {
          uintptr_t site = i == 0 ? 0x4367ca : 0x4367dd;
          uint8_t* st = g_p;
          E(0x81,0x3d); E32((uint32_t)(uintptr_t)&g_factor); E32(0x3f800000);
          E(0x75,0x05); EJMP(0x4931e0);
          E(0xD8, 0x05); E32((uint32_t)(uintptr_t)&g_move_residual[i]);        /* fadd dword [res] */
          E(0xD9, 0xC0);                                            /* fld st(0) */
          ECALL(0x4931e0);                                          /* eax = trunc(st0) */
          E(0x50, 0xDB, 0x04, 0x24, 0x58);                          /* push eax; fild dword [esp]; pop eax */
          E(0xDE, 0xE9);                                            /* fsubp st(1),st  -> residual */
          E(0xD9, 0x1D); E32((uint32_t)(uintptr_t)&g_move_residual[i]);        /* fstp dword [res] */
          E(0xC3);                                                  /* ret */
          patch_call(site, st, i == 0 ? ex : ex2);
      } }

    /* --- Timer::add sites whose argument is a script/engine constant in frames, not a per-frame rate:
           add value * logical speed (stock semantics) instead of value * logical * dt.
           0x439ac2: player shot cycle timer -= 14 ; 0x43adbd: ANM "timer -= N" helper --- */
    { static const uint8_t ex1[] = { 0xE8, 0x59, 0xAF, 0x02, 0x00 }, ex2[] = { 0xE8, 0x5E, 0x9C, 0x02, 0x00 };
      uint8_t* st = g_p;
      E(0x8B, 0x46, 0x04, 0x89, 0x06);                              /* mov eax,[esi+4]; mov [esi],eax */
      E(0xD9, 0x44, 0x24, 0x04);                                    /* fld dword [esp+4] */
      E(0xD8, 0x0D); E32((uint32_t)(uintptr_t)&g_logical);          /* fmul dword [g_logical] */
      E(0xD8, 0x46, 0x08, 0xD9, 0x56, 0x08);                        /* fadd [esi+8]; fst [esi+8] */
      ECALL(0x4931e0);
      E(0x89, 0x46, 0x04);                                          /* mov [esi+4],eax */
      E(0xC2, 0x04, 0x00);                                          /* ret 4 */
      patch_call(0x439ac2, st, ex1);
      patch_call(0x43adbd, st, ex2); }

    /* --- MotionState::step (0x464db0): pos += vel  ->  pos += vel * g_factor (player shots, damage sources; enemies/bombs run with factor 1) --- */
    { static const uint8_t ex[] = { 0xD9, 0x43, 0x0C, 0x8B, 0xF3, 0xD8, 0x03, 0xD9, 0x1B, 0xD9, 0x43, 0x10, 0xD8, 0x43, 0x04, 0xD9, 0x5B, 0x04, 0xD9, 0x43, 0x14, 0xD8, 0x43, 0x08, 0xD9, 0x5B, 0x08 };
      STUB_BEGIN();
      E(0xD9, 0x43, 0x0C); E(FMUL_SPEED[0], FMUL_SPEED[1], FMUL_SPEED[2], FMUL_SPEED[3], FMUL_SPEED[4], FMUL_SPEED[5]); E(0xD8, 0x03, 0xD9, 0x1B);
      E(0xD9, 0x43, 0x10); E(FMUL_SPEED[0], FMUL_SPEED[1], FMUL_SPEED[2], FMUL_SPEED[3], FMUL_SPEED[4], FMUL_SPEED[5]); E(0xD8, 0x43, 0x04, 0xD9, 0x5B, 0x04);
      E(0xD9, 0x43, 0x14); E(FMUL_SPEED[0], FMUL_SPEED[1], FMUL_SPEED[2], FMUL_SPEED[3], FMUL_SPEED[4], FMUL_SPEED[5]); E(0xD8, 0x43, 0x08, 0xD9, 0x5B, 0x08);
      E(0x8B, 0xF3);                                   /* mov esi,ebx */
      EJMP(0x464dd7);
      hook_site(0x464dbc, 27, ex); }

    /* --- Player shots (0x439b10 loop): speed += accel * factor @0x439b72 --- */
    { static const uint8_t ex[] = { 0xD9, 0x47, 0x18, 0x51, 0xD8, 0x47, 0x14 };
      STUB_BEGIN();
      E(0xD9, 0x47, 0x18); E(FMUL_SPEED[0], FMUL_SPEED[1], FMUL_SPEED[2], FMUL_SPEED[3], FMUL_SPEED[4], FMUL_SPEED[5]);
      E(0x51); E(0xD8, 0x47, 0x14);
      EJMP(0x439b79);
      hook_site(0x439b72, 7, ex); }

    /* --- Enemy hit test guard (0x439ed0): "player state timer unchanged this frame -> no damage".
           With sub-steps the integer timer changes on one tick in K; use "player timer advanced since the
           previous Player update" instead (tracked by the runner). @0x439ef2: cmp eax,[esi+0xa30]; jne 0x439f05 --- */
    { static const uint8_t ex[] = { 0x3B, 0x86, 0x30, 0x0A, 0x00, 0x00 };
      STUB_BEGIN();
      E(0x50);                                            /* push eax */
      E(0xA1); E32((uint32_t)(uintptr_t)&g_ptf_prev);      /* mov eax,[g_ptf_prev] */
      E(0x3B, 0x05); E32((uint32_t)(uintptr_t)&g_ptf_cur); /* cmp eax,[g_ptf_cur] */
      E(0x58);                                            /* pop eax */
      EJCC(0x85, 0x439f05);                               /* changed -> proceed */
      EJMP(0x439efa);                                     /* unchanged -> return 0 */
      hook_site(0x439ef2, 6, ex); }

    /* --- Enemy death ring effect callback (0x4107e0): shrink/fade once per frame @0x410814 --- */
    { static const uint8_t ex[] = { 0xD9, 0x47, 0x20, 0x80, 0x47, 0x2B, 0x03, 0xDC, 0x25, 0x20, 0x42, 0x4A, 0x00, 0xD9, 0x5F, 0x20 };
      STUB_BEGIN();
      E_timer_unchanged(R_EDI, 0x0c, 0x10);
      EJCC(0x84, 0x410824);
      ECOPY(0x410814, 16);
      EJMP(0x410824);
      hook_site(0x410814, 16, ex); }

    /* --- Scrolling-mesh effect VM callback (0x45dcd0, fastcall ECX=vm): per-frame UV scroll; run once per frame --- */
    { static const uint8_t ex[] = { 0x83, 0xEC, 0x18, 0xD9, 0x05, 0xC8, 0x3D, 0x4A, 0x00 };
      STUB_BEGIN();
      E(0x50);                                            /* push eax */
      E(0x8B, 0x81); E32(0x68);                           /* mov eax,[ecx+0x68] (timer prev) */
      E(0x3B, 0x81); E32(0x6c);                           /* cmp eax,[ecx+0x6c] (timer cur) */
      E(0x58);                                            /* pop eax */
      E(0x75, 0x03);                                      /* jne run */
      E(0x31, 0xC0, 0xC3);                                /* xor eax,eax; ret   (skip this tick) */
      ECOPY(0x45dcd0, 9);                                 /* run: original prologue */
      EJMP(0x45dcd9);
      hook_site(0x45dcd0, 9, ex); }
    stub_end();
    LOG("site patches installed (%u bytes of stubs)", (unsigned)g_stub_used);
}



static void th12_place_enemy(uint8_t* e, uint8_t* am, uint32_t flags, const float* R) {
        int* ids = (int*)(e + 0x1120); float* offs = (float*)(e + 0x1168); int* parent = (int*)(e + 0x1220);
        if (!(flags & 0x2000000)) {
            for (int i = 0; i < 14; i++) {
                if (!ids[i]) continue;
                float* vm = anm_get_vm(am, ids[i]);
                if (!vm) continue;
                float x = R[0] + offs[i*3], y = R[1] + offs[i*3+1], z = R[2] + offs[i*3+2];
                if (parent[i] >= 0 && parent[i] < 14 && ids[parent[i]]) {
                    float* pvm = anm_get_vm(am, ids[parent[i]]);
                    if (pvm) { x += pvm[0x109]; y += pvm[0x10a]; z += pvm[0x10b]; }
                }
                vm[0x10c] = x + 224.0f; vm[0x10d] = y + 16.0f; vm[0x10e] = z;
            }
        } else {
            for (int i = 0; i < 14; i++) {
                if (!ids[i]) continue;
                float* vm = anm_get_vm(am, ids[i]);
                if (!vm) continue;
                vm[0x10c] = R[0]; vm[0x10d] = R[1]; vm[0x10e] = R[2];
            }
        }
}
static const struct node_class th12_classes[] = {
    /* func (UpdateFunc callback address) , mode , name */
    { 0x40a1f0, MODE_SUB,   "BulletManager"   },
    { 0x437660, MODE_SUB,   "Player"          },
    { 0x406bb0, MODE_FRAME, "Bomb"            },
    { 0x427380, MODE_SUB,   "ItemManager"     },
    { 0x4283d0, MODE_SUB,   "LaserManager"    },
    { 0x41f8d0, MODE_FRAME, "Gui"             },
    { 0x403ec0, MODE_SUB,   "Stage"           },
    { 0x460c40, MODE_SUB,   "AnmManagerWorld" },
    { 0x460c30, MODE_SUB,   "AnmManagerUI"    },
    { 0x40e040, MODE_FRAME, "Spellcard"       },
    { 0x413210, MODE_FRAME, "EnemyManager"    },
    { 0x44a860, MODE_FRAME, "UfoManager"      },
    { 0x424190, MODE_FRAME, "PlayerBomb?"     },
    { 0x422bd0, MODE_FRAME, "GameManager"     },
};

static const struct GameProfile th12_profile = {
    .identity = &game_identities[GI_TH12],
    .addr = {
        .speed = 0x4b2ed0,
        .update_runner = 0x4ce89c,
        .device = 0x4ce8f0,
        .pp = 0x4ce9dc,
        .window_flags = 0x4cf428,
        .frame_time = 0x4cf2a0,
        .misc_flags = 0x4cee78,
        .raw_input = 0x4d48b8,
        .raw_pressed = 0x4d48c4,
        .replay_manager = 0x4b4518,
        .frame_fn = 0x450600,
        .enemy_manager = 0x4b43dc,
        .anm_manager = 0x4ce8cc,
        .anm_get_vm = 0x461920,
        .game_input = 0x4d49d0,
        .option_flags = 0x4ceae8,
        .autofocus = 0x4d49cc,
        .poll_input = 0x462ec0,
        .remove_node = 0x462890,
        .crit = 0x4cf0f8,
        .crit_count = 0x4cf218,
        .gm_callback = 0x422bd0,
        .game_manager = 0x4b44e8,
        .record_callback = 0x43c510,
        .playback_callback = 0x43c520,
        .player_callback = 0x437660,
        .game_pressed = 0x4d49dc,
        .game_released = 0x4d49e0,
        .player = 0x4b4514,
        .frame_context_ptr = 0x4cee34,
        .frame_flag = 0x4cee38,
        .frame_context_value = 0x4cec04,
        .cleanup_fn = 0x464c40,
        .cleanup_this = 0x4cf0d8,
        .replay_save = 0x43bc10,
        .replay_load = 0x43c350,
        .frame_calls = {0x44f881,0x44f89e,0x44f8aa},
        .replay_saves = {0x433444,0x434459,0x43519b,0x448e4f},
        .replay_load_call = 0x43b1d2,
        .runner_fn = 0x4624c0,
        .latency_cmp = 0x450729,
        .screenshot_fn = 0x42fca0, .screenshot_call = 0x450891,
    },
    .layout = {
        .runner_ending = 0x48, .gm_pause_flags = 0x60, .input_size = 0x130, .input_width = 4,
        .replay_stage = 0x1d8,
        .replay_frame = 0x1d0,
        .replay_stages = 0x20,
        .player_timer = 0xa38,
        .enemy_flags = 0x26f8,
        .enemy_position = 0x1074,
        .enemy_skip_mask = 0x1000000,
    },
    .speed_sites = th12_speed_sites, .speed_site_count = sizeof th12_speed_sites / sizeof *th12_speed_sites,
    .critical_flag_mask = 0x8000, .runner_return8_ends = 1,
    .classes = th12_classes, .class_count = sizeof th12_classes / sizeof *th12_classes,
    .mask_minor_player_edges = 0, .d3dx = "d3dx9_40.dll",
    .native_size_cycle = 1,
    .draw = { .dispatch = 0x462691, .dispatch_len = 8, .node_reg = R_ESI, .prio_off = 0, .flush_fn = 0x45a3c0, .flush_reg = R_ESI, .flush_this = 0x4ce8cc, .world_prio = 12, .item_prios = { 27, -1, -1, -1 } },
    .install_sites = th12_install_sites,
    .place_enemy = th12_place_enemy,
};
