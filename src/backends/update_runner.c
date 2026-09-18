/* ------------------------------------------------------------------ update runner replacement */
typedef int (__thiscall *NodeFn)(void* arg);
struct ListNode { struct UpdateFunc* entry; struct ListNode* next; struct ListNode* prev; };
struct UpdateFunc { int priority; uint32_t flags; NodeFn func; void* on_reg; NodeFn on_cleanup; struct ListNode node; void* arg; };
_Static_assert(__builtin_offsetof(struct UpdateFunc, arg) == 0x20, "UpdateFunc layout");
/* TH13 grew the UpdateFunc by a field: its argument sits at +0x24. */
static inline void* node_arg(struct UpdateFunc* uf) {
    return g_game->layout.node_arg ? *(void**)((uint8_t*)uf + g_game->layout.node_arg) : uf->arg;
}
typedef void (__fastcall *RemoveNodeFn)(void* a, void* b);
typedef void (__attribute__((thiscall)) *RemoveNodeThisFn)(void* runner, void* node);
static inline void game_remove_node(struct UpdateFunc* uf, uint8_t* runner) {
    switch (g_game->remove_node_abi) {
        case REMOVE_NODE_RUNNER_THIS:
            ((RemoveNodeThisFn)g_game->addr.remove_node)(runner, uf); return;
        case REMOVE_NODE_RUNNER_FIRST:
            ((RemoveNodeFn)g_game->addr.remove_node)(runner, uf); return;
        default:
            ((RemoveNodeFn)g_game->addr.remove_node)(uf, runner); return;
    }
}
/* TH13's runner keeps the next list node in the runner itself and re-reads it after every
   callback, so a callback that removes the node after it (its remove function fixes the cell
   up) redirects the walk. Mirror that exactly: the game's own removal is what maintains it. */
static inline void next_store(uint8_t* runner, struct ListNode* n) { if (g_game->layout.runner_next) *(struct ListNode**)(runner + g_game->layout.runner_next) = n; }
static inline struct ListNode* next_load(uint8_t* runner, struct ListNode* n) { return g_game->layout.runner_next ? *(struct ListNode**)(runner + g_game->layout.runner_next) : n; }

#define CRIT ((LPCRITICAL_SECTION)g_game->addr.crit)
#define CRIT_COUNT (*(volatile uint8_t*)g_game->addr.crit_count)
static inline int crit_enabled(void) { return !g_game->critical_flag_mask || (G_MISC_FLAGS & g_game->critical_flag_mask); }
static inline void crit_enter(void) { if (crit_enabled()) { EnterCriticalSection(CRIT); CRIT_COUNT++; } }
static inline void crit_leave(void) { if (crit_enabled()) { LeaveCriticalSection(CRIT); CRIT_COUNT--; } }

static struct UpdateFunc* g_stop_node; /* node that returned "stop" on the last frame tick */
static unsigned g_stat_sub_calls, g_stat_frame_calls, g_stat_long, g_stat_vlong;

