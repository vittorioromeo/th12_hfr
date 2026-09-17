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

/* Dimming (DEVNOTES_RUNTIME 3b, TH14_DEVNOTES 12). Written from a stage trace, and
   deliberately short: every line below is something the trace actually showed, and the classes
   it cannot yet place are left unclaimed rather than guessed at. `hfr_ui_get(UI_DIM_CLASSES)`
   reports which classes a profile really has, so the menu offers these and not the rest.

     prio 2,3,5,6   st01wl.anm, title.anm, front.anm -- the stage, drawn first
     prio 9         effect.anm layer 2 -- drawn under the world, as TH13's layer 2 is at its 8
     prio 19        enemy.anm layer 8 -- the first world object, so the world starts here
     prio 27..30    pl00.anm layers 13..15 -- the player, its shots and its focus ring
     prio 31, 35    bullet.anm -- lasers and bullets
     prio 42,43     effect.anm layers 20,21 -- effects over the world

   Not placed yet: items (none were on screen in the traced frames, and TH14 has no item.anm,
   so which ANM and layer they use is still unread), and which of pl00.anm's layers is the
   hitbox as against the shots -- so the whole of pl00.anm is left unfaded, which is the
   conservative half of TH13's split. */
static const struct DimRule th14_dim_rules[] = {
    { 31, 35, "bullet.anm",  -1, -1, -1, -1, DIM_NONE },       /* bullets are what the rest is faded for */
    {  9,  9, "effect.anm",   2,  2, -1, -1, DIM_EFFECTS },    /* under the world */
    /* The focus ring and the hitbox are drawn from effect.anm, not from pl00.anm, on the
       player's own layer and inside the player's priority band -- the trace shows exactly one
       of them, layer 14 at priority 29, between pl00.anm's layers 13 and 15. TH13 has the same
       carve-out (`effect.anm` layer 12, inside its 12..43 world band) and it was dropped when
       these rules were written, so the hitbox faded with the effects. The band is widened to
       the player's three layers rather than pinned to the one the trace caught: anything
       effect.anm draws inside the player's own band is the player's furniture, and the cost of
       being wrong that way is an effect near the player that does not fade, against a hitbox
       that disappears when you need it. */
    { 27, 30, "effect.anm",  13, 15, -1, -1, DIM_NONE },       /* the focus ring and the hitbox */
    { -1, -1, "pl*.anm",     -1, -1, -1, -1, DIM_NONE },       /* the player and its shots */
    { -1, -1, "effect.anm",  -1, -1, -1, -1, DIM_EFFECTS },
};

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
              .world_prio = 19, .rules = th14_dim_rules,
              .rule_count = sizeof th14_dim_rules / sizeof *th14_dim_rules,
              /* The per-VM draw, so the trace can say which ANM and layer each sprite came
                 from. The VM is this function's first stack argument here, where TH10-13
                 pass it in a register -- the fifth convention this rebuild changed.
                 The three field offsets are deliberately zero: the debug trace scans the VM's
                 words for a pointer to a loaded ANM record and prints the offset it finds, so
                 they get read off a running game rather than guessed. Until they are set, no
                 VM is classified and no rule can match the wrong thing. */
              .vm_draw = 0x478f60, .vm_draw_len = 9, .vm_stack_arg = 1,
              /* Read off a running game: every VM's loaded-ANM pointer is at +0x30 and its
                 sprite layer at +0x24, the same two places TH13 keeps them, which the trace
                 found rather than TH13's numbers being assumed. The script index is left
                 unknown -- no rule here needs one, and the dump the trace prints stops just
                 short of where TH13's sits. */
              .vm_anm_off = 0x30, .vm_layer_off = 0x24, .vm_script_off = 0 },
    .d3dx = "d3dx9_43.dll",
};
