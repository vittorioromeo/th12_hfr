/* TH08 1.00d: D3D8, native 60 Hz Chain ABI, interpolated 2D drawing.
 * See docs/games/TH08_DEVNOTES.md. Nothing in gameplay is sub-stepped by default; [fixed60]
 * subtick and substep slice the player and the projectiles, and are off unless asked for. */
#include "../backends/fixed_clock.h"
#include "../backends/fixed_history.h"

typedef int (__attribute__((thiscall)) *Th08ThisFn)(void*);
typedef int (__attribute__((thiscall)) *Th08AddQuadFn)(void*, void*);
static Th08ThisFn th08_render_original;
static double th08_phase;
static int th08_lead_on, th08_predict_on;
static int th08_any_sub(void);
static int th08_projectiles_sub(void);
static int th08_projectile_vm(const uint8_t* vm);
static uint64_t th08_tick;
static int th08_major, th08_guard_failed;
static unsigned th08_quads, th08_smoothed;
/* One history per sprite VM: the quad it drew on the last two frame ticks. */
struct Th08QuadHistory {
    uintptr_t key, script; uint64_t tick, shared_tick; int age, valid;
    float previous[4][3], current[4][3];
};
#define TH08_HISTORY_COUNT 8192
static struct Th08QuadHistory* th08_history;

/* ---- the Chain. TH06-08 keep two priority-ordered lists of callbacks, one that updates and
 * one that draws; an element is {i16 priority, u16 flags, callback, added, deleted, prev, next,
 * unk, arg}, callbacks are fastcall(arg) and answer with what the walk should do next. This is
 * the same walk as Chain::RunCalcChain (0x43ca50) and RunDrawChain (0x43cb60), lock for lock,
 * with two additions: a minor tick only calls the systems classified MODE_SUB, with the
 * engine's own frame-rate multiplier (Supervisor+0x188, the ancestor of TH10's game speed) set
 * to the tick's length; and the draw walk records whose callback is running, because a quad
 * drawn by a system that really moves between frames must not be interpolated as well. */
struct Th08Elem {
    int16_t priority; uint16_t flags;
    int (__attribute__((fastcall)) *callback)(void*);
    void* added; void* deleted;
    struct Th08Elem* prev; struct Th08Elem* next; struct Th08Elem* unk; void* arg;
};
_Static_assert(sizeof(struct Th08Elem) == 0x20, "TH08 ChainElem layout");
#define TH08_SUPERVISOR ((void*)0x17ce758)
#define TH08_CHAIN      ((struct Th08Elem*)0x164f548)
typedef void (__attribute__((thiscall)) *Th08LockFn)(void*, int);
typedef void (__attribute__((thiscall)) *Th08CutFn)(void*, struct Th08Elem*);
static inline void th08_lock(void)   { ((Th08LockFn)0x43ef70)(TH08_SUPERVISOR, 0); }
static inline void th08_unlock(void) { ((Th08LockFn)0x43efb0)(TH08_SUPERVISOR, 0); }
static struct Th08Elem* th08_stop_elem;   /* the element that answered "break" on the last major tick */
static uint32_t th08_draw_callback;       /* the draw callback running now, 0 outside the draw walk */

/* ---- the player, moved at the display's rate.
 * Player::OnUpdate (0x44c390) is a frame's worth of many things -- the focus state machine, the
 * shot timers, the human/youkai gauge, the options' position history -- and exactly one of
 * them is a rate: HandlePlayerInputs integrates `position += speed * multiplier` and clamps to
 * the playfield. So the callback stays a 60 Hz callback, and only that integration is sliced:
 * the two `fmul [multiplier]` operands at 0x44ba6a/0x44ba7c are pointed at th08_move_factor,
 * which is the game's own multiplier times this tick's length, and on the ticks between frame
 * boundaries the same integration is repeated here from the same fields. A frame's slices sum
 * to the frame's movement, so the position at every frame boundary is the stock game's. */
#define TH08_PLAYER   ((uint8_t*)0x17d5ef8)
#define TH08_INPUT    (*(const uint16_t*)0x164d52c)
#define TH08_MULTIPLIER (*(float*)0x17ce8e0)
float th08_move_factor = 1.0f;
static uint64_t th08_player_tick, th08_world_tick, th08_replay_tick;   /* the last frame tick each of these ran on */
static float th08_player_major[3];        /* where the last frame-boundary tick left the player */
/* Not while a replay plays: the file holds one input word per 60 Hz frame and the position each
   one led to, and slicing the movement puts her somewhere else when the bullets are tested. */
static int th08_player_sub(void) {
    return cfg.subtick_input && cfg.substep && g_logic_rate != 60 && node_mode(0x44c390) == MODE_SUB && !(th08_replay_tick && th08_replay_tick + 1 >= th08_tick);
}
/* The velocity HandlePlayerInputs would choose for this input word, in pixels per frame. The
   direction is a priority chain, not a sum: the four diagonals first, then down, up, left,
   right. `focused` picks the speed table the way the game does. */
static int th08_player_velocity(unsigned in, int focused, float v[2]) {
    const uint8_t* p = TH08_PLAYER;
    int dx, dy;
    if ((in & 0x50) == 0x50) { dx = -1; dy = -1; } else if ((in & 0x60) == 0x60) { dx = -1; dy = 1; }
    else if ((in & 0x90) == 0x90) { dx = 1; dy = -1; } else if ((in & 0xa0) == 0xa0) { dx = 1; dy = 1; }
    else if (in & 0x20) { dx = 0; dy = 1; } else if (in & 0x10) { dx = 0; dy = -1; }
    else if (in & 0x40) { dx = -1; dy = 0; } else if (in & 0x80) { dx = 1; dy = 0; }
    else { dx = dy = 0; }
    const uint8_t* table = *(const uint8_t* const*)(p + (focused ? 0xe2a78 : 0xe2a74));
    if (!table) return 0;
    float straight = *(const float*)(table + (focused ? 0x28 : 0x24));
    float diagonal = *(const float*)(table + (focused ? 0x30 : 0x2c));
    float speed = dx && dy ? diagonal : straight;
    v[0] = (float)dx * speed * *(const float*)(p + 0x404);
    v[1] = (float)dy * speed * *(const float*)(p + 0x408);
    return 1;
}
static void th08_player_clamp(float pos[2]) {
    const float* bound = (const float*)0x164d2ec;          /* min x, min y, width, height */
    if (pos[0] < bound[0]) pos[0] = bound[0]; else if (pos[0] > bound[0] + bound[2]) pos[0] = bound[0] + bound[2];
    if (pos[1] < bound[1]) pos[1] = bound[1]; else if (pos[1] > bound[1] + bound[3]) pos[1] = bound[1] + bound[3];
}
static int th08_player_movable(void) {
    const uint8_t* p = TH08_PLAYER;
    if (*(const char*)0x160f534) return 0;                /* the callback's own early-out */
    return *p != 1 && *p != 2;                            /* dying or re-entering: the game is moving her */
}
/* What the player's input is right now: the replay's word while one is playing (it is the only
   input there is), otherwise a fresh poll through the game's own Controller::GetInput, which
   reads the keyboard and the pad, applies the key configuration and latches nothing. */
