/* Dimming (video.dim_*): fade the background, the pickups, the cosmetic effects, the player's
 * shots and a game's own extra class, each by a percentage.
 *
 * All exist for one reason: bullets should be the most visible thing on the screen, and neither
 * a busy stage background nor a rain of P items, explosions and one's own shots should compete.
 *
 * What draws what. Every object registers a draw callback with a priority, and each frame the
 * draw runner calls them in that order: the stage's 3D scene first, then the sprite manager's
 * layers interleaved with the managers that draw their own sprites (enemies, items, lasers,
 * bullets, the player), then the interface. None of them touch Direct3D directly: a 2D sprite
 * goes into the sprite manager's batch, which is flushed as one DrawPrimitiveUP when the
 * texture or blend changes, or when someone asks. So a draw call, on its own, says nothing
 * about which object it belongs to -- the items' quads are typically flushed by the first
 * sprite of the next callback.
 *
 * The profile therefore names the runner's dispatch: the instructions that call one node's
 * callback. They are wrapped (dim_install) to flush the batch, record the node's priority in
 * g_draw_prio, call the callback, flush again and forget the priority. Every draw call then
 * happens under exactly one callback. Where one callback draws several classes -- the
 * sprite-layer callbacks draw the player's shots, effects, spirits and more in one list -- the
 * sprite VM draw is wrapped too, at both ends: each VM is classified by the profile's rules
 * (its loaded ANM's file name, its sprite layer, the callback's priority); a classified VM has
 * the batch flushed before it if something else is pending and after it always, so its quads
 * are a draw call of their own and quads from paths the wrap does not see (a manager building
 * quads itself, a VM drawn through another entry) can never share a faded call. So every draw
 * call carries one class, g_batch_class, which is what the Direct3D hooks fade by.
 *
 * Background: before the first callback with priority >= world_prio, a black quad with the
 * configured alpha is blended over the current viewport, on top of everything drawn so far.
 * Doing it there rather than modulating the background's own draws means fog, additive
 * layers, multi-pass stage effects and offscreen compositing all fade together, whatever they
 * do; and it lands in whichever target the background was drawn into. A rule may also put a
 * sprite in the background class (TH10's spell backgrounds, drawn above the world): it is then
 * faded in colour rather than alpha, towards black like the rest.
 *
 * Everything else is faded in its own draw call: the vertex colours of a 2D-mode sprite (the
 * sprite builder puts the colour in the vertices, and the fixed-function pipeline multiplies
 * the texture by it), or D3DRS_TEXTUREFACTOR for a 3D-mode sprite drawn from the game's unit
 * quad. Additively blended draws do not fade with alpha, so their colour is scaled instead,
 * which for that blend is the same thing. */
static volatile int g_draw_prio = -1;      /* priority of the draw callback running, -1 outside the runner */
static volatile uint8_t* g_draw_node;      /* its node, for the debug trace */
/* g_dim_available (timing.c): the profile describes the dispatch and the wrap is in */
static int g_dim_drawing;                  /* our own quad is going through the hooked draw */
static IDirect3DStateBlock9* g_dim_state;
static volatile int g_batch_class = DIM_NONE;   /* the class of whatever is in the sprite batch / being drawn */
static char g_vm_since[400]; static int g_vm_since_n;   /* debug: VMs entered since the last traced draw */
/* debug: g_dim_trace_frames frames have every draw logged */
static void dim_trace(IDirect3DDevice9* dev, const char* what, UINT prims, DWORD fvf, int active, void* caller) {
    if (g_dim_trace_frames <= 0 || !cfg.debug) return;
    D3DVIEWPORT9 vp = {0}; dev->lpVtbl->GetViewport(dev, &vp);
    DWORD src = 0, dst = 0, tex = 0; IDirect3DBaseTexture9* t = NULL;
    dev->lpVtbl->GetRenderState(dev, D3DRS_SRCBLEND, &src); dev->lpVtbl->GetRenderState(dev, D3DRS_DESTBLEND, &dst);
    dev->lpVtbl->GetTexture(dev, 0, &t); if (t) { tex = (DWORD)(uintptr_t)t; t->lpVtbl->Release(t); }
    LOG("draw %3d: %-6s prio %2d class %2d fvf 0x%03x prims %3u vp %u,%u %ux%u blend %lu/%lu tex %08lx from %p%s", g_dim_trace_n++, what, g_draw_prio, g_batch_class, (unsigned)fvf, prims,
        vp.X, vp.Y, vp.Width, vp.Height, (unsigned long)src, (unsigned long)dst, (unsigned long)tex, caller, active ? "" : " (not the game RT)");
    if (g_vm_since_n) { LOG("draw        vms:%s", g_vm_since); g_vm_since_n = 0; g_vm_since[0] = 0; }
}

