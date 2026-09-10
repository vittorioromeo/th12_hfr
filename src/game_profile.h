/* One profile per executable layout; shared code must not branch on game ID. */
#pragma once
#include "identity.h"
enum { MODE_FRAME = 0, MODE_SUB = 1, MAX_NODE_CLASSES = 32 };
struct node_class { uint32_t func; int mode; const char* name; };
enum SpeedOp { SPEED_ONE_PERM, SPEED_ONE_TEMP, SPEED_PAUSE_SET, SPEED_PAUSE_RESTORE, SPEED_ECL };
struct SpeedSite { uintptr_t addr; unsigned char size, op, pop_float; };
struct GameProfile {
    const struct GameIdentity* identity;
    struct {
        uintptr_t speed;
        uintptr_t update_runner;
        uintptr_t device;
        uintptr_t pp;
        uintptr_t window_flags;
        uintptr_t frame_time;
        uintptr_t misc_flags;
        uintptr_t raw_input;
        uintptr_t raw_pressed;
        uintptr_t replay_manager;
        uintptr_t frame_fn;
        uintptr_t enemy_manager;
        uintptr_t anm_manager;
        uintptr_t anm_get_vm;
        uintptr_t game_input;
        uintptr_t option_flags;
        uintptr_t autofocus;
        uintptr_t poll_input;
        uintptr_t remove_node;
        uintptr_t crit;
        uintptr_t crit_count;
        uintptr_t gm_callback;
        uintptr_t game_manager;
        uintptr_t record_callback;
        uintptr_t playback_callback;
        uintptr_t player_callback;
        uintptr_t game_pressed;
        uintptr_t game_released;
        uintptr_t player;
        uintptr_t frame_context_ptr;
        uintptr_t frame_flag;
        uintptr_t frame_context_value;
        uintptr_t cleanup_fn;
        uintptr_t cleanup_this;
        uintptr_t replay_save;
        uintptr_t replay_load;
        uintptr_t frame_calls[3], replay_saves[4], replay_load_call, runner_fn, latency_cmp;
        uintptr_t screenshot_fn, screenshot_call;   /* 0 when not known for this game */
    } addr;
    struct {
        uint32_t replay_stage;
        uint32_t replay_frame;
        uint32_t replay_stages;
        uint32_t player_timer;
        uint32_t enemy_flags;
        uint32_t enemy_position;
        uint32_t enemy_skip_mask;
        uint32_t runner_ending;     /* optional field; TH10's runner ends at +0x48 */
        uint32_t gm_pause_flags;
        uint32_t input_size;        /* bytes saved around a sub-tick poll */
        uint32_t input_width;       /* 2 or 4 bytes per input word */
        uint32_t focus_mask;        /* zero selects the later engines' 0x08 */
    } layout;
    /* Instruction shape and semantics are independent: TH10 mostly uses MOV,
       while later engines use FSTP. Only FSTP sites consume an x87 value. */
    const struct SpeedSite* speed_sites;
    size_t speed_site_count;
    const struct node_class* classes;
    size_t class_count;
    /* Set while a game is still being worked out. The patch identifies it, logs what it knows,
       and then leaves it completely alone -- no hooks at all -- because a game that installs
       and then faults is worse than one the patch does not claim to support. */
    int provisional;
    int mask_minor_player_edges;
    uint32_t critical_flag_mask;    /* zero: the runner always locks */
    int runner_stack_arg;
    int runner_return8_ends;
    const char* d3dx;
    void (*install_sites)(void);
    void (*place_enemy)(uint8_t* enemy, uint8_t* anm, uint32_t flags, const float* position);
};
static const struct GameProfile* g_game;
#define g_classes (g_game->classes)
#define g_class_count (g_game->class_count)