static unsigned th08_live_input(void) {
    if (th08_replay_tick == th08_tick) return TH08_INPUT;
    return ((uint16_t (__cdecl *)(void))0x43d970)();
}
static unsigned th08_player_minor_calls;
static void th08_player_minor(void) {
    uint8_t* p = TH08_PLAYER;
    if (!th08_player_movable()) return;
    unsigned in = th08_live_input();
    float v[2];
    /* Focus keeps the state the frame tick gave it: it is a state machine with frame counters,
       and only the speed table depends on it here. */
    if (!th08_player_velocity(in, p[3] != 0, v)) return;
    float* pos = (float*)(p + 0x2b4);
    pos[0] += v[0] * th08_move_factor; pos[1] += v[1] * th08_move_factor;
    th08_player_clamp(pos);
    /* the three hit boxes: position -/+ each half-size */
    static const uint16_t box[3][3] = {{0x3d4,0x38c,0x398},{0x3e0,0x3a4,0x3b0},{0x3ec,0x3bc,0x3c8}};
    for (int b = 0; b < 3; ++b) for (int i = 0; i < 3; ++i) {
        float half = *(const float*)(p + box[b][0] + 4 * i);
        *(float*)(p + box[b][1] + 4 * i) = pos[i] - half;
        *(float*)(p + box[b][2] + 4 * i) = pos[i] + half;
    }
    ++th08_player_minor_calls;
}
/* ---- the stage background. It is 3D: its quads are placed in the world and the camera flies
 * through them, so there is nothing to smooth quad by quad -- the motion is all in the scroll
 * position (Background+0x824) and the camera block (+0x6394: six vectors and a field of
 * view). Both are captured on every frame tick and, around the two background draw callbacks
 * only, replaced by the value for this presentation and then put back. A cut -- the camera
 * jumping between two shots -- is shown as a cut. */
#define TH08_BG_POSITION ((float*)(0x4e4030 + 0x824))
#define TH08_BG_CAMERA   ((float*)(0x4e4030 + 0x6394))
enum { TH08_CAM_N = 3 + 19 };
static float th08_cam_prev[TH08_CAM_N], th08_cam_cur[TH08_CAM_N], th08_cam_saved[TH08_CAM_N];
static uint64_t th08_cam_tick; static int th08_cam_valid;
static void th08_camera_read(float* out) { memcpy(out, TH08_BG_POSITION, 12); memcpy(out + 3, TH08_BG_CAMERA, 19 * 4); }
static void th08_camera_write(const float* in) { memcpy(TH08_BG_POSITION, in, 12); memcpy(TH08_BG_CAMERA, in + 3, 19 * 4); }
static void th08_camera_tick(void) {
    int continuous = th08_cam_tick + 1 == th08_tick;
    memcpy(th08_cam_prev, th08_cam_cur, sizeof th08_cam_prev);
    th08_camera_read(th08_cam_cur);
    th08_cam_tick = th08_tick;
    th08_cam_valid = continuous;
    for (int i = 0; i < TH08_CAM_N && th08_cam_valid; ++i) {
        float d = th08_cam_cur[i] - th08_cam_prev[i];
        if (!isfinite(d) || fabsf(d) > 64.0f) th08_cam_valid = 0;
    }
}
static int th08_camera_place(void) {
    if (!cfg.enemy_interp || g_refresh <= 60 || th08_guard_failed || !th08_cam_valid || th08_cam_tick != th08_tick) return 0;
    int predict = cfg.predict || th08_any_sub();
    double alpha = predict && !th08_predict_on ? 0.0 : th08_phase;
    th08_camera_read(th08_cam_saved);
    if (memcmp(th08_cam_saved, th08_cam_cur, sizeof th08_cam_saved)) return 0;   /* something moved it since the tick */
    float at[TH08_CAM_N];
    for (int i = 0; i < TH08_CAM_N; ++i)
        at[i] = (predict ? th08_cam_cur[i] : th08_cam_prev[i]) + (th08_cam_cur[i] - th08_cam_prev[i]) * (float)alpha;
    th08_camera_write(at);
    return 1;
}
static void th08_camera_restore(void) { th08_camera_write(th08_cam_saved); }
/* ---- projectiles, stepped at the display's rate ([fixed60] substep=1; experimental).
 * BulletManager::OnUpdate (0x431240) updates the items, the 1536 bullets and the 256 lasers. It
 * is called on every tick with the engine's multiplier set to the tick's length, which is what
 * ZUN's timers, the ANM interpreter, the items and the lasers' growth all read. The aim is
 * that at every frame boundary each bullet is where, and what, the stock game would have it --
 * the trace of the demonstration holds it to that -- with its motion in between sliced.
 *
 *   A bullet's step is `position += velocity`, with the multiplier baked into the velocity
 *   when it was set (the engine's own slow-motion effect rescales all 1536 when it changes
 *   the multiplier). So around the call every live bullet's velocity is multiplied by the
 *   tick's length and afterwards restored, exactly. Outside this function velocities are
 *   always what the stock game would hold.
 *
 *   The live state's behaviours -- acceleration, turning, the speed curves, bounce, wrap --
 *   run on the frame tick only, and on the stock state: th08_behaviours_enter puts the
 *   bullet's velocity and the multiplier back, the game's own code runs once as it always
 *   did, and th08_behaviours_leave slices the result. A behaviour stepped in fractions
 *   integrates a changing speed over a finer grid and lands somewhere else; stepped once,
 *   the velocity is constant across the frame and the slices sum to the stock move.
 *
 *   On the call where a spawning bullet goes live the stock game makes the spawn state's
 *   fractional move and then falls through to the live state's full move, and does not tick
 *   the bullet's timer: a frame and a half of travel and no time. Sliced, that fall-through
 *   is a tick's worth of each. th08_projectiles_pass gives the move and the timer the rest of
 *   the frame.
 *
 *   Counters that count calls stay per-frame by being gated to the frame tick (the off-screen
 *   grace and count, two manager counters, the laser's graze-every-20-frames test), and a
 *   spawn or death animation's "finished" is only acted on there.
 *
 * Collision and graze are tested on every tick, against where the player is. That is the
 * point, and it is why this is not replay-safe. */
static float th08_vel_saved[0x600][3], th08_vel_scaled[0x600][3];
static uint8_t th08_vel_live[0x600];        /* the state word before the pass; 0 = free slot */
static float th08_pass_f = 1.0f, th08_pass_m = 1.0f;
static uint8_t* th08_behaving;              /* the bullet between behaviours_enter and _leave */
#define TH08_BULLETS ((uint8_t*)0xf54e90 + 0x1a880)
__attribute__((force_align_arg_pointer)) void __cdecl th08_behaviours_enter(uint8_t* b) {
    if (th08_pass_f >= 1.0f) return;
    unsigned i = (unsigned)(b - TH08_BULLETS) / 0x10b8;
    if (i >= 0x600) return;
    float* v = (float*)(b + 0xd50);
    if (th08_vel_live[i] && !memcmp(v, th08_vel_scaled[i], 12)) memcpy(v, th08_vel_saved[i], 12);
    else { const float back = 1.0f / th08_pass_f; v[0] *= back; v[1] *= back; v[2] *= back; }
    TH08_MULTIPLIER = th08_pass_m;
    th08_behaving = b;
}
__attribute__((force_align_arg_pointer)) void __cdecl th08_behaviours_leave(uint8_t* b) {
    if (th08_behaving != b) return;
    th08_behaving = NULL;
    unsigned i = (unsigned)(b - TH08_BULLETS) / 0x10b8;
    float* v = (float*)(b + 0xd50);
    /* a behaviour that changed the multiplier itself keeps its change */
    if (TH08_MULTIPLIER != th08_pass_m) th08_pass_m = TH08_MULTIPLIER;
    memcpy(th08_vel_saved[i], v, 12);
    v[0] *= th08_pass_f; v[1] *= th08_pass_f; v[2] *= th08_pass_f;
    memcpy(th08_vel_scaled[i], v, 12);
    if (!th08_vel_live[i]) th08_vel_live[i] = 1;
    TH08_MULTIPLIER = th08_pass_m * th08_pass_f;
}
/* Debug: TH08_FORCE_SUB in the environment keeps sub-stepping on through a replay, which is the
   only way to put the title screen's demonstration through it and diff the trace. */