/* Wrap the draw runner's dispatch:
 *   mov eax,[node+prio_off] ; mov [g_draw_prio],eax ; mov [g_draw_node],node
 *   call flush ; call dim_at_callback
 *   <the dispatch bytes: load argument, load callback, call it>
 *   push eax ; call flush ; pop eax
 *   mov dword [g_draw_prio],-1 ; mov dword [g_batch_class],-1
 *   jmp dispatch+len
 * flush: push reg ; mov reg,[flush_this] ; call flush_fn ; pop reg ; ret
 * EAX is free at the dispatch (the callback's return value replaces it), and the flush and the
 * C call keep everything but EAX/ECX/EDX, which the copied bytes reload; every supported
 * game's dispatch has that shape. */
static void __cdecl __attribute__((force_align_arg_pointer)) dim_at_callback(void);
static void __cdecl __attribute__((force_align_arg_pointer)) dim_vm_enter(uint32_t caller);
static uint32_t __cdecl __attribute__((force_align_arg_pointer)) dim_vm_exit(void);
static volatile const uint8_t* g_vm;       /* the sprite VM being drawn */
static volatile uint32_t g_vm_return;      /* where the exit stub sends the VM draw's caller */
static void (*g_dim_flush)(void);          /* the batch flush, callable from C (keeps the callee-saved registers) */
static uint32_t g_vm_callers[32]; static int g_vm_depth;   /* the wrapped VM draws in progress (children re-enter it) */
static void dim_vm_trace(const char* anm, int layer, int script);
static int dim_classify(const char* anm, int layer, int script);
static void dim_install(void) {
    const uintptr_t at = g_game->draw.dispatch; const size_t n = g_game->draw.dispatch_len;
    const uint8_t node = g_game->draw.node_reg, freg = g_game->draw.flush_reg;
    if (!at) return;
    if (n < 5 || n > 16 || node == 4 || freg == 4 || !g_game->draw.flush_fn || !g_game->draw.flush_this) { LOG("dimming: the profile's draw description is unusable"); return; }
    if (!site_expected(at, n)) return;
    uint8_t* flush = g_p;
    E(0x50 | freg); E(0x8B, 0x05 | (freg << 3)); E32((uint32_t)g_game->draw.flush_this);   /* push reg; mov reg,[flush_this] */
    ECALL(g_game->draw.flush_fn); E(0x58 | freg); E(0xC3);                                      /* call flush_fn; pop reg; ret */
    STUB_BEGIN();
    E(0x8B, 0x80 | node); E32(g_game->draw.prio_off);                                          /* mov eax,[node+prio_off] */
    E(0xA3); E32((uint32_t)(uintptr_t)&g_draw_prio);                                            /* mov [g_draw_prio],eax */
    E(0x89, 0x05 | (node << 3)); E32((uint32_t)(uintptr_t)&g_draw_node);                        /* mov [g_draw_node],node */
    ECALL((uintptr_t)flush);
    ECALL((uintptr_t)dim_at_callback);                                                        /* the background quad, when this is the first world callback */
    ECOPY(at, n);
    E(0x50); ECALL((uintptr_t)flush); E(0x58);                                                  /* push eax; call flush; pop eax */
    E(0xC7, 0x05); E32((uint32_t)(uintptr_t)&g_draw_prio); E32(0xFFFFFFFFu);                  /* mov dword [g_draw_prio],-1 */
    E(0xC7, 0x05); E32((uint32_t)(uintptr_t)&g_batch_class); E32((uint32_t)DIM_NONE);         /* mov dword [g_batch_class],DIM_NONE */
    EJMP(at + n);
    stub_end();
    site_hook(at, n);
    /* The VM draw is wrapped at both ends. Entry: record the VM, hand C the caller's return
       address, replace it on the stack with the exit stub, carry the prologue. Exit (the function
       returns to the exit stub, its own stack argument already popped): C flushes a classified
       VM's quads as their own draw call and gives back the caller's address. Everything but the
       flags is preserved for the function and its caller. */
    const uintptr_t vd = g_game->draw.vm_draw; const size_t vn = g_game->draw.vm_draw_len; const uint8_t vreg = g_game->draw.vm_reg;
    const int vstack = g_game->draw.vm_stack_arg;
    if (vd && vn >= 5 && vn <= 16 && (vstack || vreg != 4) && site_expected(vd, vn)) {
        uint8_t* exit = g_p;
        E(0x50, 0x51, 0x52);                                                                     /* push eax; push ecx; push edx */
        ECALL((uintptr_t)dim_vm_exit);                                                           /* eax = the caller's address */
        E(0xA3); E32((uint32_t)(uintptr_t)&g_vm_return);                                         /* mov [g_vm_return],eax */
        E(0x5A, 0x59, 0x58);                                                                     /* pop edx; pop ecx; pop eax */
        E(0xFF, 0x25); E32((uint32_t)(uintptr_t)&g_vm_return);                                   /* jmp [g_vm_return] */
        STUB_BEGIN();
        if (vstack) {
            /* The VM is the first stack argument, so it is at [esp+4] on entry. EAX is the only
               scratch here and the function is about to want it, so it is borrowed and given
               back; `mov` sets no flags, which is the one thing this stub may not disturb. */
            E(0x50);                                                                             /* push eax */
            E(0x8B, 0x44, 0x24, 0x08);                                                           /* mov eax,[esp+8] */
            E(0xA3); E32((uint32_t)(uintptr_t)&g_vm);                                            /* mov [g_vm],eax */
            E(0x58);                                                                             /* pop eax */
        } else {
            E(0x89, 0x05 | (vreg << 3)); E32((uint32_t)(uintptr_t)&g_vm);                       /* mov [g_vm],reg */
        }
        E(0x50, 0x51, 0x52);                                                                     /* push eax; push ecx; push edx */
        E(0xFF, 0x74, 0x24, 0x0C);                                                               /* push [esp+12]: the caller's address */
        ECALL((uintptr_t)dim_vm_enter); E(0x83, 0xC4, 0x04);                                     /* call; add esp,4 */
        E(0x5A, 0x59, 0x58);                                                                     /* pop edx; pop ecx; pop eax */
        E(0xC7, 0x04, 0x24); E32((uint32_t)(uintptr_t)exit);                                     /* mov dword [esp],exit */
        ECOPY(vd, vn); EJMP(vd + vn);
        stub_end();
        site_hook(vd, vn);
        g_dim_flush = (void (*)(void))flush;
    }
    g_dim_available = 1;
    LOG("dimming: draws attributed at the runner @%08x (world from priority %d, %u rules%s)",
        (unsigned)at, g_game->draw.world_prio, (unsigned)g_game->draw.rule_count, vd ? ", per sprite VM" : "");
}
/* Debug (debug=1, traced frames): every VM's ANM and layer, and the first VMs' raw words -- how
   the profile's vm_anm_off/vm_layer_off and the rules were found. */
