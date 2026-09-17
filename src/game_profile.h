/* One profile per executable layout; shared code must not branch on game ID. */
#pragma once
#include "identity.h"
enum { MODE_FRAME = 0, MODE_SUB = 1, MAX_NODE_CLASSES = 32 };
struct node_class { uint32_t func; int mode; const char* name; };
enum SpeedOp { SPEED_ONE_PERM, SPEED_ONE_TEMP, SPEED_PAUSE_SET, SPEED_PAUSE_RESTORE, SPEED_ECL };
struct SpeedSite { uintptr_t addr; unsigned char size, op, pop_float; };
/* One dimming classification rule (game_profile.h `draw`): draws under callbacks with priority in
   [prio_lo, prio_hi], from VMs of the named ANM on the given layers running the given scripts,
   belong to `category`. The script range is for layers a game shares between things of
   different classes (TH12's UFOs are enemies drawn on enemy.anm's effects layer): the ANM's
   script list (`thanm -l`) gives the numbers. */
struct DimRule { int prio_lo, prio_hi; const char* anm; int layer_lo, layer_hi; int script_lo, script_hi; int category; };
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
        /* The instruction that ends `runner_fn` -- its `ret`, or `ret 4` where the runner takes
           its argument on the stack. The replacement runner finishes by jumping to it rather
           than returning on its own, so that the last instruction of the function still belongs
           to the game and anything hooked there still runs (update_runner.c says why this
           matters). Never written, only jumped to; 0 means the replacement returns by itself. */
        uintptr_t runner_ret;
        uintptr_t screenshot_fn, screenshot_call;   /* 0 when not known for this game */
        uintptr_t data_dir;          /* NUL-terminated directory the game saves into, or 0 for the game directory */
    } addr;
    struct {
        uint32_t replay_stage;
        uint32_t replay_frame;
        uint32_t replay_stages;
        uint32_t player_timer;
        uint32_t enemy_flags;
        uint32_t enemy_position;
        uint32_t enemy_skip_mask;
        uint32_t enemy_list;        /* list head in the EnemyManager; 0 = +0x68 (TH10..TH12) */
        uint32_t runner_ending;     /* optional field; TH10's runner ends at +0x48 */
        uint32_t gm_pause_flags;
        uint32_t input_size;        /* bytes saved around a sub-tick poll */
        uint32_t input_width;       /* 2 or 4 bytes per input word */
        uint32_t focus_mask;        /* zero selects the later engines' 0x08 */
        uint32_t node_arg;          /* the UpdateFunc's argument slot; zero selects +0x20 (TH13: +0x24) */
        uint32_t runner_next;       /* runner field that holds the next list node during the walk, re-read
                                       after every callback; zero: the walk keeps its own (TH13: +0x50) */
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
    /* How the game hands the update runner its object, and how the frame callbacks and the
       screenshot routine take their arguments. These are not cosmetic: the entry thunk, the
       frame shim and the screenshot stub are each written for one of them, and picking the
       wrong one unbalances the stack on the first frame. TH14 changed all three at once
       (see docs/games/TH14_DEVNOTES.md), which is why they are named rather than assumed. */
    enum RunnerArg { RUNNER_ARG_EBX = 0, RUNNER_ARG_STACK, RUNNER_ARG_ECX } runner_arg;
    /* The three frame callbacks take their context in ECX (thiscall) rather than pushed. */
    int frame_ctx_ecx;
    /* The screenshot routine takes the filename pushed (stdcall) rather than in EAX. */
    int screenshot_stack_arg;
    /* The end-of-pass cleanup takes its object in ECX (thiscall) rather than ESI. */
    int cleanup_this_ecx;
    /* What the game stores in `frame_flag` on the path the frame function takes, which the
       catch-up tick has to reproduce. Zero selects 1, which is what TH10-13 store; TH14
       stores 2. Getting this wrong does not crash -- it tells the engine it is running in a
       context it is not, which is the kind of wrong that shows up as a scene misbehaving
       three menus later. */
    uint32_t frame_flag_value;
    int runner_return8_ends;
    int native_size_cycle;          /* the game cycles its own window sizes on F10 (TH11 on) */
    /* How remove_node is called. TH10-12 pass (node, runner) in ECX/EDX, TH13 swapped them,
       and TH14 made it a method: the runner in ECX and the node pushed. */
    enum RemoveNodeAbi { REMOVE_NODE_NODE_FIRST = 0, REMOVE_NODE_RUNNER_FIRST,
                         REMOVE_NODE_RUNNER_THIS } remove_node_abi;
    const char* d3dx;
    /* Two-byte "frndint" sites in the sprite quad builder that snap every corner to a whole pixel;
       NOPed when the game draws at a higher internal resolution (video.internal_scale). */
    const uintptr_t* sprite_round_sites; size_t sprite_round_count;
    /* Dimming (video.dim_*, dimming.c). Every object draws from the game's draw list, one callback
       per node in priority order, and the callbacks do not touch Direct3D directly: sprites go
       through the sprite manager's batch, flushed whenever the texture or blend changes. So the
       draw runner's dispatch (the instructions that load a node's argument and callback and call
       it) is wrapped: it records the node's priority for the Direct3D hooks and flushes the batch
       before and after, so every draw call belongs to exactly one callback; and the sprite VM draw
       is wrapped so that a batch never mixes VMs of different classes.
       `dispatch` is those instructions (position-independent, `dispatch_len` bytes), `node_reg`
       holds the node whose priority sits at +prio_off. `flush_fn` is the batch flush, taking the
       sprite manager (read from the pointer at `flush_this`) in `flush_reg`. `vm_draw` draws one
       VM (in `vm_reg`; the first `vm_draw_len` bytes are carried); the VM holds a pointer to its
       loaded ANM (slot index, then the file name) at +vm_anm_off, its sprite layer at
       +vm_layer_off and its script index (16-bit) at +vm_script_off (0: unknown, script rules
       never match). Draws before the first callback of priority `world_prio` are background;
       `rules` classify the rest (first match wins; anm NULL matches any VM and non-VM draws too,
       an anm pattern may end in '*'; -1 bounds are open). dispatch == 0: dimming unavailable. */
    struct {
        uintptr_t dispatch; unsigned char dispatch_len, node_reg; uint32_t prio_off;
        uintptr_t flush_fn; unsigned char flush_reg; uintptr_t flush_this;
        uintptr_t vm_draw; unsigned char vm_draw_len, vm_reg; uint32_t vm_anm_off, vm_layer_off, vm_script_off;
        /* TH14 on: the VM is the draw's first stack argument rather than arriving in a
           register, so `vm_reg` says nothing and the wrap reads it off the stack instead. */
        int vm_stack_arg;
        int world_prio;
        const struct DimRule* rules; size_t rule_count;
        const char* special_name;     /* what DIM_SPECIAL fades in this game, for the menu; NULL = nothing */
    } draw;
    void (*install_sites)(void);
    void (*place_enemy)(uint8_t* enemy, uint8_t* anm, uint32_t flags, const float* position);
};
static const struct GameProfile* g_game;
#define g_classes (g_game->classes)
#define g_class_count (g_game->class_count)