static int th08_force_sub(void) { static int v = -1; if (v < 0) v = GetEnvironmentVariableA("TH08_FORCE_SUB", NULL, 0) != 0; return v; }
static int th08_projectiles_sub(void) {
    if (th08_force_sub()) return cfg.fixed_substep && cfg.substep && g_logic_rate != 60;
    return cfg.fixed_substep && cfg.substep && g_logic_rate != 60 && node_mode(0x431240) == MODE_SUB && !(th08_replay_tick && th08_replay_tick + 1 >= th08_tick);
}
int th08_test_hide;
/* Debug: TH08_RNG_TRACE in the environment counts, per game frame, who drew random numbers --
   the first return address up the frame-pointer chain that is not the RNG's own wrappers --
   and writes the table into the trace for the frames in the dump window. Two runs then say
   not just that the RNG parted on frame N but which call site drew a different number of
   times, which is the question every per-call hazard comes down to. */
static struct { uint32_t caller, parent; unsigned n; } th08_rng_callers[64];
static int th08_rng_trace(void) { static int v = -1; if (v < 0) v = GetEnvironmentVariableA("TH08_RNG_TRACE", NULL, 0) != 0; return v; }
__attribute__((force_align_arg_pointer)) void __cdecl th08_rng_seen(uint32_t* ret_slot, uint32_t* frame) {
    uint32_t caller = *ret_slot;
    for (int depth = 0; depth < 3; ++depth) {
        int rng = (caller >= 0x43ecc0 && caller < 0x43edb0) || (caller >= 0x406ef0 && caller < 0x406f40) ||
                  (caller >= 0x40d390 && caller < 0x40d3b0) || (caller >= 0x410c00 && caller < 0x410c40) || (caller >= 0x4143c0 && caller < 0x4143e0);
        if (!rng || !frame || IsBadReadPtr(frame, 8)) break;
        caller = frame[1]; frame = (uint32_t*)frame[0];
    }
    uint32_t parent = frame && !IsBadReadPtr(frame, 8) ? frame[1] : 0;   /* one level further up: "SpawnItem" says little, "SpawnItem from the bullet loop" a lot */
    for (int i = 0; i < 64; ++i) {
        if ((th08_rng_callers[i].caller == caller && th08_rng_callers[i].parent == parent) || !th08_rng_callers[i].caller) {
            th08_rng_callers[i].caller = caller; th08_rng_callers[i].parent = parent; ++th08_rng_callers[i].n; return;
        }
    }
}
static int th08_test_lasttick(void) { static int v = -1; if (v < 0) v = GetEnvironmentVariableA("TH08_TEST_LASTTICK", NULL, 0) != 0; return v; }
static int th08_any_sub(void) { return th08_player_sub() || th08_projectiles_sub(); }
static int th08_projectiles_pass(struct Th08Elem* e) {
    uint8_t* bullets = TH08_BULLETS;
    const float f = g_dt, m = TH08_MULTIPLIER;
    th08_pass_f = f; th08_pass_m = m; th08_behaving = NULL;
    for (unsigned i = 0; i < 0x600; ++i) {
        uint8_t* b = bullets + i * 0x10b8; float* v = (float*)(b + 0xd50);
        th08_vel_live[i] = (uint8_t)*(const uint16_t*)(b + 0xdb8);
        if (!th08_vel_live[i]) continue;
        memcpy(th08_vel_saved[i], v, 12);
        v[0] *= f; v[1] *= f; v[2] *= f;
        memcpy(th08_vel_scaled[i], v, 12);
    }
    TH08_MULTIPLIER = m * f;
    /* Debug: TH08_TEST_LASTTICK makes the three hooked player-collision tests answer "nothing" on every
       tick but the frame's last, which is when a whole-frame move would have been tested. With that the sliced
       projectiles have to reproduce the stock game's trace exactly; without it they are
       allowed to differ, and only by what testing more often finds. */
    th08_test_hide = th08_test_lasttick() && g_phase + g_dt < 0.9999;
    th08_unlock();
    int r = e->callback(e->arg);
    th08_lock();
    th08_test_hide = 0;
    TH08_MULTIPLIER = th08_pass_m;
    const float back = 1.0f / f;
    for (unsigned i = 0; i < 0x600; ++i) {
        uint8_t* b = bullets + i * 0x10b8; float* v = (float*)(b + 0xd50);
        if (!*(const uint16_t*)(b + 0xdb8)) continue;
        if (th08_vel_live[i] && !memcmp(v, th08_vel_scaled[i], 12)) memcpy(v, th08_vel_saved[i], 12);
        else { v[0] *= back; v[1] *= back; v[2] *= back; }
        unsigned was = th08_vel_live[i];
        if (was >= 2 && was <= 4 && *(const uint16_t*)(b + 0xdb8) == 1 && f < 1.0f) {
            /* went live on this tick, which is a frame tick: see above */
            static const float part[5] = {0, 0, 1.0f / 2.0f, 1.0f / 2.5f, 1.0f / 3.0f};
            float rest = 1.0f - f, k = part[was] * rest;
            float* pos = (float*)(b + 0xd44);
            pos[0] += v[0] * k; pos[1] += v[1] * k; pos[2] += v[2] * k;
            float* sub = (float*)(b + 0xd8c + 4);          /* the bullet's timer: previous, sub-frame, current */
            if (*(const int*)(b + 0xd8c + 8) == 0 && *sub == 0.0f) *sub = -rest;
        }
    }
    th08_pass_f = 1.0f;
    return r;
}
static int th08_walk(struct Th08Elem* root, int calc) {
    int count;
    th08_lock();
restart:
    count = 0;
    for (struct Th08Elem* e = root; e; ) {
        if (!e->callback) { e = e->next; continue; }
        uint32_t fn = (uint32_t)(uintptr_t)e->callback;
        if (calc) {
            node_seen(fn, e->priority);
            int mode = node_mode(fn);
            if (fn == 0x452550 && g_major) th08_replay_tick = th08_tick;      /* replay playback is feeding the input */
            if (fn == 0x431240 && g_major) th08_world_tick = th08_tick;       /* the playfield is running, not paused */
            if (fn == 0x44c390) {
                /* the player: a 60 Hz callback whose movement alone is sliced (see above) */
                int sub = th08_player_sub();
                th08_move_factor = TH08_MULTIPLIER * (sub ? g_dt : 1.0f);
                if (!g_major) { if (sub) th08_player_minor(); count++; e = e->next; continue; }
                th08_player_tick = th08_tick;
                mode = MODE_FRAME;
            }
            if (fn == 0x431240) mode = th08_projectiles_sub() ? MODE_SUB : MODE_FRAME;
            if (mode == MODE_FRAME && !g_major) {
                /* a pause or a menu cut the list here on the frame tick: nothing after it runs */
                if (e == th08_stop_elem) { count = 1; break; }
                count++; e = e->next; continue;
            }
            if (mode == MODE_SUB) g_stat_sub_calls++; else g_stat_frame_calls++;
        } else th08_draw_callback = fn;
        int r;
        int camera = !calc && (fn == 0x409200 || fn == 0x409640) && th08_camera_place();
    again:
        if (calc && fn == 0x431240 && th08_projectiles_sub()) r = th08_projectiles_pass(e);
        else { th08_unlock(); r = e->callback(e->arg); th08_lock(); }
        if (camera) { th08_camera_restore(); camera = 0; }
        if (!calc && fn == 0x409640 && cfg.dim[DIM_BACKGROUND] > 0) {
            /* both background callbacks have drawn and nothing of the world has: flush what they
               left in the sprite batch, then let the shared code blend its quad over the
               viewport, which is still the playfield */
            ((Th08ThisFn)0x462e40)(*(void**)0x18bdc90);
            g_draw_prio = g_game->draw.world_prio; dim_at_callback(); g_draw_prio = -1; g_batch_class = DIM_NONE;
        }
        if (calc && fn == 0x44c390) memcpy(th08_player_major, TH08_PLAYER + 0x2b4, sizeof th08_player_major);
        if (calc && fn == 0x407400 && g_major) th08_camera_tick();
        switch (r) {
        case 0: { struct Th08Elem* gone = e; e = e->next; ((Th08CutFn)0x43cf50)(TH08_CHAIN, gone); count++; continue; }
        case 2: goto again;
        case 3: if (calc && g_major) th08_stop_elem = e; count = 1; goto done;
        case 4: count = 0; goto done;
        case 5: count = -1; goto done;
        case 6: if (calc) goto restart; break;
        default: break;
        }
        count++; e = e->next;
    }
done:
    th08_unlock();
    if (!calc) th08_draw_callback = 0;
    return count;
}
/* Debug only ([hfr] debug=1, replay_trace=1): one line per game frame into th08_trace.txt, taken
   at the frame boundary before anything runs. The title screen's demonstration is a replay, so
   two runs of it at different settings diff cleanly, and the first line that differs is the
   frame a sub-stepped system stopped agreeing with the 60 Hz game. The RNG is the strict part:
   it must match exactly. Positions are quantised, because a sub-stepped integration is allowed
   to differ in the last bits and is not allowed to differ by a pixel. */