static void dim_vm_trace(const char* anm, int layer, int script) {
    if (g_dim_trace_frames <= 0 || !cfg.debug) return;
    if (g_vm_since_n < (int)sizeof g_vm_since - 40) g_vm_since_n += snprintf(g_vm_since + g_vm_since_n, sizeof g_vm_since - g_vm_since_n, " %s:%d/%d", anm ? anm : "?", layer, script);
    static int per_prio, last_prio = -2; if (g_draw_prio != last_prio) { last_prio = g_draw_prio; per_prio = 0; }
    if (per_prio++ < 3 && cfg.debug < 2) {   /* debug=2 traces often and skips the word dumps */
        const uint32_t* w = (const uint32_t*)g_vm; char line[3000]; int n = 0;
        for (int i = 0; i < 300; ++i) n += snprintf(line + n, sizeof line - n, " %08x", (unsigned)w[i]);
        LOG("draw      vm words:%s", line);
        /* which of its words points at a loaded ANM (slot index, then the file name)? */
        for (int i = 0; i < 300; ++i) {
            const uint8_t* al = (const uint8_t*)(uintptr_t)w[i];
            if ((uintptr_t)al < 0x10000 || IsBadReadPtr(al, 64)) continue;
            const char* nm = (const char*)al + 4; int ok = 0;
            for (int k = 0; k < 28; ++k) { if (nm[k] == 0) { ok = k > 4 && nm[k-4] == '.' && nm[k-3] == 'a' && nm[k-2] == 'n' && nm[k-1] == 'm'; break; } if (nm[k] < 32 || nm[k] > 126) break; }
            if (ok) LOG("draw      vm anm pointer at +0x%x: slot %u %s", i * 4, (unsigned)*(const uint32_t*)al, nm);
        }
    }
}
static int dim_in_game(void) {
    return !g_game->addr.enemy_manager || *(void**)g_game->addr.enemy_manager != NULL;
}
/* 256 = leave the draw alone; otherwise the multiplier (0..255) for the current draw, of its
   alpha for most classes, of its colour for the background class (a spell background drawn as
   a sprite fades towards black like the rest of the background, not towards transparent). */
