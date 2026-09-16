/* Fixed-clock x64 runtime. Game facts live in games/, not in this backend. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <dxgi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include "../third_party/minhook/include/MinHook.h"
#include "ui/ui_api.h"
#include "backends/fixed_history.h"
#include "backends/fixed_clock.h"
#include "backends/subtick.h"
#include "backends/substep.h"
#include "fixed_identity.h"
void hfr_d3d11_overlay(void* swap);

static const struct FixedGame* game;
static uintptr_t base;
static HMODULE module;
static FILE* logfile;
static char ini[MAX_PATH];
static int fps=0, rate=60, interpolate=1, vsync=0, debug=0, subtick=0, substep=0, diag_seconds=0;
static int major=1, guard_failed=0, depth=0;
static uint64_t ticks, frames, samples, blends;
static double phase, frequency, deadline;
static struct FixedClock logic_clock;
static HANDLE timer;
static struct FixedPose history[16384];
/* These three live in the relay page so the relocated movement site can reach them with a
   RIP-relative displacement; the DLL's own globals may be further than 2 GB from the game. */
static float* player_factor; static unsigned char* player_ran;
/* Sub-stepped projectiles: `proj_minor` is 1 while a sub-step pass is running, so the
   gated 60 Hz blocks stand aside; `proj_dt` scales the bullet motion. `proj_ran` is set
   by the manager's frame counter, which only advances on a full pass, so it says whether
   projectiles are being updated at all -- paused, between stages and in menus they are
   not, and the pass must not move anything. */
static unsigned char* proj_minor; static float* proj_dt; static unsigned char* proj_ran;
/* The node whose draw callback is running, written by the relocated draw dispatch and cleared
   when it returns; NULL outside the runner. A sprite's class is decided by it. */
static const unsigned char** draw_node;
static int dim_percent[DIM_COUNT];
static uint64_t dim_faded;
static double measured_present, measured_update;   /* last stats window, for the menu */
static uint64_t sprite_calls, sprite_skipped_off, sprite_skipped_stack;
static int listed_full; static char listed_signature[512];
/* Which draw callback drew how many sprites, over a stats window. This is what identifies a
   dimming class: the profile's rule table is keyed by callback, and guessing which callback
   is which is how earlier sections of the notes went wrong. */
#define CENSUS 24
static struct { uint32_t rva; short prio; uint64_t count; } census[CENSUS];
static void census_add(void) {
    if (!draw_node || !*draw_node) return;
    const unsigned char* node=*draw_node;
    uintptr_t fn=*(const uintptr_t*)(node+game->node_callback);
    if (fn<=base || fn>=base+game->image_size) return;
    uint32_t rva=(uint32_t)(fn-base);
    for (int i=0;i<CENSUS;++i) {
        if (census[i].count && census[i].rva!=rva) continue;
        census[i].rva=rva; census[i].prio=*(const short*)(node+game->node_priority);
        ++census[i].count; return;
    }
}
static struct SubtickPlayer proj_slice; static uint64_t proj_passes, items_skipped, sprite_steps_held;
static struct SubtickPlayer subtick_player;
static uint64_t subtick_moves, subtick_polls; static double subtick_poll_max;
typedef uintptr_t (*UpdateFn)(void*);
typedef uint32_t (*PollFn)(uintptr_t);
typedef uintptr_t (*ProjFn)(uintptr_t);
typedef uintptr_t (*ItemFn)(void*);
typedef uintptr_t (*VmStepFn)(void*,void*);
typedef void (*DrawFn)(void);
typedef uintptr_t (*SpriteFn)(void*,void*,uintptr_t);
static SpriteFn sprite_original, rotated_original, menu_sprite_original;
static SpriteFn vm_start_original[2];
static void set_rate(void);
static int subtick_active(void);
static int substep_active(void);
static void report(const char* fmt, ...) {
    if (!logfile) return;
    va_list ap; va_start(ap,fmt); vfprintf(logfile,fmt,ap); va_end(ap);
    fputc('\n',logfile); fflush(logfile);
}
#define LOG(...) report(__VA_ARGS__)
#include "ui/overlay_fixed.c"
#include "core/patch.c"
static const uint8_t* site_expected(uintptr_t addr,size_t n) {
    for (size_t i=0;i<game->signature_count;++i) {
        const struct FixedSignature* s=&game->signatures[i];
        if (addr==base+s->rva && n<=s->size) return s->bytes;
    }
    return NULL;
}
static double now(void) {LARGE_INTEGER q;QueryPerformanceCounter(&q);return (double)q.QuadPart;}
static int display_rate(void) {
    DEVMODEA dm={0};dm.dmSize=sizeof dm;
    return EnumDisplaySettingsA(NULL,ENUM_CURRENT_SETTINGS,&dm) && dm.dmDisplayFrequency>=60 ? (int)dm.dmDisplayFrequency : 60;
}
static void set_rate(void) {
    rate=fps ? fps : display_rate(); if (rate<60) rate=60; if (rate>1000) rate=1000;
    if (guard_failed) rate=60;
    deadline=0;
    memset(history,0,sizeof history);
    LOG("rate=%d, fixed simulation=60, interpolate=%d, subtick=%d, substep=%d, vsync=%d",rate,interpolate,subtick,substep,vsync);
}
/* Sub-tick player movement is a gameplay change, so it stays off unless asked for, and
   steps aside whenever its premises do not hold: at 60 Hz there is nothing between frames,
   during replay playback the input word comes from the file rather than the device, and a
   failed draw guard already means the frame structure is not understood. */