static FILE* th08_trace_file;
static void th08_trace_bullets(void) {
    const uint8_t* bullets = (const uint8_t*)0xf54e90 + 0x1a880;
    for (unsigned i = 0; i < 0x600; ++i) {
        const uint8_t* b = bullets + i * 0x10b8;
        if (!*(const uint16_t*)(b + 0xdb8)) continue;
        fprintf(th08_trace_file, "  b%u st=%u fl=%x/%x grace=%d off=%d pos=%.3f,%.3f vel=%.4f,%.4f spd=%.4f ang=%.4f t=%d+%.3f vm=%d+%.3f\n", i,
                *(const uint16_t*)(b + 0xdb8), *(const uint32_t*)(b + 0xdac), *(const uint32_t*)(b + 0xdb0),
                *(const int*)(b + 0xda8), *(const int16_t*)(b + 0xdba),
                *(const float*)(b + 0xd44), *(const float*)(b + 0xd48), *(const float*)(b + 0xd50), *(const float*)(b + 0xd54),
                *(const float*)(b + 0xd68), *(const float*)(b + 0xd74), *(const int*)(b + 0xd8c + 8), *(const float*)(b + 0xd8c + 4), *(const int*)(b + 0x2a4 + 0x40), *(const float*)(b + 0x2a4 + 0x3c));
    }
}
static unsigned th08_trace_frame;
static void th08_trace(int in_stage) {
    if (!cfg.debug || !cfg.replay_trace) return;
    if (!in_stage) { th08_trace_frame = 0; return; }
    if (!th08_trace_file) th08_trace_file = fopen("th08_trace.txt", "w");
    if (!th08_trace_file) return;
    const uint8_t* bullets = (const uint8_t*)0xf54e90 + 0x1a880;
    unsigned nb = 0, nl = 0; int64_t bx = 0, by = 0; uint32_t states = 0;
    for (unsigned i = 0; i < 0x600; ++i) {
        const uint8_t* b = bullets + i * 0x10b8;
        uint16_t state = *(const uint16_t*)(b + 0xdb8);
        if (!state) continue;
        ++nb; states = states * 31 + state;
        bx += (int64_t)lrintf(*(const float*)(b + 0xd44) * 4.0f);
        by += (int64_t)lrintf(*(const float*)(b + 0xd48) * 4.0f);
    }
    const uint8_t* lasers = (const uint8_t*)0xf54e90 + 0x660938;
    for (unsigned i = 0; i < 0x100; ++i) if (*(const int*)(lasers + i * 0x59c + 0x584)) ++nl;
    const float* pp = (const float*)0x17d61ac;
    fprintf(th08_trace_file, "f=%u rng=%04x/%u p=%.2f,%.2f nb=%u st=%08x bx=%lld by=%lld nl=%u\n",
            th08_trace_frame++, *(const uint16_t*)0x164d520, *(const uint32_t*)0x164d524,
            pp[0], pp[1], nb, (unsigned)states, (long long)bx, (long long)by, nl);
    if (cfg.replay_trace_to && (int)th08_trace_frame - 1 >= cfg.replay_trace_from && (int)th08_trace_frame - 1 <= cfg.replay_trace_to) th08_trace_bullets();
    if (th08_rng_trace()) {
        if (cfg.replay_trace_to && (int)th08_trace_frame - 1 >= cfg.replay_trace_from && (int)th08_trace_frame - 1 <= cfg.replay_trace_to)
            for (int i = 0; i < 64 && th08_rng_callers[i].caller; ++i)
                fprintf(th08_trace_file, "  rng %06x<%06x x%u\n", (unsigned)th08_rng_callers[i].caller, (unsigned)th08_rng_callers[i].parent, th08_rng_callers[i].n);
        memset(th08_rng_callers, 0, sizeof th08_rng_callers);
    }
    if ((th08_trace_frame & 63) == 0) fflush(th08_trace_file);
}
static int th08_calc_pass(void) {
    if (!g_major && cfg.debug && cfg.replay_trace >= 2 && th08_trace_file && cfg.replay_trace_to &&
        (int)th08_trace_frame - 1 >= cfg.replay_trace_from && (int)th08_trace_frame - 1 <= cfg.replay_trace_to) {
        /* replay_trace=2: the per-bullet dump on the ticks between frames too */
        fprintf(th08_trace_file, " minor phase=%.3f\n", g_phase);
        th08_trace_bullets();
    }
    if (g_major) {
        th08_stop_elem = NULL; ++th08_tick;
        int in_stage = 0;
        if (cfg.debug && cfg.replay_trace)
            for (struct Th08Elem* e = TH08_CHAIN; e; e = e->next) if ((uintptr_t)e->callback == 0x44c390) in_stage = 1;
        th08_trace(in_stage);
    }
    return th08_walk(TH08_CHAIN, 1);
}
static int __attribute__((fastcall, force_align_arg_pointer)) th08_update(void* chain) {
    (void)chain;
    th08_major = g_major && !g_skip_update;
    if (g_skip_update) return 1;
    return th08_calc_pass();
}
static void __attribute__((fastcall)) th08_audio(void* sound) {
    if (th08_major) ((Th08ThisFn)0x45d790)(sound);
}
/* The catch-up tick: an update pass with no frame drawn behind it. */
static int th08_update_only(void) {
    int r = th08_calc_pass();
    if (g_major) ((Th08ThisFn)0x45d790)((void*)0x18b8a68);
    return r == 0 ? 1 : r == -1 ? 2 : 0;
}
static uint64_t th08_draw_guard(void) {
    /* The RNG and authoritative player position/velocity. Sprite scratch data
       intentionally excluded. A limited runtime tripwire, not a replay proof. */
    static const struct { uintptr_t address; unsigned size; } ranges[] = {
        {0x164d520, 8}, {0x17d61ac, 12}, {0x17d62f0, 12}
    };
    uint64_t h = 14695981039346656037ull;
    for (unsigned r = 0; r < sizeof ranges / sizeof *ranges; ++r)
        for (unsigned i = 0; i < ranges[r].size; ++i)
            h = (h ^ *(uint8_t*)(ranges[r].address + i)) * 1099511628211ull;
    return h;
}
/* Every draw callback seen, once each, under debug: this is how the draw-side table below
   gets written for a game whose lists are built at run time. */