static int dim_draw_fade(void) {
    int cls = g_batch_class;
    if (!g_dim_available || g_dim_drawing || cls < 0 || cls >= DIM_COUNT || cfg.dim[cls] <= 0) return 256;
    int pct = cfg.dim[cls] > 100 ? 100 : cfg.dim[cls];
    return (100 - pct) * 255 / 100;
}
static int dim_fade_is_colour(int additive) { return additive || g_batch_class == DIM_BACKGROUND; }
/* The 3D-mode sprites are drawn from a vertex buffer with their colour in TEXTUREFACTOR; fade that. */
static DWORD dim_fade_factor(DWORD factor, int fade, int additive) {
    DWORD a = ((factor >> 24) * (DWORD)fade) >> 8;
    if (dim_fade_is_colour(additive)) {
        DWORD r = (((factor >> 16) & 0xFF) * (DWORD)fade) >> 8, g = (((factor >> 8) & 0xFF) * (DWORD)fade) >> 8, b = ((factor & 0xFF) * (DWORD)fade) >> 8;
        return (g_batch_class == DIM_BACKGROUND ? (factor & 0xFF000000) : (a << 24)) | (r << 16) | (g << 8) | b;
    }
    return (a << 24) | (factor & 0x00FFFFFF);
}
/* --- classification: g_batch_class is the class of whatever is in the sprite batch / being drawn */
static int dim_glob(const char* pat, const char* name) {   /* "pl*.anm": one '*' matching anything */
    const char* star = strchr(pat, '*');
    if (!star) return strcmp(pat, name) == 0;
    size_t pre = (size_t)(star - pat), suf = strlen(star + 1), n = strlen(name);
    return n >= pre + suf && memcmp(pat, name, pre) == 0 && memcmp(star + 1, name + n - suf, suf) == 0;
}
/* Every (priority, ANM, layer) that has been drawn, and how the rules classify it. The traced
   frames are one in every ten seconds, so anything short-lived -- an item, a bomb, a death --
   can be on screen constantly and still never appear in one. This sees every VM of every frame
   instead, which is what it takes to answer "and what draws the items?". Debug only, reported
   once and again whenever something new shows up, like the update-node census. */
