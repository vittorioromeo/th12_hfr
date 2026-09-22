/* TH18 v1.00a: native 60 Hz gameplay, interpolated sprite geometry.
 * See docs/games/TH18_DEVNOTES.md for the audited frame paths and remaining work. */
#include "../backends/fixed_quad.h"

static uint64_t th18_tick;
static double th18_phase;
static int th18_present_major;
#define TH18_HISTORY_COUNT 16384
static struct FixedQuadHistory* th18_history;
static unsigned th18_quads, th18_smoothed;

/* Camera inputs only: eye, up, direction, eye offset, FOV. Matrices and viewport
 * caches remain native. Both stage passes copy these inputs into the supervisor,
 * including the inline camera setup inside 41e350. */
static const unsigned th18_camera_offsets[] = {
    0x230,0x234,0x238, 0x248,0x24c,0x250, 0x254,0x258,0x25c,
    0x26c,0x270,0x274, 0x284
};
struct Th18CameraHistory {
    uintptr_t key, script; uint64_t tick; int age, valid;
    float previous[13], current[13];
};
static struct Th18CameraHistory th18_camera;
static int th18_in_stage_draw;
static int th18_camera_finite(const float* p) {
    for (int i=0;i<13;++i) if (!isfinite(p[i])) return 0;
    float ux=p[3],uy=p[4],uz=p[5],dx=p[6],dy=p[7],dz=p[8];
    float x=uy*dz-uz*dy,y=uz*dx-ux*dz,z=ux*dy-uy*dx;
    return x*x+y*y+z*z>1e-6f && p[12]>0.001f && p[12]<3.14f;
}
static int th18_camera_pose(struct Th18CameraHistory* h, uintptr_t key, uintptr_t script,
                           uint64_t tick, int age, const float* p, double alpha, float* out) {
    if (!th18_camera_finite(p)) { h->valid=0; h->key=0; return 0; }
    if (h->key!=key || h->script!=script || (tick!=h->tick+1 && tick!=h->tick) || age<h->age) {
        h->valid=0; memcpy(h->previous,p,sizeof h->previous); memcpy(h->current,p,sizeof h->current);
    } else if (tick!=h->tick) {
        float distance=0,updot=0,dirdot=0;
        for (int i=0;i<3;++i) {
            float d=(p[i]+p[i+9])-(h->current[i]+h->current[i+9]); distance+=d*d;
            updot+=p[i+3]*h->current[i+3]; dirdot+=p[i+6]*h->current[i+6];
        }
        h->valid=distance<512.0f*512.0f && updot>0 && dirdot>0 && fabsf(p[12]-h->current[12])<0.5f;
        memcpy(h->previous,h->current,sizeof h->previous); memcpy(h->current,p,sizeof h->current);
    } else if (memcmp(h->current,p,sizeof h->current)) {
        h->valid=0; memcpy(h->current,p,sizeof h->current);
    }
    h->key=key;h->script=script;h->tick=tick;h->age=age;
    if (!h->valid) return 0;
    float a=(float)fmax(0.0,fmin(1.0,alpha));
    for (int i=0;i<13;++i) out[i]=h->previous[i]+(h->current[i]-h->previous[i])*a;
    return th18_camera_finite(out);
}
static int th18_stage_draw(uint8_t* stage, uintptr_t native) {
    float saved[13],out[13];
    for (int i=0;i<13;++i) memcpy(&saved[i],stage+th18_camera_offsets[i],4);
    int smooth=th18_camera_pose(&th18_camera,(uintptr_t)stage,*(uintptr_t*)(stage+0x3454),
                               th18_tick,*(int*)(stage+0x3490),saved,th18_phase,out);
    int applied=smooth && cfg.enemy_interp && g_refresh>60;
    if (applied)
        for (int i=0;i<13;++i) memcpy(stage+th18_camera_offsets[i],&out[i],4);
    ++th18_in_stage_draw;
    int r=call_this0(native,stage);
    --th18_in_stage_draw;
    if (applied) {
        /* An inactive pass can return without selecting the stage camera. Do not
         * overwrite another context's inputs in that case. */
        int copied=1;
        for (int i=0;i<13;++i)
            if (memcmp((void*)(0x4cd478+th18_camera_offsets[i]-0x230),&out[i],4)) copied=0;
        for (int i=0;i<13;++i) {
            memcpy(stage+th18_camera_offsets[i],&saved[i],4);
            if (copied) memcpy((void*)(0x4cd478+th18_camera_offsets[i]-0x230),&saved[i],4);
        }
    }
    return r;
}
static int __attribute__((fastcall, force_align_arg_pointer)) th18_stage_first(uint8_t* stage) {
    return th18_stage_draw(stage,0x41c290);
}
static int __attribute__((fastcall, force_align_arg_pointer)) th18_stage_second(uint8_t* stage) {
    return th18_stage_draw(stage,0x41c700);
}
/* Native timer decrement consumes an unused stack argument (RET 4). */
static void __attribute__((fastcall, force_align_arg_pointer)) th18_stage_timer(void* timer, void* unused, void* arg) {
    (void)unused;
    if (th18_present_major) call_this1(0x409750,timer,arg);
}