static void th08_draw_seen(const struct Th08Elem* e) {
    static uint32_t seen[48]; static int n;
    for (int i = 0; i < n; ++i) if (seen[i] == (uint32_t)(uintptr_t)e->callback) return;
    if (n >= 48) return;
    seen[n++] = (uint32_t)(uintptr_t)e->callback;
    LOG("draw node: 0x%06x priority %d", (unsigned)(uintptr_t)e->callback, e->priority);
}
static int __attribute__((fastcall, force_align_arg_pointer)) th08_draw(void* chain) {
    (void)chain;
    if (cfg.debug) for (struct Th08Elem* e = TH08_CHAIN + 1; e; e = e->next) if (e->callback) th08_draw_seen(e);
    uint64_t before = th08_draw_guard();
    int r = th08_walk(TH08_CHAIN + 1, 0);
    /* A stage loads on a thread of its own, seeding and drawing random numbers while the
       loading screen is being drawn: one changed hash is a coincidence of timing. A draw pass
       that really advances the simulation does it every frame. */
    static unsigned streak;
    if (before != th08_draw_guard()) {
        if (++streak == 8 && !th08_guard_failed) {
            th08_guard_failed = 1;
            LOG("TH08 draw guard: drawing changed gameplay state on 8 consecutive frames; smoothing is off");
        }
    } else streak = 0;
    return r;
}
/* ---- what is drawn where.
 * Smoothing has two time bases. Interpolation shows the past: between the last two 60 Hz
 * states, up to a frame late, always exact. Prediction shows the present: the last state
 * carried forward along its last step, never late, wrong for a frame when something turns.
 * A shooter cannot afford the first for the player -- it is a frame of input lag the stock
 * game does not have -- and once the player is shown in the present, everything she can
 * collide with has to be shown there too, or every bullet looks a frame further away than the
 * collision test will find it. So the playfield is predicted and the interface, menus and
 * title screen, where nothing is being dodged, are interpolated.
 *
 * The player herself is predicted from the input, not from her last step: the frame tick that
 * will move her has not sampled the keys yet, but the keys can be read now, and
 * `position + velocity(keys now) * phase` is where that tick will put her if they are still
 * down. Nothing in the game is written for this. Her position at every 60 Hz tick, and with it
 * every collision, graze and replay, is the stock game's. */
static float th08_lead_own[2], th08_lead_attached[2];
static int th08_playfield(uint32_t callback) {
    switch (callback) {
    case 0x409200: case 0x409640:         /* stage background */
    case 0x42e120: case 0x42eb90:         /* enemies */
    case 0x44d530: case 0x44d630:         /* player, options, shots */
    case 0x427f00:                        /* effects */
    case 0x432b50:                        /* bullets, lasers, items */
        return 1;
    default: return 0;
    }
}
/* The player's own sprite, her four options, and the two effects that are pinned to her (the
   focus ring and the hit-box marker): a VM is the first 0x2a4 bytes of each. */
static int th08_player_vm(const uint8_t* vm) {
    const uint8_t* p = TH08_PLAYER;
    if (vm == p + 0x10) return 1;
    if (vm >= p + 0x40c && vm < p + 0x40c + 4 * 0x2f4) return 2;
    if (vm == *(const uint8_t* const*)(p + 0xbe834) || vm == *(const uint8_t* const*)(p + 0xe2b24)) return 2;
    return 0;
}
/* Dimming. The quad hook sees every 2D sprite with its VM and the draw callback it came from,
   and the vertex colour is right there in the quad, so a sprite is classified by where its VM
   lives and faded in place -- no batch flushes, no Direct3D-level hooks:
     effects        everything EffectManager's draw callback draws, bar what is pinned to the player
     items          ItemManager's pool (the bullet callback draws them, after the bullets)
     player shots   the 128-entry shot pool inside Player, and her options
   The background is the shared black quad, blended over the playfield once both background
   callbacks have drawn. */
static int th08_dim_class(const uint8_t* vm) {
    const uint8_t* p = TH08_PLAYER;
    if (vm >= p + 0xbe838 && vm < p + 0xbe838 + 0x80 * 0x484) return DIM_PLAYER_SHOTS;
    if (vm >= p + 0x40c && vm < p + 0x40c + 4 * 0x2f4) return DIM_PLAYER_SHOTS;
    if (vm >= (const uint8_t*)0x1653648 && vm < (const uint8_t*)0x1653648 + 0x17b088) return DIM_ITEMS;
    if (th08_draw_callback == 0x427f00 && !th08_player_vm(vm)) return DIM_EFFECTS;
    return DIM_NONE;
}
/* Bullets, lasers and items, by pool: drawn where they are when they are really being stepped. */
static int th08_projectile_vm(const uint8_t* vm) {
    const uint8_t* bm = (const uint8_t*)0xf54e90;
    if (vm >= bm + 0x1a880 && vm < bm + 0x1a880 + 0x600 * 0x10b8) return 1;
    if (vm >= bm + 0x660938 && vm < bm + 0x660938 + 0x100 * 0x59c) return 1;
    return vm >= (const uint8_t*)0x1653648 && vm < (const uint8_t*)0x1653648 + 0x17b088;
}
static void th08_frame_lead(double alpha) {
    th08_lead_on = 0; th08_predict_on = 0;
    memset(th08_lead_own, 0, sizeof th08_lead_own); memset(th08_lead_attached, 0, sizeof th08_lead_attached);
    if (!cfg.enemy_interp || g_refresh <= 60 || th08_guard_failed) return;
    int playing = th08_world_tick == th08_tick && th08_tick;
    if (th08_any_sub()) th08_predict_on = playing;
    if (th08_player_sub()) {
        /* she really is where she is; what was placed relative to her on the frame tick follows */
        th08_predict_on = playing;
        if (playing && th08_player_tick == th08_tick) {
            const float* pos = (const float*)(TH08_PLAYER + 0x2b4);
            th08_lead_attached[0] = pos[0] - th08_player_major[0];
            th08_lead_attached[1] = pos[1] - th08_player_major[1];
            th08_lead_on = 1;
        }
        return;
    }
    if (!cfg.predict && !th08_any_sub()) return;
    th08_predict_on = playing;
    if (!playing || th08_player_tick != th08_tick || !th08_player_movable()) return;
    unsigned in = th08_live_input();
    const uint8_t* p = TH08_PLAYER;
    int focused = *(const int*)(p + 0xfdc) ? (*(const uint32_t*)(p + 0xfe0) & 1) != 0 : (in & 4) != 0;
    float v[2];
    if (!th08_player_velocity(in, focused, v)) return;
    const float* pos = (const float*)(p + 0x2b4);
    float to[2] = { pos[0] + v[0] * TH08_MULTIPLIER * (float)alpha, pos[1] + v[1] * TH08_MULTIPLIER * (float)alpha };
    th08_player_clamp(to);
    th08_lead_own[0] = th08_lead_attached[0] = to[0] - pos[0];
    th08_lead_own[1] = th08_lead_attached[1] = to[1] - pos[1];
    th08_lead_on = 1;
}
static void th08_quad_shape(const struct Th08QuadHistory* h, double alpha, int predict, float out[4][3]);
static double th08_pred_sum; static float th08_pred_max; static unsigned th08_pred_n, th08_pred_over1, th08_pred_over4;
static unsigned th08_pred_class[5][3];
/* Debug: how wrong the last frame's prediction turned out to be, now that the state it was
   predicting exists -- the visible cost of showing the present. Worst corner, per class. */