enum { DIM_CENSUS = 128, DIM_CENSUS_NAME = 24 };
/* The name is copied, not pointed at. The first version of this kept the `const char*` the VM
   gave it and compared pointers: an ANM record is freed and its memory reused, so by the time
   the line printed the name was whatever had been written there since, every reallocation of
   the same file became another row, and the table filled with rubbish before the thing being
   looked for ever reached it. Which is how it managed to hide the very answer it was added to
   find. */
static struct { int prio, layer, cls; char anm[DIM_CENSUS_NAME]; unsigned long long n; } g_dim_census[DIM_CENSUS];
static int g_dim_census_n, g_dim_census_reported;
/* A name is only worth recording if it still looks like one when it is read. */
static void dim_census_name(char* out, const char* anm) {
    int i = 0;
    if (anm) for (; i < DIM_CENSUS_NAME - 1; ++i) {
        char c = anm[i];
        if (!c) break;
        if (c < 32 || c > 126) { i = 0; break; }
        out[i] = c;
    }
    if (!i) { out[0] = '?'; i = 1; }
    out[i] = 0;
}
/* A callback that draws without any VM going through the sprite draw is drawing its quads
   itself -- which is what TH13's items do, and the reason its item rule matches on priority
   with a NULL ANM. The VM census cannot see those at all, so they are counted here: how many
   draw calls a callback made, and whether any VM was behind them. Without this, "what draws
   the items?" has no answer in the log however long the game is played, which is exactly what
   two stages' worth of looking established. */
static unsigned g_cb_draws_at_entry, g_cb_vms_at_entry; static int g_cb_prio = -1;
static void dim_census(int prio, const char* anm, int layer, int cls);
static void dim_census_callback_end(void) {
    if (!cfg.debug || g_cb_prio < 0) return;
    /* The per-frame counters are reset between frames, so a callback whose end is only noticed
       after that reset would compare against a larger number and look like it drew. */
    if (g_frame_draws > g_cb_draws_at_entry && g_frame_vms == g_cb_vms_at_entry)
        dim_census(g_cb_prio, "(no VM: its own quads)", -1, dim_classify(NULL, -1, -1));
}
static void dim_census(int prio, const char* anm, int layer, int cls) {
    if (!cfg.debug) return;
    char name[DIM_CENSUS_NAME]; dim_census_name(name, anm);
    for (int i = 0; i < g_dim_census_n; ++i)
        if (g_dim_census[i].prio == prio && g_dim_census[i].layer == layer &&
            !strcmp(g_dim_census[i].anm, name)) { ++g_dim_census[i].n; return; }
    if (g_dim_census_n >= DIM_CENSUS) return;
    g_dim_census[g_dim_census_n].prio = prio; g_dim_census[g_dim_census_n].layer = layer;
    memcpy(g_dim_census[g_dim_census_n].anm, name, sizeof name);
    g_dim_census[g_dim_census_n].cls = cls;
    g_dim_census[g_dim_census_n].n = 1;
    ++g_dim_census_n; g_dim_census_reported = 0;
}
static void dim_census_report(void) {
    if (!cfg.debug || g_dim_census_reported || !g_dim_census_n) return;
    g_dim_census_reported = 1;
    for (int i = 0; i < g_dim_census_n; ++i) {
        int c = g_dim_census[i].cls;
        LOG("draw census: priority %d, %s layer %d, %llu draws -- %s", g_dim_census[i].prio,
            g_dim_census[i].anm, g_dim_census[i].layer,
            g_dim_census[i].n, c >= 0 && c < DIM_COUNT ? DIM_NAMES[c] : "not faded");
    }
}
/* The class of a VM (anm name and layer given) or of a non-VM draw (anm NULL) under the running callback. */
static int dim_classify(const char* anm, int layer, int script) {
    for (size_t i = 0; i < g_game->draw.rule_count; ++i) {
        const struct DimRule* r = &g_game->draw.rules[i];
        if (r->prio_lo >= 0 && g_draw_prio < r->prio_lo) continue;
        if (r->prio_hi >= 0 && g_draw_prio > r->prio_hi) continue;
        if (r->anm) { if (!anm || !dim_glob(r->anm, anm)) continue; }
        if (r->layer_lo >= 0 && (layer < r->layer_lo || layer > r->layer_hi)) continue;
        if (r->script_lo >= 0 && (script < r->script_lo || script > r->script_hi)) continue;
        return r->category;
    }
    return DIM_NONE;
}
/* Around one VM draw. A classified VM's quads become their own draw call: the batch is
   flushed before it if something else is pending, and after it always, so that quads from
   draw paths the wrap does not see (a manager building quads itself, a VM drawn through
   another entry) can never share a faded draw call. Unclassified VMs batch as the game likes. */