static int subtick_active(void) {
    return subtick && rate > 60 && player_factor && !guard_failed;
}
/* One slice of the current frame's player motion, using input polled at this instant. */
static void subtick_move(double tau) {
    double dt = subtick_slice(&subtick_player, tau, subtick_active() && subtick_player.armed);
    if (dt <= 0.0) return;
    double t0 = now();
    uint32_t input = ((PollFn)(base + game->input_poll))(0);
    double cost = (now() - t0) / frequency;
    if (cost > subtick_poll_max) subtick_poll_max = cost;
    ++subtick_polls;
    unsigned char* player = (unsigned char*)(base + game->player);
    const float* straight = (const float*)(player + game->pl_speed_straight);
    const float* diagonal = (const float*)(player + game->pl_speed_diagonal);
    float dx, dy;
    subtick_direction(input, straight, diagonal, &dx, &dy);
    if (dx == 0 && dy == 0) return;
    float* position = (float*)(player + game->pl_position);
    const float* scale = (const float*)(player + game->pl_scale);
    const float* bounds = (const float*)(base + game->bounds);
    position[0] = subtick_clamp(position[0] + dx * scale[0] * (float)dt, bounds[0], bounds[2]);
    position[1] = subtick_clamp(position[1] + dy * scale[1] * (float)dt, bounds[1], bounds[3]);
    ++subtick_moves;
}
/* Sub-stepped projectiles run the bullet/laser callback again between native ticks with
   the 60 Hz blocks gated off, so bullets move -- and are culled, grazed and collided --
   several times per frame. It stands aside on the same terms as sub-tick movement, and
   additionally whenever the last full pass did not run the callback at all: paused,
   between stages and in menus the manager's frame counter does not advance, and nothing
   should be moved. */
static int substep_active(void) {
    return substep && rate > 60 && proj_dt && proj_minor && !guard_failed;
}
/* One slice of the current frame's projectile motion. The slices of a frame sum to exactly
   one frame, and the native pass contributes none of it, so at every native tick the
   bullets are where the unmodified game would have put them. */
static void projectiles_slice(double tau) {
    double dt = subtick_slice(&proj_slice, tau, substep_active() && proj_slice.armed);
    if (dt <= 0.0) return;
    *proj_dt = (float)dt;
    *proj_minor = 1;
    ((ProjFn)(base + game->projectile))(base + game->projectile_arg);
    *proj_minor = 0;
    *proj_dt = 0.0f;
    ++proj_passes;
}
/* The projectile callback's first act, before it looks at a single bullet, is to update the
   item pool. That is a whole-frame step -- fall speed accumulates 0.03 a frame toward a
   terminal 3.0, the collection and off-screen tests read the stepped position, and collecting
   an item awards score and power -- so on a sub-step pass it has to stand aside entirely, or
   items fall at the tick rate. Rendering still shows them moving smoothly, because each item
   carries its own sprite VM and the sprite hook interpolates that like any other. */
static uintptr_t item_update(void* pool) {
    if (proj_minor && *proj_minor) {++items_skipped;return 0;}
    return ((ItemFn)(base+game->item_update))(pool);
}
/* And the same for the sprite VM every bullet carries. The callback steps it once at the
   tail of the per-bullet loop, and a step is a whole frame of the bullet's ANM script: the
   scripts that fade, spin and scatter a cancelled bullet move the sprite themselves, so
   stepping one six times a frame scatters it six times as far. Held back on a sub-step pass,
   like the timer four instructions later and like the laser loop's identical step. What is
   lost is smoothness within the tick, and the sprite hook already interpolates that.
   The caller discards the result; 1 is what the stepper answers for a VM with no script. */
static uintptr_t sprite_vm_step(void* manager, void* vm) {
    if (proj_minor && *proj_minor) {++sprite_steps_held;return 1;}
    return ((VmStepFn)(base+game->proj_sprite_step))(manager,vm);
}
/* Scheduling uses elapsed time: a blocked Present must not slow the simulation just
   because the requested presentation rate exceeds the actual display rate. At most
   one native update is made per outer loop; severe stalls retain native slowdown. */
static uintptr_t update_first(void* result) {
    if (pending_rate) {pending_rate=0;set_rate();}
    major=fixed_clock_step(&logic_clock,now(),frequency,rate,&phase);
    if (major) {
        /* Finish the outgoing frame's projectile motion before the next frame's logic sees
           it, so the 60 Hz pass reads exactly the positions the unmodified game would. */
        projectiles_slice((double)ticks + 1.0);
        ++ticks;
        /* 1.0 reproduces the original instruction exactly, so with the feature off the
           relocated site is bit-identical to the game's own code. */
        if (player_factor) *player_factor=subtick_active()?0.0f:1.0f;
        if (player_ran) *player_ran=0;
        if (proj_dt) *proj_dt=substep_active()?0.0f:1.0f;
        if (proj_ran) *proj_ran=0;
        uintptr_t r=((UpdateFn)(base+game->update))(result);
        subtick_player.armed=player_ran && *player_ran;
        proj_slice.armed=proj_ran && *proj_ran;
        subtick_move((double)ticks+phase);
        return (unsigned char)r; /* AH=0 is the post-update relay's normal path. */
    }
    projectiles_slice((double)ticks+phase);
    subtick_move((double)ticks+phase);
    return 0x100; /* AL=0 (normal), AH=1 (skip native audio/fast-forward bookkeeping). */
}
static uintptr_t update_extra(void* result) {
    ++ticks; return ((UpdateFn)(base+game->update))(result);
}
static uint64_t guard_hash(void) {
    uint64_t h=14695981039346656037ull;
    for (size_t r=0;r<game->guard_count;++r) {
        const struct GuardRange* g=&game->guards[r];
        for (uint32_t n=0;n<g->count;++n) {
            const unsigned char* p=(void*)(base+g->rva+(size_t)n*g->stride);
            for (uint32_t b=0;b<g->bytes;++b) h=(h^p[b])*1099511628211ull;
        }
    }
    return h;
}
/* Read-only walk of the game's own update and draw lists, exactly as its runners walk them.
   Nothing is patched or called; this only says which systems are registered right now, which
   is what identifies whatever draws a screen the sprite hook never sees. */