static void th08_prediction_census(const struct Th08QuadHistory* h, float pos[4][3]) {
    float guess[4][3]; th08_quad_shape(h, 1.0, 1, guess);
    float err = 0;
    for (unsigned c = 0; c < 4; ++c) { float ex = pos[c][0] - guess[c][0], ey = pos[c][1] - guess[c][1], e = sqrtf(ex * ex + ey * ey); if (e > err) err = e; }
    if (err >= 64.0f) return;
    uint32_t cb = th08_draw_callback;
    int k = cb == 0x432b50 ? 0 : cb == 0x44d530 || cb == 0x44d630 ? 1 : cb == 0x427f00 ? 2 : cb == 0x42e120 || cb == 0x42eb90 ? 3 : 4;
    ++th08_pred_class[k][0]; if (err > 1.0f) ++th08_pred_class[k][1]; if (err > 4.0f) ++th08_pred_class[k][2];
    th08_pred_sum += err; ++th08_pred_n;
    if (err > 1.0f) ++th08_pred_over1;
    if (err > 4.0f) ++th08_pred_over4;
    if (err > th08_pred_max) th08_pred_max = err;
}
/* Where to draw a quad between (or beyond) its last two frame-tick shapes.
 *
 * Moving the four corners independently along straight lines is right for a sprite that only
 * translates. A sprite that spins has corners that travel on arcs: interpolated along chords
 * it shrinks between ticks, and predicted along tangents it swells and snaps back sixty times a
 * second, which on a screen full of spinning bullets is a shimmer. So the quad is taken apart:
 * its centre moves along a line, and the corners turn about it by the angle the quad turned
 * between the two ticks and grow by the ratio their arms grew. One angle, from the first
 * corner, serves all four -- the quad is rigid apart from scale. A turn too large to be a
 * rotation (a flip, a sprite swapped under the same VM) falls back to straight lines.
 *
 * A gap in the ticks, a script restart or a teleport shows the quad where it is; so does a VM
 * that draws twice in one tick with different shapes, because that is one VM standing in for
 * several sprites and its history describes none of them. */
static int th08_quad_pose(struct Th08QuadHistory* h, uintptr_t key, uintptr_t script, int age,
                          float pos[4][3], double alpha, int predict, float out[4][3]) {
    for (unsigned c = 0; c < 4; ++c) for (unsigned i = 0; i < 3; ++i) if (!isfinite(pos[c][i])) { h->valid = 0; return 0; }
    if (h->key != key || h->script != script || th08_tick > h->tick + 1 || th08_tick < h->tick || age < h->age) {
        if (h->key != key) h->shared_tick = 0;
        h->key = key; h->script = script; h->tick = th08_tick; h->age = age; h->valid = 0;
        memcpy(h->current, pos, sizeof h->current); memcpy(h->previous, pos, sizeof h->previous);
    } else if (th08_tick != h->tick) {
        float cx = 0, cy = 0, px = 0, py = 0;
        for (unsigned c = 0; c < 4; ++c) { cx += pos[c][0]; cy += pos[c][1]; px += h->current[c][0]; py += h->current[c][1]; }
        float dx = (cx - px) * 0.25f, dy = (cy - py) * 0.25f;
        if (cfg.debug && predict && h->valid) th08_prediction_census(h, pos);
        memcpy(h->previous, h->current, sizeof h->previous); memcpy(h->current, pos, sizeof h->current);
        h->tick = th08_tick; h->age = age; h->valid = dx * dx + dy * dy < 64.0f * 64.0f;
        /* A VM that has recently stood in for several sprites still is one: its first draw of a
           tick would otherwise be smoothed from wherever its last draw of the previous tick
           happened to be, which is a different sprite's position (text does this per glyph). */
        if (h->shared_tick && th08_tick - h->shared_tick < 120) h->valid = 0;
    } else if (memcmp(h->current, pos, sizeof h->current)) {
        h->valid = 0; h->shared_tick = th08_tick; memcpy(h->current, pos, sizeof h->current);
    }
    if (!h->valid) return 0;
    th08_quad_shape(h, alpha, predict, out);
    return memcmp(out, pos, sizeof h->current) != 0;
}
static void th08_quad_shape(const struct Th08QuadHistory* h, double alpha, int predict, float out[4][3]) {
    const float a = (float)alpha;
    float pc[3] = {0, 0, 0}, cc[3] = {0, 0, 0};
    for (unsigned c = 0; c < 4; ++c) for (unsigned i = 0; i < 3; ++i) { pc[i] += h->previous[c][i] * 0.25f; cc[i] += h->current[c][i] * 0.25f; }
    float centre[3];
    for (unsigned i = 0; i < 3; ++i) centre[i] = (predict ? cc[i] : pc[i]) + (cc[i] - pc[i]) * a;
    /* the turn, from the first corner's arm */
    float ax = h->previous[0][0] - pc[0], ay = h->previous[0][1] - pc[1];
    float bx = h->current[0][0] - cc[0],  by = h->current[0][1] - cc[1];
    float cross = ax * by - ay * bx, dot = ax * bx + ay * by;
    float la = ax * ax + ay * ay, lb = bx * bx + by * by;
    float cs = 1.0f, sn = 0.0f; int turning = 0;
    if (la > 0.25f && lb > 0.25f && fabsf(cross) > 1e-4f * sqrtf(la * lb)) {
        float theta = atan2f(cross, dot);
        if (fabsf(theta) < 1.0f) { cs = cosf(theta * a); sn = sinf(theta * a); turning = 1; }
        else { cs = 2.0f; }                                /* not a rotation: straight lines */
    }
    for (unsigned c = 0; c < 4; ++c) {
        if (cs > 1.5f) {
            for (unsigned i = 0; i < 3; ++i)
                out[c][i] = (predict ? h->current[c][i] : h->previous[c][i]) + (h->current[c][i] - h->previous[c][i]) * a;
            continue;
        }
        float ox = h->previous[c][0] - pc[0], oy = h->previous[c][1] - pc[1];
        float nx = h->current[c][0] - cc[0],  ny = h->current[c][1] - cc[1];
        float fx, fy;
        if (!turning) {                                    /* pure translation and scale: the arms are parallel */
            fx = (predict ? nx : ox) + (nx - ox) * a; fy = (predict ? ny : oy) + (ny - oy) * a;
        } else {
            float lo = sqrtf(ox * ox + oy * oy), ln = sqrtf(nx * nx + ny * ny);
            float from_x = predict ? nx : ox, from_y = predict ? ny : oy, from_l = predict ? ln : lo;
            float want = from_l + (ln - lo) * a, k = from_l > 1e-3f ? want / from_l : 1.0f;
            if (k < 0) k = 0;
            fx = (from_x * cs - from_y * sn) * k; fy = (from_x * sn + from_y * cs) * k;
        }
        out[c][0] = centre[0] + fx; out[c][1] = centre[1] + fy;
        out[c][2] = (predict ? h->current[c][2] : h->previous[c][2]) + (h->current[c][2] - h->previous[c][2]) * a;
    }
}
/* DrawInner has already constructed and clipped the quad. Move only the temporary vertices
 * handed to the native batch copier, then restore them. No player, bullet, laser or animation
 * VM position is written by this hook. */
