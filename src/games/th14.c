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

/* ------------------------------------------------------------------ per-frame hooks

   A system may only be MODE_SUB once everything it counts in whole frames still counts in
   whole frames when its update runs six times a frame. TH14 makes that far smaller a job than
   TH13 did, and the reason is worth stating once: *this engine's timers carry a rate pointer*.
   A timer is {prev, int, float, const float* rate} and its tick is "float += rate ? *rate : 1,
   int = (int)float, prev = the int before that" -- and the constructors store `&0x4d8f58`, the
   game speed, into that rate field in a hundred and ninety-nine places. So under sub-stepping
   every one of those timers advances by a sixth of a frame per tick all by itself, and its
   integer crosses a whole number on exactly one tick per frame. TH13's three hundred lines of
   hooks were mostly for timers that had no such pointer.

   What is left is the three things a rate pointer cannot fix:
     - an integer counter decremented by the update itself rather than by a timer,
     - a block gated on "the timer's integer *is* N", which stays true for a whole frame and so
       runs on every tick of it rather than once,
     - anything that consumes the replay RNG.
   Each hook below is one of those, and each is gated on the same question: did this tick cross
   a whole frame? The bullet's own timer answers it -- prev at +0x13c0 against int at +0x13c4,
   which the manager ticks after every update (0x4171b7). */
