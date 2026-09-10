/* ------------------------------------------------------------------ update runner replacement */
typedef int (__thiscall *NodeFn)(void* arg);
struct ListNode { struct UpdateFunc* entry; struct ListNode* next; struct ListNode* prev; };
struct UpdateFunc { int priority; uint32_t flags; NodeFn func; void* on_reg; NodeFn on_cleanup; struct ListNode node; void* arg; };
_Static_assert(__builtin_offsetof(struct UpdateFunc, arg) == 0x20, "UpdateFunc layout");
typedef void (__fastcall *RemoveNodeFn)(struct UpdateFunc* uf, uint8_t* runner);
#define game_remove_node ((RemoveNodeFn)g_game->addr.remove_node)

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
        if (g_major) { g_stop_node = NULL; g_frame_active = 0; }
        else subtick_input_begin();
    }
    crit_enter();
    struct ListNode* n = *(struct ListNode**)(runner + 0x18);
restart:
    while (n) {
        struct UpdateFunc* uf = n->entry;
        n = n->next;
        if (!uf->func) continue;
        if (!(uf->flags & 2)) { count++; continue; }
    call_again:
        if (g_game->layout.runner_ending && *(int*)(runner + g_game->layout.runner_ending) != 0) {
            if (uf->on_cleanup) uf->on_cleanup(uf->arg);
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
        int r = uf->func(uf->arg);
        if (player_minor) {
            input_write(g_game->addr.game_pressed, saved_pressed);
            input_write(g_game->addr.game_released, saved_released);
            set_game_input(G_GAME_INPUT | saved_bomb);
        }
        if (fn == g_game->addr.player_callback) { uint8_t* pl = *(uint8_t**)g_game->addr.player; if (pl) { g_ptf_prev = g_ptf_cur; g_ptf_cur = *(float*)(pl + g_game->layout.player_timer); } }
        if (replay_node) { uint8_t* rm = G_REPLAY_MANAGER; if (rm && *(int*)(rm + g_game->layout.replay_frame) != frame_before) g_frame_active = 1; }
        crit_enter();
        switch (r) {
        case 0: game_remove_node(uf, runner); count++; break;
        case 2: if (uf->flags & 2) goto call_again; count++; break;
        case 3: if (g_major) g_stop_node = uf; count = 1; goto done;
        case 4: count = 0; goto done;
        case 8: if (g_game->runner_return8_ends) { count = 0; goto done; } count++; break;
        case 5: count = -1; goto done;
        case 6: n = *(struct ListNode**)(runner + 0x18); count = 0; goto restart;
        case 7: if (uf->on_cleanup) uf->on_cleanup(uf->arg); count++; break;
        default: count++; break;
        }
    }
done:
    crit_leave();
    set_factor(1.0f);
    if (is_update) { subtick_input_end(); enemy_interp(g_phase); }
    return count;
}
__asm__(
    ".intel_syntax noprefix\n.globl _hfr_runner_entry\n_hfr_runner_entry:\n"
    "  push ebx\n  call _hfr_runner\n  add esp, 4\n  ret\n"
    ".att_syntax\n"
);
extern void hfr_runner_entry(void);
static int __stdcall hfr_runner_stack_entry(uint8_t* runner) { return hfr_runner(runner); }