static int __attribute__((fastcall, force_align_arg_pointer)) th08_quad(void* manager, void* unused, uint8_t* vm, uint8_t* vertices) {
    (void)unused;
    float saved[4][3];
    for (unsigned c = 0; c < 4; ++c) memcpy(saved[c], vertices + c * 28, 12);
    ++th08_quads;
    uint32_t colours[4]; int faded = 0;
    { int cls = th08_dim_class(vm);
      if (cls != DIM_NONE && cfg.dim[cls] > 0) {
          /* alpha, for both blend modes: the engine's additive mode is SRCALPHA/ONE, so the
             vertex alpha scales an additive sprite exactly as it scales an ordinary one */
          unsigned keep = 100 - (unsigned)(cfg.dim[cls] > 100 ? 100 : cfg.dim[cls]);
          for (unsigned c = 0; c < 4; ++c) {
              uint32_t* colour = (uint32_t*)(vertices + c * 28 + 16);
              colours[c] = *colour;
              *colour = (*colour & 0x00ffffffu) | ((((*colour >> 24) * keep) / 100) << 24);
          }
          faded = 1;
      } }
    int attached = th08_lead_on ? th08_player_vm(vm) : 0;
    if (attached) {
        const float* lead = attached == 1 ? th08_lead_own : th08_lead_attached;
        for (unsigned c = 0; c < 4; ++c) {
            *(float*)(vertices + c * 28) += lead[0];
            *(float*)(vertices + c * 28 + 4) += lead[1];
        }
        ++th08_smoothed;
    } else if (th08_history && cfg.enemy_interp && g_refresh > 60 && !th08_guard_failed && !(th08_projectiles_sub() && th08_projectile_vm(vm))) {
        int field = th08_playfield(th08_draw_callback);
        int predict = field && (cfg.predict || th08_any_sub());
        /* a paused playfield has no next state to be carried towards */
        double alpha = predict && !th08_predict_on ? 0.0 : th08_phase;
        uintptr_t key = (uintptr_t)vm;
        unsigned first = (unsigned)((key >> 2) * 2654435761u) & (TH08_HISTORY_COUNT - 1);
        struct Th08QuadHistory* h = NULL;
        for (unsigned i = 0; i < 16; ++i) {
            struct Th08QuadHistory* candidate = &th08_history[(first + i) & (TH08_HISTORY_COUNT - 1)];
            if (candidate->key == key || !candidate->key || candidate->tick + 2 < th08_tick) { h = candidate; break; }
        }
        float out[4][3];
        if (h && th08_quad_pose(h, key, *(uint32_t*)(vm + 0x21c), *(int*)(vm + 0x40), saved, alpha, predict, out)) {
            for (unsigned c = 0; c < 4; ++c) memcpy(vertices + c * 28, out[c], 12);
            ++th08_smoothed;
        }
    }
    int r = ((Th08AddQuadFn)0x462f10)(manager, vertices);
    for (unsigned c = 0; c < 4; ++c) memcpy(vertices + c * 28, saved[c], 12);
    if (faded) for (unsigned c = 0; c < 4; ++c) memcpy(vertices + c * 28 + 16, &colours[c], 4);
    return r;
}
/* The fraction of the way from the previous 60 Hz state to the current one that this
   presentation should show. With sub-stepping the scheduler's phase says where this tick sits
   in its frame (the same lag enemy_interp gives TH10-13's enemies); without it logic runs on
   a Bresenham share of the presentation slots and the remainder is the phase. */
static double th08_alpha(void) {
    double a = (cfg.substep && g_logic_rate != 60) ? g_phase + g_dt
                                                   : (double)g_lacc / (double)(g_refresh > 0 ? g_refresh : 60);
    return a < 0 ? 0 : a > 1 ? 1 : a;
}
/* Supervisor::TakeSnapshot(path), called from GameWindow::Present while Home reads as newly
   pressed. It asks for the back buffer and locks it, so the back-buffer hook has to be told to
   hand over a lockable copy; and "newly pressed" lasts a whole 60 Hz frame, which is now several
   presentations, so only the one that followed a frame tick takes the picture. */
