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
/* The game's own writes to the game speed (`addr.speed`). Sub-stepping *is* the game speed --
   the runtime stores `logical * factor` there -- so every one of the game's stores has to say
   what it meant in game-frame units, or the next store puts the game back to one frame per
   tick and the sub-steps become six frames of motion. There are twenty-five stores. Twelve are
   described here; the other thirteen are correct untouched, and § of the dev notes says why
   each one is. The short version: a store of a value *derived from the current speed* composes
   with the factor by itself, and so does a store of zero.

   The save/set-1.0/restore triples are `SPEED_ONE_TEMP` on the set only. The restore writes
   back the raw global the game saved, which is the scaled value, so patching it would be
   wrong -- `SPEED_PAUSE_SET`/`_RESTORE` are for the games that restore from a value the
   runtime has to reconstruct. */
static const struct SpeedSite th14_speed_sites[] = {
    /* stores 1.0 for good: the new logical speed really is 1.0 */
    {0x40747a, 10, SPEED_ONE_PERM, SPEED_SRC_NONE},   /* the speed object's initialiser */
    {0x435b44, 10, SPEED_ONE_PERM, SPEED_SRC_NONE},   /* game state change */
    {0x436100, 10, SPEED_ONE_PERM, SPEED_SRC_NONE},   /* supervisor reset */
    {0x444a00, 10, SPEED_ONE_PERM, SPEED_SRC_NONE},   /* player reset */
    {0x44df30, 10, SPEED_ONE_PERM, SPEED_SRC_NONE},   /* end-of-stage sequence */
    /* stores 1.0 for the duration of something, and the game restores the raw value after */
    {0x40da9a, 10, SPEED_ONE_TEMP, SPEED_SRC_NONE},   /* around three slow-motion updates; restored 0x40dac5 */
    {0x448eb6, 10, SPEED_ONE_TEMP, SPEED_SRC_NONE},   /* pause menu opens; restored 0x449097 */
    {0x448ff8, 10, SPEED_ONE_TEMP, SPEED_SRC_NONE},   /* pause menu opens; restored 0x44a165 */
    {0x449eff, 10, SPEED_ONE_TEMP, SPEED_SRC_NONE},   /* pause menu opens; restored 0x44af39 */
    {0x44a0b5, 10, SPEED_ONE_TEMP, SPEED_SRC_NONE},   /* pause menu opens; restored 0x44b0e5 */
    {0x46fe8a, 10, SPEED_ONE_TEMP, SPEED_SRC_NONE},   /* a sprite flagged "unaffected by slow-motion" */
    /* the script instruction that sets the game speed; the value is in xmm0 */
    {0x429796,  8, SPEED_ECL,      SPEED_SRC_XMM0},
};

static const struct DimRule th14_dim_rules[] = {
    /* Items. The manager's update (census priority 24, body at 0x438550) accumulates +0.2 a
       frame into a per-entity field and scales the rest by the game speed at 0x4d8f58 -- which
       is TH13's item fall model instruction for instruction -- and its draw at priority 31 is
       a separate bullet.anm stream from the bullet manager's at 35. TH14's items are drawn
       from the bullet texture, which is why three stages of looking for an item ANM found
       nothing. Matched on priority with no ANM, exactly as TH13's item rule is. */
    { 31, 31, NULL,          -1, -1, -1, -1, DIM_ITEMS },
    { 35, 35, "bullet.anm",  -1, -1, -1, -1, DIM_NONE },       /* bullets are what the rest is faded for */
    /* There was a rule here calling bullet.anm on layers 20 and 21 the items. The experiment
       it was: dim_items faded nothing, and a stage played with items everywhere produced no
       such row at all, so those draws are something else that happened to be on screen the
       one time -- bullet cancels, most likely, but nothing here has established that either.
       Removed rather than reassigned. Items turn out not to go through the sprite VM draw at
       all, which is why the VM census could never have shown them; the callback census added
       alongside this is what will. */
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
    /* The player's own layers, in TH13's shape: carve out what is her, fade what she fires,
       and leave anything else of hers alone. The census separates them by how often they draw
       against the number of frames -- her body's own callback at priority 28 draws about once
       a frame (5054 draws), while layer 13 at priority 27 and layer 15 at priority 30 draw
       fifteen and thirteen times a frame, which is a screenful of shots and not a character.
       Layer 14 is the once-a-frame one and stays unfaded, with the focus ring above. */
    { -1, -1, "pl*.anm",     14, 14, -1, -1, DIM_NONE },       /* the player herself */
    { -1, -1, "pl*.anm",     13, 15, -1, -1, DIM_PLAYER_SHOTS },
    { -1, -1, "pl*.anm",     -1, -1, -1, -1, DIM_NONE },       /* anything else of hers */
    { -1, -1, "effect.anm",  -1, -1, -1, -1, DIM_EFFECTS },
};