/* Keep the native runner, including its ending instruction (thprac hooks it). The
 * frame scheduler calls this once per presentation; only an owed tick runs logic. */
static int __attribute__((fastcall, force_align_arg_pointer)) th18_update(void* runner) {
    th18_present_major = !g_skip_update;
    if (g_skip_update) return 1;
    ++th18_tick;
    return call_this0(0x4012e0, runner);
}
static int th18_update_only(void) {
    /* The normal frame selects viewport/context 2 before calling the runner. */
    call_this1(0x41b330, (void*)0x4ccdf0, (void*)2);
    ++th18_tick;
    int r = call_this0(0x4012e0, *(void**)0x4cf294);
    if (r == 0 || r == -1) {
        call_this0(0x402b30, (void*)0x4cd884);
        return r == 0 ? 1 : 2;
    }
    return 0;
}
static int th18_frame_original(void* window) {
    th18_phase = (double)g_lacc / (double)(g_refresh > 0 ? g_refresh : 60);
    /* A skipped native draw would also skip Present, defeating the shared clock. This
     * override is local to the frame; the user's saved frameskip setting is retained. */
    uint8_t skip = *(uint8_t*)0x4cd00c;
    *(uint8_t*)0x4cd00c = 0;
    int r = call_this0(0x472fd0, window);
    *(uint8_t*)0x4cd00c = skip;
    if (cfg.debug) {
        static double last;
        double now = now_s();
        if (now - last >= 5.0) {
            LOG("TH18: native ticks=%llu; 2D quads=%u smoothed=%u; logic=%d Hz present=%d Hz",
                (unsigned long long)th18_tick, th18_quads, th18_smoothed, g_logic_rate, g_refresh);
            last = now; th18_quads = th18_smoothed = 0;
        }
    }
    return r;
}
/* This routine also accounts slowdown/replay timing; count native frames, not presents. */
static void __cdecl __attribute__((force_align_arg_pointer)) th18_after_present(void) {
    if (th18_present_major) ((void (__cdecl *)(void))0x4728a0)();
}
static void __stdcall __attribute__((force_align_arg_pointer)) th18_snapshot(const char* path) {
    if (!th18_present_major) return;
    g_in_screenshot = 1;
    ((void (__stdcall *)(const char*))0x453f40)(path);
    g_in_screenshot = 0;
}

/* The native batch builder has already calculated/clipped these vertices and stored
 * the native corners in VM+0x4f0. Change only the temporary copy sent to the GPU, then
 * restore it. This includes embedded bullet/player VMs as well as manager-owned VMs. */