int __cdecl __attribute__((used)) hfr_runner(uint8_t* runner) {
    int count = 0;
    if (g_skip_update && runner == G_UPDATE_RUNNER) { enemy_interp(g_phase); return 1; }
    int is_update = runner == G_UPDATE_RUNNER;
    if (is_update) {
        /* The desync trace samples here, before the frame's first tick runs, so it reads the state
           at a frame boundary in both modes. It used to sample at the end of the pass, which under
           sub-stepping is one sub-tick -- a sixth of a frame at 360Hz -- into the frame, and that
           put a constant offset of most of a frame's movement into every comparison and buried the
           small real differences the trace exists to find. */
        if (g_major) { g_stop_node = NULL; g_frame_active = 0; replay_trace_frame(); }
        else subtick_input_begin();
    }
    crit_enter();
    struct ListNode* n = *(struct ListNode**)(runner + 0x18);
restart:
    while (n) {
        struct UpdateFunc* uf = n->entry;
        n = n->next; next_store(runner, n);
        if (!uf->func) continue;
        node_seen((uint32_t)uf->func, uf->priority);
        if (!(uf->flags & 2)) { count++; continue; }
    call_again:
        if (g_game->layout.runner_ending && *(int*)(runner + g_game->layout.runner_ending) != 0) {
            if (uf->on_cleanup) uf->on_cleanup(node_arg(uf));
            count++; continue;
        }
        int mode = node_mode((uint32_t)uf->func);
        if (mode == MODE_FRAME && !g_major) {
            if (uf == g_stop_node) { count = 1; goto done; } /* the list was cut here on the last frame tick */
            /* GameManager (g_game->addr.gm_callback) returns "stop" while paused (flags 0x10/0x20/0x40); a pause raised by a
               sub-stepped node mid-frame must cut the list immediately, not only at the next frame tick */
            if ((uint32_t)uf->func == g_game->addr.gm_callback) { uint8_t* gm = *(uint8_t**)g_game->addr.game_manager; if (gm && (*(uint32_t*)(gm + g_game->layout.gm_pause_flags) & 0x70)) { count = 1; goto done; } }
            count++; continue;
        }
        crit_leave();
        uint32_t fn = (uint32_t)uf->func;
        int replay_node = is_update && g_major && (fn == g_game->addr.record_callback || fn == g_game->addr.playback_callback);   /* replay record / playback nodes */
        int frame_before = 0;
        if (replay_node) {
            uint8_t* rm = G_REPLAY_MANAGER;
            if (rm) { frame_before = *(int*)(rm + g_game->layout.replay_frame); if (frame_before == 0) replay_stage_start(rm); }
        }
        set_factor(mode == MODE_SUB ? g_dt : 1.0f);
        if (mode == MODE_SUB) g_stat_sub_calls++; else g_stat_frame_calls++;
        /* Edge-triggered actions (notably Marisa B formation switching) belong
           to the frame boundary, even while movement/focus are sampled faster. */
        uint32_t saved_pressed = 0, saved_released = 0, saved_bomb = 0;
        int player_minor = g_game->mask_minor_player_edges && fn == g_game->addr.player_callback && !g_major;
        if (player_minor) {
            saved_pressed = input_read(g_game->addr.game_pressed);
            saved_released = input_read(g_game->addr.game_released);
            saved_bomb = G_GAME_INPUT & 2;
            set_game_input(G_GAME_INPUT & ~2u);
            input_write(g_game->addr.game_pressed, 0); input_write(g_game->addr.game_released, 0);
        }
        int r = uf->func(node_arg(uf));
        if (player_minor) {
            input_write(g_game->addr.game_pressed, saved_pressed);
            input_write(g_game->addr.game_released, saved_released);
            set_game_input(G_GAME_INPUT | saved_bomb);
        }
        if (fn == g_game->addr.player_callback) { uint8_t* pl = *(uint8_t**)g_game->addr.player; if (pl) { g_ptf_prev = g_ptf_cur; g_ptf_cur = *(float*)(pl + g_game->layout.player_timer); } }
        if (replay_node) { uint8_t* rm = G_REPLAY_MANAGER; if (rm && *(int*)(rm + g_game->layout.replay_frame) != frame_before) g_frame_active = 1; }
        crit_enter();
        n = next_load(runner, n);
        switch (r) {
        case 0: game_remove_node(uf, runner); n = next_load(runner, n); count++; break;
        case 2: if (uf->flags & 2) goto call_again; count++; break;
        case 3: if (g_major) g_stop_node = uf; count = 1; goto done;
        case 4: count = 0; goto done;
        case 8: if (g_game->runner_return8_ends) { count = 0; goto done; } count++; break;
        case 5: count = -1; goto done;
        case 6: n = *(struct ListNode**)(runner + 0x18); next_store(runner, n); count = 0; goto restart;
        case 7: if (uf->on_cleanup) uf->on_cleanup(node_arg(uf)); count++; break;
        default: count++; break;
        }
    }
done:
    crit_leave();
    set_factor(1.0f);
    if (is_update) {
        if (g_major && !g_skip_update) ++g_site_census_frames;
        subtick_input_end(); enemy_interp(g_phase);
    }
    return count;
}
/* The replacement runner is entered by a five-byte jump written over the game's runner, so
 * from the jump onwards the whole function is ours -- including its last instruction, which
 * is where anything that wants to run after an update pass puts its hook. thprac's menu is
 * exactly that: in TH10..TH13 its update callback sits on the `ret` that ends this function,
 * and a replacement that returns on its own quietly hides it (its ImGui frame never opens,
 * so its draw-side hook renders nothing and the menu simply never appears).
 *
 * So the replacement does not return by itself. It leaves the stack and EAX exactly as the
 * game's own epilogue does -- return value in EAX, ESP on the caller's return address, the
 * callee-saved registers already restored by the C ABI -- and jumps to the game's `ret`.
 * That instruction still belongs to the game, and whoever hooked it still gets it. Nothing
 * here knows or cares who that is; the point is simply not to take an instruction that was
 * never ours to take.
 *
 * `g_runner_tail` is that address (install.c picks it), or one of the two labels below when
 * the profile does not record one, so the jump is always to a real `ret`. The catch-up pass
 * in limiter.c deliberately calls hfr_runner directly and not through here: it is an extra
 * update with no frame drawn behind it, and a menu should not be told about it. */