static int readable(const void* p,size_t n) {
    MEMORY_BASIC_INFORMATION info;
    if (!p || VirtualQuery(p,&info,sizeof info)!=sizeof info) return 0;
    if (info.State!=MEM_COMMIT || (info.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return 0;
    return (uintptr_t)p+n <= (uintptr_t)info.BaseAddress+info.RegionSize;
}
static void log_lists(const char* when) {
    if (!game->draw_list || !game->update_list) return;
    const struct {const char* name;uint32_t sentinel;} lists[2]={
        {"update",game->update_list},{"draw",game->draw_list}};
    for (int l=0;l<2;++l) {
        char line[512]; int n=0; unsigned count=0;
        n+=snprintf(line+n,sizeof line-n,"%s %s list:",when,lists[l].name);
        const unsigned char* node=(const unsigned char*)(base+lists[l].sentinel);
        for (unsigned i=0;i<64 && readable(node,0x40);++i) {
            uintptr_t fn=*(const uintptr_t*)(node+game->node_callback);
            if (fn>base && fn<base+game->image_size && n<(int)sizeof line-32) {
                short prio=*(const short*)(node+game->node_priority);
                n+=snprintf(line+n,sizeof line-n," %d@%x",prio,(unsigned)(fn-base));
                ++count;
            }
            node=*(const unsigned char* const*)(node+game->node_next);
        }
        if (count) {
            /* Print only when the set has changed: one ordinary session then yields the map
               for every screen it passed through, instead of only the first few seconds. */
            size_t at=strlen(listed_signature);
            if (!strstr(listed_signature,line)) {
                /* Only log a list we can also remember. Logging one we cannot record would
                   repeat it every window for the rest of the session. */
                if (at+strlen(line)+2<sizeof listed_signature) {
                    LOG("%s (%u nodes)",line,count);
                    memcpy(listed_signature+at,line,strlen(line)+1);
                    listed_signature[at+strlen(line)]='\n'; listed_signature[at+strlen(line)+1]=0;
                } else if (!listed_full) {
                    listed_full=1;
                    LOG("node lists: no room to remember more; not logging further changes");
                }
            }
        }
    }
}
static void draw_frame(void) {
    uint64_t before=guard_hash();
    ((DrawFn)(base+game->draw))();
    ++frames;
    if (!guard_failed && before!=guard_hash()) {
        guard_failed=1;
        LOG("DRAW GUARD FAILED at frame=%llu tick=%llu; reverting to 60 Hz without interpolation",(unsigned long long)frames,(unsigned long long)ticks);
        set_rate();
    }
}
static struct FixedPose* history_slot(uintptr_t key,int create) {
    size_t index=((key>>4)*11400714819323198485ull)>>(64-14);
    struct FixedPose* vacant=NULL;
    for (unsigned i=0;i<8;++i) {
        struct FixedPose* slot=&history[(index+i)&16383];
        if (slot->key==key) return slot;
        if (!vacant && (!slot->key || slot->tick+2<ticks)) vacant=slot;
    }
    return create?vacant:NULL;
}
static void reset_vm(void* vm) {
    struct FixedPose* h=history_slot((uintptr_t)vm,0);
    if (h) memset(h,0,sizeof *h);
}
static uintptr_t vm_start_0(void* m,void* v,uintptr_t script) {reset_vm(v);return vm_start_original[0](m,v,script);}
static uintptr_t vm_start_1(void* m,void* v,uintptr_t script) {reset_vm(v);return vm_start_original[1](m,v,script);}
/* Which class is being drawn right now, from the callback the draw runner is in. */
/* A VM that belongs to one of the profile's pools is classified by the pool, whatever callback
   is drawing it: the item pool shares its callback with the bullets. `rva` is the first entry's
   VM, so an address matches only if it is that VM or an exact multiple of the stride past it. */
static int dim_pool_class(const unsigned char* vm) {
    for (size_t i=0;i<game->dim_pool_count;++i) {
        const struct DimPool* p=&game->dim_pools[i];
        uintptr_t first=base+p->rva;
        if ((uintptr_t)vm<first) continue;
        uintptr_t offset=(uintptr_t)vm-first;
        if (offset%p->stride==0 && offset/p->stride<p->count) return p->category;
    }
    return DIM_NONE;
}
static int dim_class(void) {
    if (!draw_node || !game->dim_rule_count) return DIM_NONE;
    const unsigned char* node=*draw_node;
    if (!node) return DIM_NONE;
    uintptr_t fn=*(const uintptr_t*)(node+game->node_callback);
    if (fn<=base || fn>=base+game->image_size) return DIM_NONE;
    uint32_t rva=(uint32_t)(fn-base);
    for (size_t i=0;i<game->dim_rule_count;++i)
        if (game->dim_rules[i].draw_callback==rva) return game->dim_rules[i].category;
    return DIM_NONE;
}
/* Fade a sprite by scaling its alpha, which is the top byte of the VM's packed colour. The
   x86 runtime scales alpha for most classes and the colour only where alpha would do nothing
   (additive blending, and the background class); this backend cannot yet tell a VM's blend
   mode, so it scales alpha only and additive effects will not fade until it can. */
static uint32_t dim_fade_colour(uint32_t colour,int percent,int klass) {
    uint32_t keep=(uint32_t)(100-percent);
    /* The background is drawn opaque over the playfield's own fill, so lowering its alpha
       would change nothing; it is darkened by scaling its colour instead, which is what the
       x86 backend does for this one class. Everything else fades by alpha. */
    if (klass==DIM_BACKGROUND) {
        uint32_t r=((colour>>16)&0xff)*keep/100u, g=((colour>>8)&0xff)*keep/100u, b=(colour&0xff)*keep/100u;
        return (colour&0xff000000u)|(r<<16)|(g<<8)|b;
    }
    uint32_t alpha=((colour>>24)&0xff)*keep/100u;
    return (colour&0x00ffffffu)|(alpha<<24);
}
static uintptr_t sprite(SpriteFn original,void* manager,void* vm,uintptr_t flags) {
    ++sprite_calls;
    if (depth || !vm || !interpolate || guard_failed || rate==60) {
        ++sprite_skipped_off;
        return original(manager,vm,flags);
    }
    /* Stack VMs are temporary text/layout scratch objects, not persistent sprites. */
    NT_TIB* tib=(NT_TIB*)NtCurrentTeb();
    if ((uintptr_t)vm>=(uintptr_t)tib->StackLimit && (uintptr_t)vm<(uintptr_t)tib->StackBase) {
        ++sprite_skipped_stack;
        return original(manager,vm,flags);
    }
    unsigned char* p=vm;
    uintptr_t script; int age;
    float saved[3], out[3], turn[3], turn_out[3], size[2], size_out[2];
    memcpy(saved,p+game->vm_position,sizeof saved);
    memcpy(&script,p+game->vm_script,sizeof script);
    memcpy(&age,p+game->vm_age,sizeof age);
    memcpy(turn,p+game->vm_rotation,sizeof turn);
    memcpy(size,p+game->vm_scale,sizeof size);
    struct FixedPose* h=history_slot((uintptr_t)vm,1);
    ++samples; census_add();
    int predict=subtick_active();
    int changed=h && fixed_pose(h,(uintptr_t)vm,script,ticks,age,saved,phase,predict,out);
    /* Rotation and scale ride on the same decision: a menu that spins or grows a step per
       60 Hz frame looks exactly as stepped as one that moves, and most of them do both. */
    int turned=h && fixed_pose_extra(h,turn,size,phase,predict,turn_out,size_out);
    if (changed) {memcpy(p+game->vm_position,out,sizeof out);++blends;}
    if (turned) {memcpy(p+game->vm_rotation,turn_out,sizeof turn_out);
                 memcpy(p+game->vm_scale,size_out,sizeof size_out);}
    /* Dimming rides on the same wrap: scale the colour the game is about to copy into its
       draw colour, then put it back, exactly as the position is handled. */
    int klass=game->vm_colour?dim_pool_class(p):DIM_NONE;
    if (klass==DIM_NONE && game->vm_colour) klass=dim_class();
    int percent=(klass>=0 && klass<DIM_COUNT)?dim_percent[klass]:0;
    uint32_t colour=0;
    if (percent>0) {
        memcpy(&colour,p+game->vm_colour,sizeof colour);
        uint32_t faded=dim_fade_colour(colour,percent,klass);
        memcpy(p+game->vm_colour,&faded,sizeof faded);
        ++dim_faded;
    }
    ++depth;
    uintptr_t result=original(manager,vm,flags);
    --depth;
    if (percent>0) memcpy(p+game->vm_colour,&colour,sizeof colour);
    /* Restore only what we wrote: preserve the original function's native behavior. */
    if (changed) memcpy(p+game->vm_position,saved,sizeof saved);
    if (turned) {memcpy(p+game->vm_rotation,turn,sizeof turn);
                 memcpy(p+game->vm_scale,size,sizeof size);}
    return result;
}
static uintptr_t sprite_draw(void* m,void* v,uintptr_t f) {return sprite(sprite_original,m,v,f);}
static uintptr_t rotated_draw(void* m,void* v,uintptr_t f) {return sprite(rotated_original,m,v,f);}
static uintptr_t menu_draw(void* m,void* v,uintptr_t f) {return sprite(menu_sprite_original,m,v,f);}
static HRESULT present(IDXGISwapChain* swap,UINT sync,UINT flags) {
    (void)sync;
    if (!(flags & DXGI_PRESENT_TEST) && menu_key_code) hfr_d3d11_overlay(swap);
    *(int*)(base+game->no_vsync)=!vsync;
    return IDXGISwapChain_Present(swap,vsync?1:0,flags);
}
static int wait_frame(void) {
    double t=now(), step=frequency/rate;
    if (!deadline || t-deadline>step*4) deadline=t;
    deadline+=step;
    for (;;) {
        double remaining=deadline-now();
        if (remaining<=0) break;
        if (timer && remaining>frequency*0.0004) {
            LARGE_INTEGER due; due.QuadPart=-(LONGLONG)((remaining/frequency-0.0002)*10000000.0);
            if (SetWaitableTimer(timer,&due,0,NULL,NULL,FALSE)) WaitForSingleObject(timer,INFINITE);
            else SwitchToThread();
        } else YieldProcessor();
    }
    /* The game counts its own frames once per native tick (just past wait_resume, which a
       presentation-only iteration never reaches), so its on-screen readout would sit at 60
       whatever the display is doing. Count the iterations it does not see, and the number it
       prints becomes the presentation rate. Nothing else in the binary reads that counter. */
    if (!major && game->fps_counter) ++*(unsigned*)(base+game->fps_counter);
    static double last; static uint64_t ft,ut,st,bt,pt,qt;
    t=now();
    if (diag_seconds>0) {
        static double began;
        if (!began) began=t;
        if ((t-began)/frequency >= diag_seconds) {
            log_lists("final");
            LOG("diag_seconds=%d elapsed; quitting",diag_seconds);
            if (logfile) fflush(logfile);
            ExitProcess(0);
        }
    }
    if (!last) {last=t;ft=frames;ut=ticks;st=samples;bt=blends;pt=subtick_polls;qt=proj_passes;}
    if (t-last>=frequency*2) {
        double seconds=(t-last)/frequency;
        measured_present=(frames-ft)/seconds; measured_update=(ticks-ut)/seconds;
        LOG("stats seconds=%.3f presents=%.2f updates=%.2f frames=%llu ticks=%llu samples=%llu blends=%llu guard=%s api=%d",seconds,
            (frames-ft)/seconds,(ticks-ut)/seconds,(unsigned long long)frames,(unsigned long long)ticks,
            (unsigned long long)(samples-st),(unsigned long long)(blends-bt),guard_failed?"FAILED":"ok",*(int*)(base+game->graphics_api));
        /* A feature that is switched on but did nothing all window is a bug, not a mode.
           Say which term is holding it back, and print the byte section 14 believed was the
           replay flag so a run can finally say what it really does. */
        LOG("sprites: %llu calls, %llu off, %llu stack, %llu sampled, %llu blended",
            (unsigned long long)sprite_calls,(unsigned long long)sprite_skipped_off,
            (unsigned long long)sprite_skipped_stack,(unsigned long long)samples,
            (unsigned long long)blends);
        {   /* sprites per draw callback: the map a dimming rule table is built from */
            char line[480]; int n=0; n+=snprintf(line+n,sizeof line-n,"sprites by callback:");
            for (int i=0;i<CENSUS && census[i].count;++i)
                if (n<(int)sizeof line-24)
                    n+=snprintf(line+n,sizeof line-n," %d@%x=%llu",census[i].prio,
                        (unsigned)census[i].rva,(unsigned long long)census[i].count);
            if (census[0].count) LOG("%s",line);
            memset(census,0,sizeof census);
        }
        if (dim_faded) LOG("dimming: %llu sprites faded",(unsigned long long)dim_faded);
        log_lists("registered");
        if ((subtick && subtick_polls==pt) || (substep && proj_passes==qt))
            LOG("idle: subtick=%d(+%llu polls, armed=%d) substep=%d(+%llu passes, armed=%d) "
                "rate=%d guard=%d player_ran=%d proj_ran=%d suspect[%x]=%u",
                subtick,(unsigned long long)(subtick_polls-pt),subtick_player.armed,
                substep,(unsigned long long)(proj_passes-qt),proj_slice.armed,
                rate,guard_failed,player_ran?*player_ran:-1,proj_ran?*proj_ran:-1,
                (unsigned)game->replay_suspect,
                game->replay_suspect?*(const unsigned char*)(base+game->replay_suspect):0);
        if (proj_passes) LOG("substep %s: %llu projectile passes (%.2f/s, %.2f per frame), "
            "%llu item updates and %llu bullet animation steps held back",
            substep_active()?"on":"standing by",(unsigned long long)proj_passes,
            (proj_passes-qt)/seconds,(double)(proj_passes-qt)/(double)(ticks-ut?ticks-ut:1),
            (unsigned long long)items_skipped,(unsigned long long)sprite_steps_held);
        if (subtick_polls) LOG("subtick %s: %llu input polls (%.2f/s), longest %.2f ms, %llu player moves",
            subtick_active()?"on":"standing by",(unsigned long long)subtick_polls,(subtick_polls-pt)/seconds,
            subtick_poll_max*1000.0,(unsigned long long)subtick_moves);
        pt=subtick_polls;qt=proj_passes;subtick_poll_max=0;
        last=t;ft=frames;ut=ticks;st=samples;bt=blends;
    }
    return !major;
}
/* All generated relays are leaf tail jumps (no stack changes or calls). Native
   call sites provide shadow space and unwind metadata for our compiled callbacks. */
/* One 64 KiB-granular reservation near the image: the first page holds emitted code and
   ends up read-only, the second holds the words the runtime and the relocated player
   site write every frame and stays writable. Both are within rel32 of the image. */
static unsigned char* relay_page; static size_t relay_used; static unsigned char* data_page;
static int rel32(unsigned char* field,uintptr_t end,uintptr_t target) {
    int64_t d=(int64_t)target-(int64_t)end;
    if (d<INT32_MIN || d>INT32_MAX) return 0;
    int32_t value=(int32_t)d;memcpy(field,&value,4);return 1;
}
static void* relay(void* target) {
    if (relay_used+16>4096) return NULL;
    unsigned char* p=relay_page+relay_used;relay_used+=16;
    p[0]=0xff;p[1]=0x25;memset(p+2,0,4);memcpy(p+6,&target,8);return p;
}
static int queue_call(uint32_t rva,void* target) {
    unsigned char b[5]={0xe8};void* dest=relay(target);
    return dest && rel32(b+1,base+rva+5,(uintptr_t)dest) && patch_bytes(base+rva,b,sizeof b,NULL);
}
static int prepare_patches(void) {
    SYSTEM_INFO si;GetSystemInfo(&si);
    uintptr_t start=base&~((uintptr_t)si.dwAllocationGranularity-1);
    for (uintptr_t delta=si.dwAllocationGranularity;delta<0x70000000;delta+=si.dwAllocationGranularity) {
        relay_page=VirtualAlloc((void*)(start+delta),8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        if (relay_page) break;
        if (start>delta) relay_page=VirtualAlloc((void*)(start-delta),8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        if (relay_page) break;
    }
    if (!relay_page) return 0;
    data_page=relay_page+4096;player_factor=NULL;player_ran=NULL;
    proj_minor=NULL;proj_dt=NULL;proj_ran=NULL;draw_node=NULL;
    patch_begin();
    if (!queue_call(game->update_calls[0],update_first) || !queue_call(game->update_calls[1],update_extra) ||
        !queue_call(game->draw_call,draw_frame) || !queue_call(game->present_call,present)) return 0;
    unsigned char b[32];memset(b,0x90,sizeof b);
    b[0]=0xe8;
    if (!rel32(b+1,base+game->wait_site+5,(uintptr_t)relay(wait_frame))) return 0;
    b[5]=0x85;b[6]=0xc0;b[7]=0x0f;b[8]=0x85;
    if (!rel32(b+9,base+game->wait_site+13,base+game->frame_epilogue)) return 0;
    b[13]=0xe9;
    if (!rel32(b+14,base+game->wait_site+18,base+game->wait_resume) ||
        !patch_bytes(base+game->wait_site,b,game->wait_patch_size,NULL)) return 0;
    /* Skip the native fast-forward/audio work on a presentation-only iteration.
       On real ticks reproduce the three displaced instructions exactly. */
    unsigned char* p=relay_page+relay_used;relay_used+=32;
    p[0]=0x84;p[1]=0xe4; /* test ah,ah */
    p[2]=0x0f;p[3]=0x85;
    if (!rel32(p+4,(uintptr_t)p+8,base+game->draw_call)) return 0;
    p[8]=0x8b;p[9]=0x1d;
    if (!rel32(p+10,(uintptr_t)p+14,base+game->audio_counter)) return 0;
    const unsigned char displaced[]={0x40,0x32,0xf6,0x41,0x8b,0xfd,0xe9};
    memcpy(p+14,displaced,sizeof displaced);
    if (!rel32(p+21,(uintptr_t)p+25,base+game->post_update_resume)) return 0;
    memset(b,0x90,12);b[0]=0xe9;
    if (!rel32(b+1,base+game->post_update+5,(uintptr_t)p) || !patch_bytes(base+game->post_update,b,12,NULL)) return 0;
    /* Player movement. The two multiplies that turn a held direction into this frame's step
       gain a factor the sub-tick pass owns; the site is relocated whole so the store between
       them -- the facing direction the animation triggers already consumed -- keeps its place.
       A byte store records that the site ran, which is how the pass knows the player is in a
       state that moves at all: paused, dying and between stages it simply never executes. */
    /* Sub-stepped projectiles. Each gate has the same shape: test the pass flag, then either
       run the relocated bytes and resume, or jump past the block. The flag is tested before
       the relocated instructions so the flags they set are the ones that survive -- the state
       switch's `sub ecx,r12d` is read by the `je` at its resume, and by this gate's own `je`
       when it has an `other` target to choose between. */
    if (game->projectile) {
        proj_minor=data_page+8; *proj_minor=0;
        proj_dt=(float*)(data_page+12); *proj_dt=1.0f;
        proj_ran=data_page+16; *proj_ran=0;
        struct Gate { uint32_t site, resume, skip, other; unsigned size; int mark; } gates[] = {
            {game->proj_states, game->proj_states_resume, game->proj_states_skip, game->proj_states_other, game->proj_states_size, 0},
            {game->proj_offscreen, game->proj_offscreen_resume, game->proj_offscreen_resume, 0, game->proj_offscreen_size, 0},
            {game->proj_timer, game->proj_timer_resume, game->proj_timer_resume, 0, game->proj_timer_size, 0},
            {game->proj_laser_timer, game->proj_laser_timer_resume, game->proj_laser_timer_skip, 0, game->proj_laser_timer_size, 0},
            {game->proj_epoch, game->proj_epoch_resume, game->proj_epoch_resume, 0, game->proj_epoch_size, 1},
        };
        for (size_t i=0;i<sizeof gates/sizeof *gates;++i) {
            const struct Gate* g=&gates[i];
            if (relay_used+96>4096) return 0;
            unsigned char* q=relay_page+relay_used;relay_used+=96;size_t k=0;
            q[k]=0x80;q[k+1]=0x3d;q[k+6]=0x00;              /* cmp byte [rip+minor],0 */
            if (!rel32(q+k+2,(uintptr_t)(q+k+7),(uintptr_t)proj_minor)) return 0;
            k+=7;
            size_t branch=k; q[k]=0x75;k+=2;                /* jne skip; filled in below */
            memcpy(q+k,(const void*)(base+g->site),g->size);k+=g->size;
            if (g->mark) {                                  /* mov byte [rip+ran],1 */
                q[k]=0xc6;q[k+1]=0x05;q[k+6]=1;
                if (!rel32(q+k+2,(uintptr_t)(q+k+7),(uintptr_t)proj_ran)) return 0;
                k+=7;
            }
            q[k]=0xe9;                                      /* jmp resume */
            if (!rel32(q+k+1,(uintptr_t)(q+k+5),base+g->resume)) return 0;
            k+=5;
            q[branch+1]=(unsigned char)(k-(branch+2));      /* jne lands on the skip jump */
            if (g->other) {
                /* A gate that has to know what it is skipping: the relocated bytes run on
                   this side too, purely for the comparison they leave in the flags, and the
                   pass continues at one of two places according to the answer. Re-running
                   them is safe because they only read -- the state word and two registers
                   the switch itself was about to consume. */
                memcpy(q+k,(const void*)(base+g->site),g->size);k+=g->size;
                q[k]=0x0f;q[k+1]=0x84;                      /* je skip */
                if (!rel32(q+k+2,(uintptr_t)(q+k+6),base+g->skip)) return 0;
                k+=6;
                q[k]=0xe9;                                  /* jmp other */
                if (!rel32(q+k+1,(uintptr_t)(q+k+5),base+g->other)) return 0;
                k+=5;
            } else {
                q[k]=0xe9;                                  /* skip: jump past the block */
                if (!rel32(q+k+1,(uintptr_t)(q+k+5),base+g->skip)) return 0;
                k+=5;
            }
            memset(b,0x90,g->size);b[0]=0xe9;
            if (!rel32(b+1,base+g->site+5,(uintptr_t)q) ||
                !patch_bytes(base+g->site,b,g->size,NULL)) return 0;
        }
        /* The motion itself is not relocated but rewritten: each axis gains a multiply by the
           sub-step's length before it is added to the position. Off, the length is 1.0 and
           the multiply is exact, so the result is bit-identical to the original adds. */
        if (relay_used+96>4096) return 0;
        unsigned char* m=relay_page+relay_used;relay_used+=96;size_t k=0;
        static const unsigned char axis[3][3]={{0x53,0x30,0x08},{0x4b,0x34,0x0c},{0x43,0x38,0x10}};
        for (int a=0;a<3;++a) {
            const unsigned char* x=axis[a];
            m[k]=0xf3;m[k+1]=0x0f;m[k+2]=0x10;m[k+3]=x[0];m[k+4]=x[2];k+=5;   /* movss xmmN,[rbx+vel] */
            m[k]=0xf3;m[k+1]=0x0f;m[k+2]=0x59;m[k+3]=(unsigned char)((x[0]&0x38)|0x05);
            if (!rel32(m+k+4,(uintptr_t)(m+k+8),(uintptr_t)proj_dt)) return 0; /* mulss xmmN,[rip+dt] */
            k+=8;
            m[k]=0xf3;m[k+1]=0x0f;m[k+2]=0x58;m[k+3]=x[0];m[k+4]=x[1];k+=5;   /* addss xmmN,[rbx+pos] */
            m[k]=0xf3;m[k+1]=0x0f;m[k+2]=0x11;m[k+3]=x[0];m[k+4]=x[1];k+=5;   /* movss [rbx+pos],xmmN */
            if (a==0) {   /* the load the original interleaved here; the cull reads it */
                static const unsigned char load[]={0x48,0x8b,0x83,0x50,0x01,0x00,0x00};
                memcpy(m+k,load,sizeof load);k+=sizeof load;
            }
        }
        m[k]=0xe9;
        if (!rel32(m+k+1,(uintptr_t)(m+k+5),base+game->proj_motion_resume)) return 0;
        k+=5;
        memset(b,0x90,game->proj_motion_size);b[0]=0xe9;
        if (!rel32(b+1,base+game->proj_motion+5,(uintptr_t)m) ||
            !patch_bytes(base+game->proj_motion,b,game->proj_motion_size,NULL)) return 0;
        /* A laser's head advances by its speed the same way, so the same multiply sub-steps
           the beam; the tail follows the head by the game's own maximum-gap rule. */
        if (relay_used+48>4096) return 0;
        unsigned char* l=relay_page+relay_used;relay_used+=48;k=0;
        memcpy(l+k,(const void*)(base+game->proj_laser_growth),8);k+=8;  /* movss xmm1,[rbx+speed] */
        l[k]=0xf3;l[k+1]=0x0f;l[k+2]=0x59;l[k+3]=0x0d;                   /* mulss xmm1,[rip+dt]    */
        if (!rel32(l+k+4,(uintptr_t)(l+k+8),(uintptr_t)proj_dt)) return 0;
        k+=8;
        memcpy(l+k,(const void*)(base+game->proj_laser_growth+8),4);k+=4;/* addss xmm1,[rbx]       */
        l[k]=0xe9;
        if (!rel32(l+k+1,(uintptr_t)(l+k+5),base+game->proj_laser_growth_resume)) return 0;
        k+=5;
        memset(b,0x90,game->proj_laser_growth_size);b[0]=0xe9;
        if (!rel32(b+1,base+game->proj_laser_growth+5,(uintptr_t)l) ||
            !patch_bytes(base+game->proj_laser_growth,b,game->proj_laser_growth_size,NULL)) return 0;
        /* The items the callback updates before any bullet: a whole call redirected rather
           than a block relocated, because nothing of it is wanted on a sub-step pass. */
        if (game->item_call && !queue_call(game->item_call,item_update)) return 0;
        /* The bullet sprite VM step at the tail of the loop, redirected the same way and for
           the same reason: nothing of it belongs on a sub-step pass. */
        if (game->proj_sprite_call && !queue_call(game->proj_sprite_call,sprite_vm_step)) return 0;
    }
    /* The draw runner's per-node dispatch: record the node, run the callback, forget it. A
       leaf that pushes nothing, so the callback sees the stack it always saw; and `mov` sets
       no flags, so the `cmp eax,2` that follows still reads the callback's return value. */
    if (game->draw_dispatch) {
        draw_node=(const unsigned char**)(data_page+24); *draw_node=NULL;
        if (relay_used+64>4096) return 0;
        unsigned char* d=relay_page+relay_used;relay_used+=64;size_t k=0;
        d[k]=0x48;d[k+1]=0x89;d[k+2]=0x1d;                      /* mov [rip+draw_node],rbx */
        if (!rel32(d+k+3,(uintptr_t)(d+k+7),(uintptr_t)draw_node)) return 0;
        k+=7;
        memcpy(d+k,(const void*)(base+game->draw_dispatch),game->draw_dispatch_size);
        k+=game->draw_dispatch_size;
        d[k]=0x48;d[k+1]=0xc7;d[k+2]=0x05;                      /* mov qword [rip+draw_node],0 */
        memset(d+k+7,0,4);
        if (!rel32(d+k+3,(uintptr_t)(d+k+11),(uintptr_t)draw_node)) return 0;
        k+=11;
        d[k]=0xe9;
        if (!rel32(d+k+1,(uintptr_t)(d+k+5),base+game->draw_dispatch_resume)) return 0;
        k+=5;
        memset(b,0x90,game->draw_dispatch_size);b[0]=0xe9;
        if (!rel32(b+1,base+game->draw_dispatch+5,(uintptr_t)d) ||
            !patch_bytes(base+game->draw_dispatch,b,game->draw_dispatch_size,NULL)) return 0;
    }
    if (game->player_motion && relay_used+64<=4096) {
        /* These two are written every frame, so they belong on the page that stays writable. */
        player_factor=(float*)data_page;*player_factor=1.0f;
        player_ran=data_page+4;*player_ran=0;
        const unsigned char* site=(const unsigned char*)(base+game->player_motion);
        unsigned char* q=relay_page+relay_used;relay_used+=64;size_t k=0;
        memcpy(q+k,site,8);k+=8;                                        /* mulss xmm6,[rdi+scale.x] */
        q[k]=0xf3;q[k+1]=0x0f;q[k+2]=0x59;q[k+3]=0x35;                  /* mulss xmm6,[rip+factor]  */
        if (!rel32(q+k+4,(uintptr_t)(q+k+8),(uintptr_t)player_factor)) return 0;
        k+=8;
        memcpy(q+k,site+8,8);k+=8;                                      /* movss [rdi+facing],xmm7  */
        memcpy(q+k,site+16,8);k+=8;                                     /* mulss xmm7,[rdi+scale.y] */
        q[k]=0xf3;q[k+1]=0x0f;q[k+2]=0x59;q[k+3]=0x3d;                  /* mulss xmm7,[rip+factor]  */
        if (!rel32(q+k+4,(uintptr_t)(q+k+8),(uintptr_t)player_factor)) return 0;
        k+=8;
        q[k]=0xc6;q[k+1]=0x05;q[k+6]=1;                                 /* mov byte [rip+ran],1     */
        if (!rel32(q+k+2,(uintptr_t)(q+k+7),(uintptr_t)player_ran)) return 0;
        k+=7;
        q[k]=0xe9;                                                      /* jmp back                 */
        if (!rel32(q+k+1,(uintptr_t)(q+k+5),base+game->player_motion_resume)) return 0;
        memset(b,0x90,game->player_motion_size);b[0]=0xe9;
        if (!rel32(b+1,base+game->player_motion+5,(uintptr_t)q) ||
            !patch_bytes(base+game->player_motion,b,game->player_motion_size,NULL)) return 0;
    }
    DWORD old;
    /* Only the code page changes protection; data_page stays PAGE_READWRITE. */
    if (!VirtualProtect(relay_page,4096,PAGE_EXECUTE_READ,&old)) return 0;
    FlushInstructionCache(GetCurrentProcess(),relay_page,4096);return 1;
}
__declspec(dllexport) DWORD WINAPI hfr_start(void* unused) {
    (void)unused;
    static LONG started;
    if (InterlockedCompareExchange(&started,1,0)) return 0;
    char path[MAX_PATH];
    DWORD path_length=GetModuleFileNameA(module,ini,sizeof ini);
    if (!path_length || path_length>=sizeof ini) return 0;
    char* slash=strrchr(ini,'\\');if (!slash) return 0;
    strcpy(slash+1,"touhou_hfr.log");logfile=fopen(ini,"w");
    strcpy(slash+1,"touhou_hfr.ini");
    LOG("Touhou HFR x64 fixed-clock prototype");
    DWORD exe_length=GetModuleFileNameA(NULL,path,sizeof path);
    if (!exe_length || exe_length>=sizeof path || !(game=fixed_identify_file(path))) {
        LOG("Executable fingerprint rejected; no patches applied");return 0;
    }
    LOG("Verified %s",game->name);
    base=(uintptr_t)GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER* dos=(void*)base;
    IMAGE_NT_HEADERS64* pe=(void*)(base+dos->e_lfanew);
    if (pe->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 || pe->OptionalHeader.SizeOfImage!=game->image_size) return 0;
    for (size_t i=0;i<game->signature_count;++i) {
        const struct FixedSignature* s=&game->signatures[i];
        if (memcmp((void*)(base+s->rva),s->bytes,s->size)) {LOG("Signature rejected at RVA %x; no patches applied",s->rva);return 0;}
    }
    fps=GetPrivateProfileIntA("hfr","fps",0,ini);
    if (fps<0) fps=0;
    if (fps>1000) fps=1000;
    /* Experimental backend uses its own opt-out to avoid changing x86 defaults. */
    vsync=GetPrivateProfileIntA("fixed60","vsync",0,ini)!=0;
    interpolate=GetPrivateProfileIntA("fixed60","interpolate",1,ini)!=0;
    subtick=GetPrivateProfileIntA("fixed60","subtick",0,ini)!=0;
    substep=GetPrivateProfileIntA("fixed60","substep",0,ini)!=0;
    for (int i=0;i<DIM_COUNT;++i) {
        char key[32]; snprintf(key,sizeof key,"dim_%s",DIM_NAMES[i]);
        int v=GetPrivateProfileIntA("video",key,0,ini);
        dim_percent[i]=v<0?0:(v>100?100:v);
    }
    /* Unattended diagnostics: run for this many seconds, then log and quit. 0 disables
       it, which is the default and what any normal install has. */
    diag_seconds=GetPrivateProfileIntA("fixed60","diag_seconds",0,ini);
    debug=GetPrivateProfileIntA("hfr","debug",0,ini)!=0;
    menu_key_code=GetPrivateProfileIntA("video","menu_key",VK_F11,ini);
    if (menu_key_code<0 || menu_key_code>255) menu_key_code=VK_F11;
    LARGE_INTEGER q;QueryPerformanceFrequency(&q);frequency=(double)q.QuadPart;
    timer=CreateWaitableTimerExW(NULL,NULL,2,TIMER_ALL_ACCESS);
    if (!timer) timer=CreateWaitableTimerW(NULL,FALSE,NULL);
    set_rate();
    if (!prepare_patches()) {LOG("Code patch preparation failed");return 0;}
    if (MH_Initialize()!=MH_OK ||
        MH_CreateHook((void*)(base+game->sprite_draw),sprite_draw,(void**)&sprite_original)!=MH_OK ||
        MH_CreateHook((void*)(base+game->sprite_draw_rotated),rotated_draw,(void**)&rotated_original)!=MH_OK ||
        (game->sprite_draw_menu &&
         MH_CreateHook((void*)(base+game->sprite_draw_menu),menu_draw,(void**)&menu_sprite_original)!=MH_OK) ||
        MH_CreateHook((void*)(base+game->vm_start[0]),vm_start_0,(void**)&vm_start_original[0])!=MH_OK ||
        MH_CreateHook((void*)(base+game->vm_start[1]),vm_start_1,(void**)&vm_start_original[1])!=MH_OK ||
        MH_QueueEnableHook(MH_ALL_HOOKS)!=MH_OK || MH_ApplyQueued()!=MH_OK) {
        LOG("Sprite hook installation failed");MH_Uninitialize();return 0;
    }
    if (!patch_commit()) {LOG("Code patch commit failed");MH_Uninitialize();return 0;}
    LOG("Installed at image=%p relay=%p; original executable unchanged",(void*)base,relay_page);
    return 1;
}
BOOL WINAPI DllMain(HINSTANCE self,DWORD reason,LPVOID reserved) {
    (void)reserved;
    if (reason==DLL_PROCESS_ATTACH) {module=self;DisableThreadLibraryCalls(self);}
    return TRUE;
}
