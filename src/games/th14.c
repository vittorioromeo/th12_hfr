/* TH14 v1.00b, Double Dealing Character. The engine is TH13's, rebuilt with a newer compiler,
   and three of its calling conventions changed with it -- see docs/games/TH14_DEVNOTES.md:

     - the update runner takes its object in ECX rather than EBX,
     - the three window-manager frame functions are thiscall rather than stdcall,
     - remove_node is a method: the runner in ECX and the node pushed,
     - and the screenshot routine takes its filename pushed rather than in EAX.

   Each of those is a named field rather than an assumption, because this is the first game
   where they differ and TH15 on will differ again.

   What is described so far: identification, the whole video path, and the scheduler -- so the
   game runs at the display's rate with its simulation at 60 Hz. What is not: the UpdateFunc
   class table and the dimming map, both of which are read off a running game rather than out
   of the executable (DEVNOTES_RUNTIME 3b), and the per-frame hooks that sub-stepping needs.
   An empty class table means node_mode answers MODE_FRAME for every callback, so nothing is
   sub-stepped and nothing is mis-stepped; the menu offers no sub-step switches for this game
   and says why. */
#include "../game_profile.h"

static const struct GameProfile th14_profile = {
    .identity = &game_identities[GI_TH14],
    .addr = {
        .device = 0x4d8f68,
        .window_flags = 0x4f7a54,
        .misc_flags = 0x4f5815,          /* a byte: whether the runner locks */
        .raw_input = 0x4d6878,
        .raw_pressed = 0x4d6884,
        .replay_manager = 0x4db688,
        /* The sprite/ANM manager, which is also what the batch flush takes. */
        .anm_manager = 0x4f56cc,
        /* The replay nodes, from the draw trace's pairing and from where OpenInputLagPatch
           puts its replay speed-control patch (0x455e82, inside the playback one). */
        .record_callback = 0x455e40, .playback_callback = 0x455e60,
        .update_runner = 0x4db51c,
        .frame_fn = 0x46a950,
        .remove_node = 0x401630,
        .crit = 0x4f56d0,
        .crit_count = 0x4f5808,
        /* The frame function's own "this is a frame pass" context, which the catch-up tick
           reproduces: `mov [0x4d9640], 0x4d93e8` at 0x46a965 and `mov [0x4d9644], 2` at
           0x46a994, the pair TH13 writes as 0x4dcc18/0x4dc9d0 and 1. */
        .frame_context_ptr = 0x4d9640, .frame_flag = 0x4d9644, .frame_context_value = 0x4d93e8,
        /* ... and what it calls when the pass says stop, at 0x46a9a7 and 0x46a9bf. */
        .cleanup_fn = 0x403bb0, .cleanup_this = 0x4d98ec,
        .frame_calls = {0x469a27, 0x469a45, 0x469a51},
        .runner_fn = 0x401280, .runner_ret = 0x40138a,
        .latency_cmp = 0x46aa80,
        .screenshot_fn = 0x445000, .screenshot_call = 0x46abf6,
    },
    .layout = {
        .node_arg = 0x24,        /* as TH13 */
        .runner_next = 0x50,     /* as TH13: the runner keeps the walk's next node in itself */
        .input_width = 4,
    },
    .critical_flag_mask = 0xff,
    .runner_return8_ends = 1,
    .runner_arg = RUNNER_ARG_ECX,
    .frame_ctx_ecx = 1,
    .screenshot_stack_arg = 1,
    .cleanup_this_ecx = 1,
    .frame_flag_value = 2,
    .remove_node_abi = REMOVE_NODE_RUNNER_THIS,
    .native_size_cycle = 1,
    /* The draw path, as far as it is read so far. The dispatch is the draw runner's own
       `mov ecx,[edi+0x24]; mov eax,[edi+8]; call eax` at 0x40141a, the same three instructions
       TH13 has at 0x470c9e with the node in ESI instead. The flush and its manager come off
       the frame function's first act, which is where TH13's were read from too.

       `world_prio` is deliberately left at zero: which priority the world starts at is read
       off a running game, not out of the executable, and a wrong one fades the wrong half of
       the screen. Zero means the menu reports dimming as unavailable -- but the dispatch is
       still wrapped, which is what makes the debug draw trace run, which is what will supply
       the priority and the rules. The sprite VM draw is not described yet either, so the
       per-VM rules stay inert; dimming.c already treats both as optional. */
    .draw = { .dispatch = 0x40141a, .dispatch_len = 8, .node_reg = R_EDI, .prio_off = 0,
              .flush_fn = 0x475eb0, .flush_reg = R_ECX, .flush_this = 0x4f56cc,
              .world_prio = 0, .rules = NULL, .rule_count = 0 },
    .d3dx = "d3dx9_43.dll",
};