void* g_runner_tail;   /* external linkage: the thunks below reach it by name */
extern void hfr_runner_entry(void);        /* the runner is in EBX */
extern void hfr_runner_stack_entry(void);  /* ... or on the stack (TH10) */
extern void hfr_runner_ecx_entry(void);    /* ... or in ECX (TH14 on) */
extern void hfr_runner_tail_ret(void);     /* fallbacks: our own ending, matching each one */
extern void hfr_runner_tail_ret4(void);
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _hfr_runner_entry\n_hfr_runner_entry:\n"
    "  push ebx\n  call _hfr_runner\n  add esp, 4\n"
    "  jmp dword ptr [_g_runner_tail]\n"
    ".globl _hfr_runner_tail_ret\n_hfr_runner_tail_ret:\n"
    "  ret\n"
    ".globl _hfr_runner_stack_entry\n_hfr_runner_stack_entry:\n"
    "  mov eax, [esp+4]\n  push eax\n  call _hfr_runner\n  add esp, 4\n"
    "  jmp dword ptr [_g_runner_tail]\n"
    ".globl _hfr_runner_ecx_entry\n_hfr_runner_ecx_entry:\n"
    "  push ecx\n  call _hfr_runner\n  add esp, 4\n"
    "  jmp dword ptr [_g_runner_tail]\n"
    ".globl _hfr_runner_tail_ret4\n_hfr_runner_tail_ret4:\n"
    "  ret 4\n"
    ".att_syntax\n"
);
/* A game's runner ends in `ret` (or `ret 4` where it takes its argument on the stack). Accept
   an int3 too: a debugger or another patch may already own that instruction, which is the
   whole reason for jumping to it, and it restores the byte itself when it runs. Anything else
   means the profile is pointing somewhere it should not, and we end the pass ourselves. */
/* The entry thunk for a game's calling convention, and the ending that matches it. Only the
   stack form returns with `ret 4`; a runner whose object arrives in a register returns with a
   plain `ret` whichever register it is. */
void* runner_entry_thunk(void) {
    switch (g_game->runner_arg) {
        case RUNNER_ARG_STACK: return (void*)hfr_runner_stack_entry;
        case RUNNER_ARG_ECX:   return (void*)hfr_runner_ecx_entry;
        default:               return (void*)hfr_runner_entry;
    }
}
static int runner_tail_usable(uintptr_t addr, int stack_arg) {
    if (!addr) return 0;
    uint8_t b = *(volatile uint8_t*)addr;
    return b == 0xcc || b == (stack_arg ? 0xc2 : 0xc3);
}
static void runner_tail_select(void) {
    int stack_arg = g_game->runner_arg == RUNNER_ARG_STACK;
    void* own = stack_arg ? (void*)hfr_runner_tail_ret4 : (void*)hfr_runner_tail_ret;
    if (runner_tail_usable(g_game->addr.runner_ret, stack_arg)) {
        g_runner_tail = (void*)g_game->addr.runner_ret;
        LOG("update runner: the pass ends on the game's own instruction at 0x%06x, so a hook there still runs",
            (unsigned)g_game->addr.runner_ret);
    } else {
        g_runner_tail = own;
        if (g_game->addr.runner_ret)
            LOG("update runner: 0x%06x is not the ending this profile describes; the pass ends by itself",
                (unsigned)g_game->addr.runner_ret);
        else
            LOG("update runner: this profile does not record where the game's runner ends; the pass ends by itself");
    }
}