static void __cdecl __attribute__((force_align_arg_pointer)) dim_vm_enter(uint32_t caller) {
    if (g_vm_depth < 32) g_vm_callers[g_vm_depth] = caller;
    g_vm_depth++;
    if (!g_vm || g_draw_prio < 0) return;
    const char* anm = NULL; int layer = -1, script = -1;
    if (g_game->draw.vm_anm_off) { const uint8_t* al = *(const uint8_t* const*)(g_vm + g_game->draw.vm_anm_off); if (al) anm = (const char*)al + 4; }
    if (g_game->draw.vm_layer_off) layer = *(const int*)(g_vm + g_game->draw.vm_layer_off);
    if (g_game->draw.vm_script_off) script = *(const uint16_t*)(g_vm + g_game->draw.vm_script_off);
    dim_vm_trace(anm, layer, script); g_frame_vms++;
    int cls = dim_classify(anm, layer, script);
    dim_census(g_draw_prio, anm, layer, cls);
    if (cls == g_batch_class) return;
    g_dim_flush(); g_frame_flushes++;
    g_batch_class = cls;
}
static uint32_t __cdecl __attribute__((force_align_arg_pointer)) dim_vm_exit(void) {
    uint32_t caller = g_vm_depth > 0 && g_vm_depth <= 32 ? g_vm_callers[g_vm_depth - 1] : 0;
    if (g_vm_depth > 0) g_vm_depth--;
    if (g_vm_depth == 0 && g_batch_class != DIM_NONE) { g_dim_flush(); g_frame_flushes++; g_batch_class = DIM_NONE; }
    return caller;
}
/* Called by the wrap before every draw callback. At the first callback of the world, blend the
   black quad over what has been drawn: at that point the background is complete and the world
   has not started, whether the game draws both into the back buffer or, as TH11 on do, renders
   the stage offscreen and composites it (even several times, with TH13's trance effect) --
   dimming the source dims every use of it. */