/* Which of the update list's callbacks may run more than once a frame.

   Only the two sprite-manager passes do, and that is a deliberate stopping point rather than a
   first instalment. A sprite VM interpolates its own position, scale, colour and rotation
   between the keyframes its script sets, in units of the game speed -- `0x473139` onwards is a
   run of `mulss xmm0,[0x4d8f58]`, and half a dozen sites store the speed's *address* into an
   interpolator field -- so running a pass six times with the speed at a sixth gives the
   interpolation six times the resolution and changes nothing else: no entity moved, no timer
   advanced, no script instruction ran early. That is the menu, the HUD, the title screen, the
   dialogue portraits and the spell-card plate at the display's rate.

   Everything else stays MODE_FRAME. A gameplay system sub-stepped without the per-frame hooks
   that keep its own counters in whole frames is DEVNOTES_RUNTIME § 7: it looks smoother and is
   quietly wrong, and the wrongness is in hit detection. The identification below is good
   enough to name BulletManager, and naming it is not the same as being ready to step it.

   The two passes are the pair registered from one function at 0x47a780 (`0x47aa5b` priority
   29, `0x47aac8` priority 8), and they are two walks over two lists of one manager object --
   the same `this`, the same body, the same `call 0x46fe50` per VM, over `[this+0xfe8208]` and
   `[this+0xfe8210]`. TH13 calls its pair world and UI; which of these two is which is not
   established here, so they are named for when they run. The late one is reached through a
   gate (`0x47e7c0`) that skips it on a flag in the supervisor, which is the shape of the pass
   that stops when the game does -- a signal, not a reading. Both are MODE_SUB either way, so
   the labels are labels.

   Systems that step their own VMs by calling 0x46fe50 directly from a MODE_FRAME callback are
   unaffected and still advance once a frame, which is what keeps this consistent. */
static const struct node_class th14_classes[] = {
    { 0x47e7f0, MODE_SUB,   "AnmSpritesEarly" },   /* priority 8  -> 0x47e6c0 */
    { 0x47e7c0, MODE_SUB,   "AnmSpritesLate"  },   /* priority 29 -> 0x47e5e0, behind a gate */
    /* The rest, named where the dev notes could name them, so that the census has somewhere to
       report against and a later change is an edit to one line rather than a new table. */
    { 0x417610, MODE_FRAME, "BulletManager"   },
    { 0x43a6a0, MODE_FRAME, "LaserManager?"   },
    { 0x40b8e0, MODE_FRAME, "Ascii"           },
    { 0x459f30, MODE_FRAME, "Title"           },
    { 0x431a40, MODE_FRAME, "Front"           },
    { 0x41ee80, MODE_FRAME, "Effects?"        },
    { 0x444890, MODE_FRAME, "Update01"        },
    { 0x4447b0, MODE_FRAME, "Update03"        },
    { 0x448bd0, MODE_FRAME, "Update09"        },
    { 0x436d70, MODE_FRAME, "Update11"        },
    { 0x455e40, MODE_FRAME, "ReplayRecord"    },
    { 0x40eb70, MODE_FRAME, "Update13"        },
    { 0x457ee0, MODE_FRAME, "Update17"        },
    { 0x44ec60, MODE_FRAME, "Player"          },
    { 0x411eb0, MODE_FRAME, "Update20"        },
    { 0x422a60, MODE_FRAME, "Update21"        },
    { 0x439750, MODE_FRAME, "ItemManager"     },
    { 0x41cb50, MODE_FRAME, "Update26"        },
    { 0x455e60, MODE_FRAME, "ReplayPlayback"  },
};

static const struct GameProfile th14_profile = {
    .identity = &game_identities[GI_TH14],
    .addr = {
        /* The game speed, which is also the sub-step factor: `th14_speed_sites` below is the
           other half of describing it, and the two are only correct together. */
        .speed = 0x4d8f58,
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
    .speed_sites = th14_speed_sites, .speed_site_count = sizeof th14_speed_sites / sizeof *th14_speed_sites,
    .classes = th14_classes, .class_count = sizeof th14_classes / sizeof *th14_classes,
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
