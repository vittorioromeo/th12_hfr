#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../dim_classes.h"
struct FixedSignature { uint32_t rva; unsigned char size; unsigned char bytes[32]; };
struct GuardRange { uint32_t rva, bytes, count, stride; };
/* Which dimming class a sprite belongs to, by the draw callback that is running. Coarser than
   the x86 rules, which also match the ANM file, layer and script index -- so a callback rule is
   only correct for a callback that draws *nothing but* that class. Two of New Classic's do not:
   the bullet callback also draws the items, and the player's shot callback goes on to draw the
   player. Both are handled by the pool rules below, and a new callback rule is worth only as
   much as a reading of the whole function that says it draws one thing. */
struct DimRule { uint32_t draw_callback; int category; };
/* Some classes share a draw callback with something that must not fade: items are drawn by the
   same callback as the bullets they have to stand out against. Those are matched by the VM's
   own address instead -- a pool is a fixed array of fixed-stride entries each carrying a VM at
   a fixed offset, so membership is exact arithmetic, not a guess. Pools are checked first. */
struct DimPool { uint32_t rva; uint32_t stride, count; int category; };
struct FixedGame {
    const char* name;
    const char* executable;
    const char* sha256;
    uint32_t image_size;
    const struct FixedSignature* signatures;
    size_t signature_count;
    uint32_t update, update_calls[2], draw, draw_call;
    uint32_t post_update, post_update_resume, audio_counter;
    uint32_t wait_site, wait_resume, frame_epilogue;
    unsigned wait_patch_size;
    uint32_t present_call, graphics_api, no_vsync;
    /* Every entry point that draws a VM. Menus and the title screen go through a third
       one the patch missed for a long time, which is why nothing there was ever smoothed. */
    uint32_t sprite_draw, sprite_draw_rotated, sprite_draw_menu;
    uint32_t vm_start[2];
    unsigned vm_position, vm_age, vm_script, vm_flags;
    unsigned vm_rotation, vm_scale;   /* radians x/y/z, and the x/y size multipliers */
    /* The VM's packed draw colour: three colour bytes then alpha in the top byte, copied
       verbatim into the global the draw path reads. Dimming scales it in place. */
    unsigned vm_colour;
    /* The draw runner's per-node dispatch, relocated so the runtime knows which callback is
       drawing; that is what a sprite is classified by. Same shape as the update runner's. */
    uint32_t draw_dispatch, draw_dispatch_resume; unsigned draw_dispatch_size;
    const struct DimRule* dim_rules; size_t dim_rule_count;
    const struct DimPool* dim_pools; size_t dim_pool_count;
    uint32_t fps_counter;             /* the game's own presented-frame count for its readout */
    uint32_t update_list, draw_list;  /* the two list sentinels, for the node diagnostic */
    unsigned node_priority, node_callback, node_next, node_argument;
    /* Sub-tick player movement. The motion site is relocated so its per-frame step can be
       scaled; everything the sub-step pass needs to reproduce that step lives here. */
    uint32_t player_motion, player_motion_resume;
    unsigned player_motion_size;
    uint32_t player, bounds, input_poll;
    /* Recorded in section 14 as "non-zero while a replay drives the input word". That came
       from three leaf functions nothing in the binary calls, so it is not trustworthy and
       no longer gates anything (section 17); it is logged so a run can identify it. */
    uint32_t replay_suspect;
    unsigned pl_position, pl_scale, pl_speed_straight, pl_speed_diagonal;
    /* Sub-stepped projectiles. One callback updates bullets and lasers; on a sub-step pass
       it is called directly with `projectile_arg` and only its motion, culling and collision
       are wanted, so each block that must stay at 60 Hz is relocated behind a gate. Every
       site has a resume (where the relocated bytes continue) and the gated ones a skip. */
    uint32_t projectile, projectile_arg;
    uint32_t proj_motion, proj_motion_resume; unsigned proj_motion_size;
    /* The state switch is not simply skipped on a sub-step pass. Only state 1 -- the
       ordinary moving bullet -- falls through the switch into the generic motion. Every
       other live state (the three spawn-in animations and the cancel animation) advances
       the bullet by a fraction of its velocity inside its own arm and then leaves the loop
       body early, bypassing the motion, the cull and the collision. Skipping the switch
       wholesale therefore gave those states a full extra frame of full-speed motion per
       frame, on top of their own (section 26). So the minor branch re-reads the state:
       `proj_states_skip` is where it continues when the state is the ordinary one, and
       `proj_states_other` where it continues for all the rest -- the age update, which is
       itself gated, so they are left exactly where the 60 Hz pass put them. */
    uint32_t proj_states, proj_states_resume, proj_states_skip, proj_states_other;
    unsigned proj_states_size;
    uint32_t proj_offscreen, proj_offscreen_resume; unsigned proj_offscreen_size;
    uint32_t proj_timer, proj_timer_resume; unsigned proj_timer_size;
    uint32_t proj_laser_growth, proj_laser_growth_resume; unsigned proj_laser_growth_size;
    uint32_t proj_laser_timer, proj_laser_timer_resume, proj_laser_timer_skip; unsigned proj_laser_timer_size;
    uint32_t proj_epoch, proj_epoch_resume; unsigned proj_epoch_size;
    /* The item pool is not a projectile, but the projectile callback updates it before it
       touches a single bullet: `item_call` is that call and `item_update` its target. Item
       motion is a whole-frame step -- fall speed accumulates toward a terminal velocity and
       the collection tests read the stepped position -- so a sub-step pass must not make it,
       or items fall at the tick rate instead of at 60 Hz (section 22). */
    uint32_t item_call, item_update;
    /* Every bullet carries a sprite VM at +0x50, and the callback steps it one frame at the
       tail of the per-bullet loop -- four instructions before the age it does gate. A script
       step is a whole frame: it is what plays the cancel bursts, and a script that offsets
       its sprite moves it once per step, so on a sub-step pass the step has to be held back
       or those animations run at the tick rate (section 25). The laser loop's identical step
       is already inside `proj_laser_timer`'s skip. This one cannot be relocated -- the block
       it sits in reads the manager through a RIP-relative operand -- so the call is
       redirected instead: `proj_sprite_call` is that call and `proj_sprite_step` its target. */
    uint32_t proj_sprite_call, proj_sprite_step;
    const struct GuardRange* guards;
    size_t guard_count;
};