static void __cdecl __attribute__((force_align_arg_pointer)) dim_at_callback(void) {
    if (g_dim_trace_frames > 0 && cfg.debug && g_draw_node)
        LOG("draw      callback prio %d fn %08x", g_draw_prio, (unsigned)*(const uint32_t*)((const uint8_t*)g_draw_node + 8));
    dim_census_callback_end();
    g_cb_prio = g_draw_prio; g_cb_draws_at_entry = g_frame_draws; g_cb_vms_at_entry = g_frame_vms;
    g_batch_class = g_draw_prio >= 0 ? dim_classify(NULL, -1, -1) : DIM_NONE;   /* the batch was just flushed */
    if (cfg.dim[DIM_BACKGROUND] <= 0 || !g_dim_available || g_dim_frame_done || !g_dev) return;
    if (g_draw_prio < g_game->draw.world_prio || !dim_in_game()) return;
    IDirect3DDevice9* dev = g_dev;
    g_dim_frame_done = 1;
    /* Over the current viewport: the whole target when the game has not restricted it (the
       offscreen stage of TH11 on), the playfield when it has (TH10 draws straight into the back
       buffer, with the interface painted around it afterwards). */
    D3DVIEWPORT9 vp; if (FAILED(dev->lpVtbl->GetViewport(dev, &vp))) return;
    int pct = cfg.dim[DIM_BACKGROUND] > 100 ? 100 : cfg.dim[DIM_BACKGROUND];
    DWORD colour = (DWORD)(pct * 255 / 100) << 24;
    float x0 = (float)vp.X - 0.5f, y0 = (float)vp.Y - 0.5f, x1 = (float)(vp.X + vp.Width) - 0.5f, y1 = (float)(vp.Y + vp.Height) - 0.5f;
    struct { float x, y, z, rhw; DWORD c; } v[4] = {
        { x0, y0, 0.0f, 1.0f, colour }, { x1, y0, 0.0f, 1.0f, colour }, { x0, y1, 0.0f, 1.0f, colour }, { x1, y1, 0.0f, 1.0f, colour } };
    g_dim_drawing = 1;
    if (g_dim_state) g_dim_state->lpVtbl->Capture(g_dim_state);
    dev->lpVtbl->SetVertexShader(dev, NULL); dev->lpVtbl->SetPixelShader(dev, NULL);
    dev->lpVtbl->SetTexture(dev, 0, NULL);
    dev->lpVtbl->SetFVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    dev->lpVtbl->SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);
    dev->lpVtbl->SetRenderState(dev, D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    dev->lpVtbl->SetRenderState(dev, D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->lpVtbl->SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->lpVtbl->SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->lpVtbl->SetRenderState(dev, D3DRS_ALPHATESTENABLE, FALSE);
    dev->lpVtbl->SetRenderState(dev, D3DRS_ZENABLE, FALSE);
    dev->lpVtbl->SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);
    dev->lpVtbl->SetRenderState(dev, D3DRS_STENCILENABLE, FALSE);
    dev->lpVtbl->SetRenderState(dev, D3DRS_FOGENABLE, FALSE);
    dev->lpVtbl->SetRenderState(dev, D3DRS_LIGHTING, FALSE);
    dev->lpVtbl->SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    dev->lpVtbl->SetRenderState(dev, D3DRS_SCISSORTESTENABLE, FALSE);
    dev->lpVtbl->SetRenderState(dev, D3DRS_COLORWRITEENABLE, 0xF);
    dev->lpVtbl->SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->lpVtbl->SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->lpVtbl->SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->lpVtbl->SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dev->lpVtbl->SetTextureStageState(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->lpVtbl->SetTextureStageState(dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->lpVtbl->DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, v, sizeof v[0]);
    if (g_dim_state) g_dim_state->lpVtbl->Apply(g_dim_state);
    g_dim_drawing = 0;
}
/* Scale the vertex colours of an item draw in place (the copy the caller made). `additive` when the
   destination blend is ONE, where alpha alone would change nothing. */
static void dim_fade_vertices(uint8_t* verts, UINT count, UINT stride, DWORD fvf, int fade, int additive) {
    if (!(fvf & D3DFVF_DIFFUSE)) return;
    UINT off = 16 + ((fvf & D3DFVF_PSIZE) ? 4 : 0);
    if (off + 4 > stride) return;
    for (UINT i = 0; i < count; ++i) { DWORD* c = (DWORD*)(verts + i * stride + off); *c = dim_fade_factor(*c, fade, additive); }
}
static void dim_release(void) { SAFE_RELEASE(g_dim_state); }
static void dim_init(IDirect3DDevice9* dev) {
    dim_release();
    if (!g_dim_available) return;
    if (FAILED(dev->lpVtbl->CreateStateBlock(dev, D3DSBT_ALL, &g_dim_state))) { g_dim_state = NULL; LOG("dimming: no state block; the game's state may be disturbed"); }
}