static void th14_install_sites(void) {
    g_p = stub_begin();

    /* --- Bullet (0x416700, ESI = bullet): the wait counter [+0x24]-- inside the behaviour
           loop, and the same counter plus the collision countdown [+0xbfc]-- at the end of the
           update. Three raw decrements per call, none of them through a timer. The copied
           bytes keep their own short jumps: each one skips exactly the instructions that follow
           it in the copy, and what follows the copy is the jump back, which is where they
           landed before. --- */
    STUB_BEGIN();
    E_timer_unchanged(R_ESI, 0x13c0, 0x13c4); EJCC(0x84, 0x416a85);
    ECOPY(0x416a7a, 11); EJMP(0x416a85); site_hook(0x416a7a, 11);

    STUB_BEGIN();
    E_timer_unchanged(R_ESI, 0x13c0, 0x13c4); EJCC(0x84, 0x416d25);
    ECOPY(0x416d09, 28); EJMP(0x416d25); site_hook(0x416d09, 28);

    /* --- Bullet state 5 (the cancel animation, jump-table arm at 0x416d6c): on the frame where
           the timer's integer *is* 3, and only then, it spawns the cancel effect -- `cmp eax,3`
           with a `jne` past the whole block. The integer sits at 3 for a frame, so six ticks a
           frame is six effects and six draws on the RNG. Keep the game's two branches and add
           the third: the integer has to have changed on this tick. --- */
    STUB_BEGIN();
    E(0x8b, 0x86); E32(0x13c4);                     /* mov eax,[esi+0x13c4] */
    E(0x83, 0xf8, 0x03);                            /* cmp eax,3 */
    EJCC(0x8c, 0x416c40);                           /* jl  : too early, no motion for this arm */
    EJCC(0x85, 0x416bd9);                           /* jne : past it, motion only */
    E_timer_unchanged(R_ESI, 0x13c0, 0x13c4);
    EJCC(0x84, 0x416bd9);                           /* the same frame again: motion only */
    EJMP(0x416b24); site_hook(0x416b0f, 21);

    /* --- Items (0x438550 behind the callback 0x439750, EDI = item, stride 0xc18, timer prev
           +0xbc8 / integer +0xbcc / float +0xbd0 / rate +0xbd4, ticked in the per-item tail at
           0x438d0c). Everything about an item's motion is already right: the velocity is
           `pos += vel * speed` (0x4386b6) and even the gravity is `speed * 0.2` (0x438716), so
           an item falls at the same rate however often it is stepped. What is not right is the
           two integer countdowns the update decrements itself -- the despawn countdown in state
           5, and the "wait, then start falling" countdown in state 1. Both are gated on the
           item's own timer having crossed a whole frame.

           The second keeps the game's own "already expired" branch ahead of the gate, so an item
           that is falling keeps being updated every tick; it is only the counting that is once a
           frame. --- */
    STUB_BEGIN();
    E_timer_unchanged(R_EDI, 0xbc8, 0xbcc); EJCC(0x84, 0x438d96);
    E(0xff, 0x8f); E32(0xc00);                      /* dec dword [edi+0xc00] */
    EJCC(0x89, 0x438d96);                           /* jns: still waiting */
    EJMP(0x4385cf); site_hook(0x4385c3, 12);

    STUB_BEGIN();
    E(0x8b, 0x87); E32(0xc00);                      /* mov eax,[edi+0xc00] */
    E(0x85, 0xc0);                                  /* test eax,eax */
    EJCC(0x8e, 0x43865c);                           /* jle: expired, the normal path every tick */
    E_timer_unchanged(R_EDI, 0xbc8, 0xbcc); EJCC(0x84, 0x438d96);
    E(0x48);                                        /* dec eax */
    E(0x89, 0x87); E32(0xc00);                      /* mov [edi+0xc00],eax */
    E(0x85, 0xc0);                                  /* test eax,eax */
    EJCC(0x8f, 0x438d96);                           /* jg: still waiting */
    EJMP(0x43864a); site_hook(0x438631, 25);

    /* --- Player (0x44dbd0 behind the callback 0x44ec60, EDI = player). The life-state machine
           dispatches on [+0x684] through a table at 0x44ebf4: state 1 is alive and is the only
           one whose body belongs at the display's rate. The other four are the death, respawn
           and stage-clear sequences -- they count whole frames, spawn effects on exact frame
           numbers and draw on the RNG, and every one of them would do it six times. Rather than
           gate each, gate the dispatch: on a tick that is not a frame boundary, any state but 1
           goes straight to the update's tail, which is where the sprite VMs are stepped and so
           still runs every tick. --- */
    STUB_BEGIN();
    E(0x83, 0xf8, 0x01);                            /* cmp eax,1 */
    E(0x74, 0x0d);                                  /* je: alive, always dispatch */
    E_not_major(); EJCC(0x84, 0x44dfd1);            /* minor tick: skip to the tail */
    E(0xff, 0x24, 0x85); E32(0x44ebf4);             /* jmp [eax*4 + table] */
    site_hook(0x44dbf8, 7);

    /* --- The player's position is fixed point at [+0x5ec]/[+0x5f0], 1/128 of a pixel, advanced
           by the truncation of a velocity the game has already multiplied by the game speed
           (0x44d72f, 0x44d746). Carry the truncation residual across sub-steps, or six sixths
           of a velocity add up to less than one whole. --- */
    movement_cvttss(0x44d774, 0x8f, 0x604, R_ECX, 0);
    movement_cvttss(0x44d77c, 0x87, 0x608, R_EAX, 1);

    /* --- Invincibility blink: "the state timer's integer changed, and is a multiple of 3" ->
           flash (0x44e16f). The game asks the question with its own prev/int pair, which under
           sub-stepping is true on exactly one tick of the frame, so the flash would be set for a
           sixth of a frame and cleared for the rest. Ask it of the timer's float instead, before
           and after this tick's Player call, which is what g_ptf_prev/g_ptf_cur are: that is
           true on every tick, and the "multiple of 3" then holds for a whole frame, as it did.
           TH13 replaces the identical guard at 0x446888 for the same reason. --- */
    STUB_BEGIN();
    E(0x50, 0xa1); E32((uint32_t)(uintptr_t)&g_ptf_prev);
    E(0x3b, 0x05); E32((uint32_t)(uintptr_t)&g_ptf_cur); E(0x58);
    EJCC(0x84, 0x44e1a5);                           /* unchanged: the game's own je target */
    E(0x8b, 0x87); E32(0x690);                      /* mov eax,[edi+0x690] -- what follows divides it */
    EJMP(0x44e17d); site_hook(0x44e16f, 14);

    /* --- The focus counter [+0x1830c]++ once per frame (0x44d924); the option-gather laser
           reads it against 30 at 0x44da0d. --- */
    gate_block(0x44d924, 6, 0x44d92a, R_EDI, 0x68c);

    /* --- The options chase the player by a fixed proportion of the remaining distance each
           frame (0x44d9aa: (target - pos) * [+0x182bc] / 100, with the blend at 30). An
           exponential approach run six times a frame is not the same approach: at 30% a tick the
           options would close 88% of the gap in a frame instead of 30%, and sit on the player
           instead of trailing her. Run the approach on frame boundaries only. The tail from
           0x44db3d, which puts the option's position into its sprite VM, still runs every tick.
           That leaves the options moving in 60 Hz steps while the player does not; making them
           smooth means interpolating between the two frame positions, the way `place_enemy`
           already does for enemies, and that is a separate piece of work. --- */
    STUB_BEGIN();
    E_not_major(); EJCC(0x84, 0x44db3d);            /* minor tick: no approach this tick */
    E(0x83, 0xf8, 0x1e);                            /* cmp eax,0x1e */
    EJCC(0x8c, 0x44db3d);                           /* jl: the game's own branch */
    EJMP(0x44d9aa); site_hook(0x44d9a1, 9);

    /* --- Player shots against enemies (0x451400, called by the enemy with its position and
           radius). Two guards in it, and both are the reason a sub-stepped player stops doing
           damage; TH13 has the same pair at 0x446888 and 0x4436b4.

           The first is at the top of the function: "the player's state timer integer did not
           change -> return 0". Sub-stepped, that integer changes on one tick of the six and the
           enemy code runs on the boundary tick, so on most rates it never changed on the tick
           that asked -- and nothing the player fired ever hit anything. Ask the question of the
           timer's float before and after this tick's Player call instead. --- */
    STUB_BEGIN();
    E(0x50, 0xa1); E32((uint32_t)(uintptr_t)&g_ptf_prev);
    E(0x3b, 0x05); E32((uint32_t)(uintptr_t)&g_ptf_cur); E(0x58);
    EJCC(0x85, 0x451463);                           /* changed: the game's own jne target */
    EJMP(0x451458);                                 /* unchanged: its fall-through, return 0 */
    site_hook(0x451450, 8);

    /* --- The second guard is per shot, inside that function's loop over the 256 shots at
           player+0xde28: "this shot's timer integer changed this tick, and is a multiple of the
           shot's interval" (0x4514ce). That is the shot's hit cadence in frames, and it is
           asked on the boundary tick, so the integer has to change on the boundary tick and
           nowhere else. The timer is ticked in the player's own tail (0x44e08d, a countdown by
           its rate pointer, which is the game speed); sub-stepped it crosses a whole number on
           whichever tick the float happens to reach one, and at 360 Hz, where dt is not exact in
           float32, that is never the boundary tick.

           So tick it by the logical speed on the boundary tick -- one whole frame, which is what
           the unmodified game does -- and leave it alone on the others, with prev already set to
           the integer so the guard reads "unchanged". The total per frame is the same, and the
           shot's lifetime is still the same number of frames. --- */
    STUB_BEGIN();
    E(0x8b, 0x43, 0xfc);                            /* mov eax,[ebx-4]  (the integer) */
    E(0x89, 0x43, 0xf8);                            /* mov [ebx-8],eax  (prev = integer) */
    E_not_major(); EJCC(0x84, 0x44e0dd);            /* minor tick: no countdown, eax is the int */
    E(0xb8); E32((uint32_t)(uintptr_t)&g_logical);  /* mov eax,&g_logical -- one frame's worth */
    EJMP(0x44e096); site_hook(0x44e08d, 13);

    /* --- The player's weapons (0x100 of them at player+0x6c8, walked by 0x451380, each updated
           by 0x4510b0 which calls its type's update and then ticks its own timer at 0x45131b).
           That timer's integer is what every weapon type's update asks "did it change, and is it
           a multiple of N" -- the cadence on which a weapon fires, retargets and drives the
           damage volume it owns in the shot array. It is the same rule as the shot timer one
           line above, and for the same reason: whatever the weapon does on that tick is read by
           the enemy once a frame, on the frame boundary, so the integer has to cross there.

           Advance it by the logical speed on the boundary tick and not at all on the others,
           writing the integer and the float back unchanged so nothing else notices. The weapon's
           own motion is a MotionState stepped every tick by 0x4510b0, so this does not make the
           broom or the options move in steps. --- */
    STUB_BEGIN();
    E(0x8b, 0x46, 0x1c);                            /* mov eax,[esi+0x1c]  (the integer) */
    E(0x89, 0x46, 0x18);                            /* mov [esi+0x18],eax  (prev = integer) */
    E_not_major(); E(0x75, 0x0a);                   /* boundary tick: advance */
    E(0xf3, 0x0f, 0x10, 0x46, 0x20);                /* movss xmm0,[esi+0x20] */
    EJMP(0x45135f);                                 /* ... and store both back unchanged */
    E(0xb9); E32((uint32_t)(uintptr_t)&g_logical);  /* mov ecx,&g_logical -- one frame's worth */
    EJMP(0x451328); site_hook(0x45131b, 13);

    /* --- Debug only: count what the shot-versus-enemy test does, because "the player fires and
           nothing dies" has four possible answers inside one function and guessing between them
           is how a build ships with the wrong one fixed. tests = enemies tested, shots = shot
           slots examined, notick = rejected because the shot's timer integer did not change,
           cadence = rejected on its interval, hit = reached the shape test, melee = of those,
           the ones with the swept shape, which is what a focus weapon's damage volume is. --- */
    if (cfg.debug) {
        STUB_BEGIN();
        E_count(5, "tests");
        E(0x8b, 0x0d); E32(0x4db52c);               /* mov ecx,[0x4db52c] */
        EJMP(0x451469); site_hook(0x451463, 6);

        STUB_BEGIN();
        E_count(0, "shots");
        E(0x8b, 0x06);                              /* mov eax,[esi] */
        E(0x3b, 0x46, 0xfc);                        /* cmp eax,[esi-4] */
        E(0x75, 0x0b);                              /* changed: on to the interval */
        E_count(1, "notick");
        EJMP(0x45174a);
        EJMP(0x4514d9); site_hook(0x4514ce, 11);

        STUB_BEGIN();
        E(0x85, 0xd2);                              /* test edx,edx */
        E(0x74, 0x0b);                              /* zero: this shot hits */
        E_count(2, "cadence");
        EJMP(0x4516b7);
        E_count(3, "hit");
        E(0xf6, 0xc1, 0x02);                        /* test cl,2 -- the swept shape */
        E(0x74, 0x06);
        E_count(4, "melee");
        EJMP(0x4514e5); site_hook(0x4514dd, 8);
    }

    stub_end();
    LOG("TH14 site patches installed (%u bytes of stubs)", (unsigned)g_stub_used);
}

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
    { 0x417610, MODE_SUB,   "BulletManager"   },
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
    { 0x44ec60, MODE_SUB,   "Player"          },
    { 0x411eb0, MODE_FRAME, "Update20"        },
    { 0x422a60, MODE_FRAME, "Update21"        },
    { 0x439750, MODE_SUB,   "ItemManager"     },
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
        /* The player, and the callback the runner watches so that it can read the state
           timer's float before and after every Player call (layout.player_timer). */
        .player = 0x4db67c, .player_callback = 0x44ec60,
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
        /* The player's life-state timer: prev +0x68c, integer +0x690, float +0x694, and its
           rate pointer at +0x698 -- which the constructor points at the game speed (0x44dd2a),
           so the timer sub-steps by itself. */
        .player_timer = 0x694,
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
    .install_sites = th14_install_sites,
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