static void __attribute__((thiscall, force_align_arg_pointer)) th08_snapshot(void* supervisor, const char* path) {
    if (!th08_major) return;
    g_in_screenshot = 1;
    ((void (__attribute__((thiscall)) *)(void*, const char*))0x44748f)(supervisor, path);
    g_in_screenshot = 0;
}
static int __attribute__((fastcall)) th08_frame_original(void* window) {
    static unsigned entered;
    if (entered++ < 3) LOG("TH08 frame entry: context=%p native hwnd=%p attached=%p dev=%p minimized=%d",window,*(void**)window,g_wnd,g_dev,g_minimized);
    th08_phase = th08_alpha();
    th08_frame_lead(th08_phase);
    int r = th08_render_original(window);
    static double report; double now = now_s();
    if (now - report >= 5.0) {
        LOG("TH08: tick=%llu, 2D quads=%u interpolated=%u, logic %d Hz, draw guard=%s",
            (unsigned long long)th08_tick, th08_quads, th08_smoothed, g_logic_rate, th08_guard_failed ? "failed" : "ok");
        if (cfg.debug && th08_pred_n)
            LOG("TH08 prediction: %u sprite-frames, mean miss %.3f px, max %.1f, over 1 px %.2f%%, over 4 px %.3f%%", th08_pred_n,
                th08_pred_sum / th08_pred_n, th08_pred_max, 100.0 * th08_pred_over1 / th08_pred_n, 100.0 * th08_pred_over4 / th08_pred_n);
        if (cfg.debug && th08_pred_n) {
            unsigned (*k)[3] = th08_pred_class;
            LOG("TH08 prediction by class (n, >1px, >4px): bullets %u %u %u | player %u %u %u | effects %u %u %u | enemies %u %u %u | other %u %u %u",
                k[0][0],k[0][1],k[0][2],k[1][0],k[1][1],k[1][2],k[2][0],k[2][1],k[2][2],k[3][0],k[3][1],k[3][2],k[4][0],k[4][1],k[4][2]);
        }
        memset(th08_pred_class, 0, sizeof th08_pred_class);
        th08_pred_sum = 0; th08_pred_max = 0; th08_pred_n = th08_pred_over1 = th08_pred_over4 = 0;
        report = now; th08_quads = th08_smoothed = 0;
    }
    return r;
}
static int th08_frame_original_c(void* window) { return th08_frame_original(window); }
static void th08_install_presentation(void) {
    /* This engine family keeps its settings where New Classic's are: the defaults have to be
       the replay-safe ones, and [hfr] substep=1 -- right for TH10-15, which stamp the rate
       into the replay -- would not be. */
    cfg.enemy_interp = GetPrivateProfileIntA("fixed60", "interpolate", 1, g_ini_path) != 0;
    cfg.predict = GetPrivateProfileIntA("fixed60", "predict", 1, g_ini_path) != 0;
    cfg.subtick_input = GetPrivateProfileIntA("fixed60", "subtick", 0, g_ini_path) != 0;
    cfg.fixed_substep = GetPrivateProfileIntA("fixed60", "substep", 0, g_ini_path) != 0;
    cfg.substep = cfg.subtick_input || cfg.fixed_substep;   /* either takes the scheduler off 60 Hz */
    LOG("TH08 presentation: interpolate=%d predict=%d subtick=%d substep=%d", cfg.enemy_interp, cfg.predict, cfg.subtick_input, cfg.fixed_substep);
    th08_history = calloc(TH08_HISTORY_COUNT, sizeof *th08_history);
    if (!th08_history) LOG("TH08: pose history allocation failed; interpolation unavailable");
    th08_render_original = (Th08ThisFn)g_p;
    ECOPY(0x441e70, 6); EJMP(0x441e76); stub_end();
    /* The shared frame scheduler takes the game's frame function over, as on TH10-15: it
       paces presentation, decides how many logic ticks this slot owes and whether each is a
       frame boundary, and calls back into the original through th08_frame_original. */
    patch_jmp(0x441e70, hfr_frame_ecx, site_expected(0x441e70, 5));
    const uint8_t nops[6] = {0x90,0x90,0x90,0x90,0x90,0x90};
    patch_bytes(0x441ecf, nops, 6, site_expected(0x441ecf, 6));
    site_call(0x441f4d, th08_update);
    /* fmul dword [0x17ce8e0] -> fmul dword [th08_move_factor], twice: x then y */
    { uint8_t op[6] = {0xd8, 0x0d}; uint32_t a = (uint32_t)(uintptr_t)&th08_move_factor; memcpy(op + 2, &a, 4);
      patch_bytes(0x44ba6a, op, 6, site_expected(0x44ba6a, 6));
      patch_bytes(0x44ba7c, op, 6, site_expected(0x44ba7c, 6)); }
    site_call(0x441f5a, th08_audio);
    site_call(0x441fec, th08_draw);
    /* Sub-stepped projectiles: all of these are inert at one tick per frame (every tick is a
       frame tick). */
    gate_block(0x43147b, 21, 0x431490, 0, -1);      /* bullet +0xda8: off-screen grace, counts calls */
    gate_block(0x431524, 24, 0x43153c, 0, -1);      /* bullet +0xdba: off-screen count, up */
    gate_block(0x431578, 24, 0x431590, 0, -1);      /* ... and down */
    gate_block(0x432112, 21, 0x432127, 0, -1);      /* manager +0x6ba53c */
    gate_block(0x432137, 21, 0x43214c, 0, -1);      /* manager +0x6ba54c */
    /* lasers graze on frames where timer % 20 == 0: once, on the tick the timer reached it */
    STUB_BEGIN(); ECOPY(0x431f0c, 13);
    E(0x8b,0x4d,0xd8);                                /* mov ecx,[ebp-0x28]: the laser */
    E(0x8b,0x81); E32(0x588);                         /* mov eax,[ecx+0x588]: timer.previous */
    E(0x3b,0x81); E32(0x590);                         /* cmp eax,[ecx+0x590]: timer.current */
    E(0x75,0x02, 0x31,0xd2);                          /* jne +2; xor edx,edx */
    EJMP(0x431f19); site_hook(0x431f0c, 13);
    /* The live state's behaviours -- [0x431322, 0x43146f): the behaviour scheduler and the nine
       flag-gated functions, self-contained -- run on the frame tick only, on the stock game's
       state (th08_behaviours_enter/leave). The ticks between skip straight to the move. */
    { uint8_t* stub = g_p;
      E_not_major(); E(0x75,0x05); EJMP(0x43146f);
      E(0xff,0x75,0xe0); ECALL((uintptr_t)th08_behaviours_enter); E(0x83,0xc4,0x04);   /* push [ebp-0x20]; call; add esp,4 */
      E(0x8b,0x4d,0xe0); ECALL(0x42ffc0); EJMP(0x43132a); stub_end();                   /* the 8 bytes replaced */
      g_stub_start = stub; site_hook(0x431322, 8);
      stub = g_p;
      E(0xff,0x75,0xe0); ECALL((uintptr_t)th08_behaviours_leave); E(0x83,0xc4,0x04);
      ECOPY(0x43146f, 10); EJMP(0x431479); stub_end();
      g_stub_start = stub; site_hook(0x43146f, 10); }
    /* A spawning bullet goes live, and a dying one is freed, when its animation script reports
       that it has finished. Stepped in fractions the script's clock passes its last instruction
       part way through a frame, so the answer arrived half a frame early -- the same "delay
       timers start a frame early" TH14 measured. The script still runs every tick; on the
       ticks between frames the answer is withheld, and the frame tick gets it again. */
    { uint8_t* stub = g_p;
      E(0xff,0x74,0x24,0x04); ECALL(0x45ea00); E_not_major(); E(0x75,0x02, 0x31,0xc0, 0xc2,0x04,0x00); stub_end();
      static const uintptr_t finished_calls[4] = {0x4317f3, 0x431904, 0x431a16, 0x431ad8};
      for (int i = 0; i < 4; ++i) site_call(finished_calls[i], stub); }
    if (th08_test_lasttick()) {
        /* not 0x449ff0, the spawn-state test: it only raises a flag, which the frame tick's
           "animation finished" then reads in the same call as the stock game does */
        static const struct { uintptr_t fn; unsigned char len, pop; } tests[3] = {{0x44a230,6,8},{0x44a470,6,8},{0x44a6a0,9,0x14}};
        for (int i = 0; i < 3; ++i) {
            STUB_BEGIN();
            E(0x83,0x3d); E32((uint32_t)(uintptr_t)&th08_test_hide); E(0x00);      /* cmp dword [th08_test_hide],0 */
            E(0x74,0x05, 0x31,0xc0, 0xc2,tests[i].pop,0x00);                         /* je run; xor eax,eax; ret n */
            ECOPY(tests[i].fn, tests[i].len); EJMP(tests[i].fn + tests[i].len);
            site_hook(tests[i].fn, tests[i].len);
        }
        LOG("TH08 TEST: player collision confined to the last tick of each frame");
    }
    if (th08_rng_trace()) {
        STUB_BEGIN();
        E(0x51,0x52,0x50);                              /* push ecx; push edx; push eax */
        E(0x8d,0x44,0x24,0x0c);                         /* lea eax,[esp+12]: the return address */
        E(0x55,0x50);                                   /* push ebp; push eax */
        ECALL((uintptr_t)th08_rng_seen); E(0x83,0xc4,0x08);
        E(0x58,0x5a,0x59);                              /* pop eax; pop edx; pop ecx */
        ECOPY(0x43ecc0, 6); EJMP(0x43ecc6);
        site_hook(0x43ecc0, 6);
    }
    stub_end();
    site_call(0x44215b, th08_snapshot);
    /* Keep the original native argument and return address in place. Copies
       become the C hook's two stack arguments; ret 4 consumes the native one. */
    uint8_t* quad = g_p;
    E(0xff,0x74,0x24,0x04, 0xff,0x75,0x08);
    ECALL((uintptr_t)th08_quad); E(0xc2,0x04,0x00); stub_end();
    site_call(0x462df2, quad);
    g_frame_hook_installed = 1;
    g_dim_available = 1;   /* no dispatch to wrap: the chain walker and the quad hook do the attributing */
    LOG("TH08 experimental: D3D8 bridge, shared frame scheduler, %s", cfg.substep ? "sub-stepped gameplay" : "60 Hz gameplay with interpolated 2D quads");
}
static const struct node_class th08_classes[] = {
    {0x445453, MODE_FRAME, "supervisor"},
    {0x402200, MODE_FRAME, "ascii"},
    {0x439bc7, MODE_FRAME, "game_manager"},
    {0x452550, MODE_FRAME, "replay_playback"},
    {0x407400, MODE_FRAME, "background"},
    {0x44c390, MODE_SUB,   "player"},
    {0x42c660, MODE_FRAME, "enemies"},
    {0x418010, MODE_FRAME, "spellcard"},
    {0x427bf0, MODE_FRAME, "effects"},
    {0x431240, MODE_SUB,   "projectiles"},
    {0x4338ca, MODE_FRAME, "gui"},
    {0x452490, MODE_FRAME, "replay_record"},
};
/* Declarative only: the menu offers a fade slider for every class some rule names, and this
   game classifies in its quad hook rather than through these. Priority 99 matches nothing. */
static const struct DimRule th08_dim_rules[] = {
    { 99, 99, NULL, -1, -1, -1, -1, DIM_ITEMS },
    { 99, 99, NULL, -1, -1, -1, -1, DIM_EFFECTS },
    { 99, 99, NULL, -1, -1, -1, -1, DIM_PLAYER_SHOTS },
};
static const struct GameProfile th08_profile = {
    .identity = &game_identities[GI_TH08],
    .d3d8 = 1,
    .install_presentation = th08_install_presentation,
    .frame_original = th08_frame_original_c,
    .update_only = th08_update_only,
    .draw = { .world_prio = 8, .rules = th08_dim_rules, .rule_count = sizeof th08_dim_rules / sizeof *th08_dim_rules },
    .classes = th08_classes, .class_count = sizeof th08_classes / sizeof *th08_classes,
    .d3dx = "d3dx8.dll"
};