static int __attribute__((fastcall, force_align_arg_pointer)) th18_quad(void* manager, void* unused, uint8_t* vm, uint8_t* vertices) {
    (void)unused;
    float saved[4][3], out[4][3];
    for (unsigned c = 0; c < 4; ++c) memcpy(saved[c], vertices + c * 28, 12);
    ++th18_quads;
    if (!th18_in_stage_draw && th18_history && cfg.enemy_interp && g_refresh > 60) {
        uintptr_t key = (uintptr_t)vm;
        unsigned first = (unsigned)((key >> 2) * 2654435761u) & (TH18_HISTORY_COUNT - 1);
        struct FixedQuadHistory* h = NULL;
        for (unsigned i = 0; i < 16; ++i) {
            struct FixedQuadHistory* candidate = &th18_history[(first + i) & (TH18_HISTORY_COUNT - 1)];
            if (candidate->key == key || !candidate->key || candidate->tick + 2 < th18_tick) { h = candidate; break; }
        }
        /* Script file slot + script number, not the moving instruction pointer. */
        uintptr_t script = (*(uint32_t*)(vm + 0x1c) << 16) ^ *(uint32_t*)(vm + 0x28);
        int predict = cfg.predict && g_draw_prio >= 19 && g_draw_prio <= 41;
        /* Stop prediction immediately while paused or while the ability shop is open. */
        uint8_t* game = *(uint8_t**)0x4cf2e4;
        double alpha = th18_phase;
        if (predict && ((!game) || (*(uint32_t*)(game + 0xb0) & 5) || *(void**)0x4cf2a4)) alpha = 0;
        if (h && fixed_quad_pose(h, key, script, th18_tick, *(int*)(vm + 0x550), saved, alpha, predict, out)) {
            for (unsigned c = 0; c < 4; ++c) memcpy(vertices + c * 28, out[c], 12);
            ++th18_smoothed;
        }
    }
    int r = call_this1(0x47e800, manager, vertices);
    for (unsigned c = 0; c < 4; ++c) memcpy(vertices + c * 28, saved[c], 12);
    return r;
}
static void th18_install_presentation(void) {
    cfg.enemy_interp = GetPrivateProfileIntA("fixed60", "interpolate", 1, g_ini_path) != 0;
    cfg.predict = GetPrivateProfileIntA("fixed60", "predict", 0, g_ini_path) != 0;
    cfg.substep = cfg.subtick_input = cfg.fixed_substep = 0;
    th18_history = calloc(TH18_HISTORY_COUNT, sizeof *th18_history);
    if (!th18_history) LOG("TH18: quad history allocation failed; interpolation unavailable");
    /* The automatic-latency path is inlined into the window loop. Route it to the
     * same selection as the other two settings, then redirect both selected calls. */
    g_p = stub_begin();
    STUB_BEGIN(); EJMP(0x471c37); site_hook(0x471a9e, 6);
    site_call(0x471c4e, hfr_frame_ecx);
    site_call(0x471c5a, hfr_frame_ecx);
    site_call(0x472ff0, th18_update);
    site_call(0x473185, th18_after_present);
    site_call(0x473367, th18_snapshot);
    uint8_t latency[7]; memcpy(latency, site_expected(0x4730be, 7), 7); latency[6] = 0x7f;
    patch_bytes(0x4730be, latency, 7, site_expected(0x4730be, 7));
    /* ECX=manager, EDI=VM, [esp+4]=vertices. The C fastcall takes two additional
     * stack arguments and pops them; ret 4 consumes the original native argument. */
    uint8_t* quad = g_p;
    E(0xff,0x74,0x24,0x04, 0x57);
    ECALL((uintptr_t)th18_quad); E(0xc2,0x04,0x00); stub_end();
    site_call(0x47e6b5, quad);
    patch_jmp(0x41ca70,th18_stage_first,site_expected(0x41ca70,5));
    site_call(0x41ca81,th18_stage_second);
    site_call(0x41c99c,th18_stage_timer);
    g_frame_hook_installed = 1;
    LOG("TH18 experimental: native 60 Hz gameplay; sprite and stage-camera interpolation=%d prediction=%d; sub-tick gameplay unavailable", cfg.enemy_interp, cfg.predict);
}
static const struct DimRule th18_dim_rules[] = {
    { 19, 19, NULL,         -1, -1, -1, -1, DIM_ITEMS },
    { 33, 33, NULL,         -1, -1, -1, -1, DIM_ITEMS },
    { 38, 38, "bullet.anm", -1, -1, -1, -1, DIM_NONE },
};
static const struct GameProfile th18_profile = {
    .identity = &game_identities[GI_TH18],
    .addr = { .device = 0x4ccdf8, .pp = 0x4ccee4, .enemy_manager = 0x4cf2d0,
              .anm_manager = 0x51f65c },
    .install_presentation = th18_install_presentation,
    .frame_original = th18_frame_original, .update_only = th18_update_only,
    .draw = { .dispatch = 0x401490, .dispatch_len = 8, .node_reg = R_EDI,
              .flush_fn = 0x47e730, .flush_reg = R_ECX, .flush_this = 0x51f65c,
              .vm_draw = 0x481210, .vm_draw_len = 9, .vm_stack_arg = 1,
              .vm_layer_off = 0x18, .vm_slot_off = 0x20,
              .anm_table_off = 0x312072c, .anm_slots = 33,
              .world_prio = 19, .rules = th18_dim_rules,
              .rule_count = sizeof th18_dim_rules / sizeof *th18_dim_rules },
    .d3dx = "d3dx9_43.dll"
};
