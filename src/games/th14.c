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
        .update_runner = 0x4db51c,
        .frame_fn = 0x46a950,
        .remove_node = 0x401630,
        .crit = 0x4f56d0,
        .crit_count = 0x4f5808,
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
    .remove_node_abi = REMOVE_NODE_RUNNER_THIS,
    .native_size_cycle = 1,
    .d3dx = "d3dx9_43.dll",
};
