#pragma once
#include <stdint.h>
#include <stddef.h>
struct FixedSignature { uint32_t rva; unsigned char size; unsigned char bytes[32]; };
struct GuardRange { uint32_t rva, bytes, count, stride; };
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
    uint32_t sprite_draw, sprite_draw_rotated;
    uint32_t vm_start[2];
    unsigned vm_position, vm_age, vm_script, vm_flags;
    /* Sub-tick player movement. The motion site is relocated so its per-frame step can be
       scaled; everything the sub-step pass needs to reproduce that step lives here. */
    uint32_t player_motion, player_motion_resume;
    unsigned player_motion_size;
    uint32_t player, bounds, input_poll, replay_playing;
    unsigned pl_position, pl_scale, pl_speed_straight, pl_speed_diagonal;
    const struct GuardRange* guards;
    size_t guard_count;
};
