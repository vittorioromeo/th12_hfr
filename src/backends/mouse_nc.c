/* Verified only for th06nc_0914, already identified by full executable SHA256.
   6de0 adds the live origin at 53cac0; 3000/3c90 scale by c6e068 / 480.
   The original poll returns a word; its caller at 7b8b2 stores current/previous
   input. Hooking the return therefore keeps native key edges and the recorder. */
static HWND mouse_window_handle;
static PollFn mouse_poll_original;
static struct MouseButtons mouse_buttons={7};
static int mouse_supported,mouse_enabled,mouse_bomb_button=1,mouse_toggle_held=1;
static int mouse_active;
static unsigned mouse_output;
static float mouse_target[2],mouse_target_fraction[2];
static int mouse_direct=1,mouse_anchor_ready;
static int mouse_speed_limit;
static float mouse_pending[2];
static uint64_t mouse_limited_tick=UINT64_MAX;
static float mouse_last_cursor[2];
static unsigned mouse_native_input;

void hfr_mouse_window(HWND hwnd) { mouse_window_handle=hwnd; }
static void mouse_set_enabled(int on) {
    mouse_enabled=!!on; mouse_active=0; mouse_buttons.blocked=7; mouse_anchor_ready=0;
    LOG("Mouse control %s (F10 toggles, F11 Mouse tab)",mouse_enabled?"enabled":"disabled");
}
static uint32_t mouse_sample(uint32_t input,int native_tick) {
    int foreground=mouse_window_handle && GetForegroundWindow()==mouse_window_handle;
    int key=(GetAsyncKeyState(VK_F10)&0x8000)!=0;
    if (foreground && !hfr_menu_visible()) {
        if (key && !mouse_toggle_held) mouse_set_enabled(!mouse_enabled);
        mouse_toggle_held=key;
    } else mouse_toggle_held=1;
    /* The native movement/projectile sites stop executing in pause/menu states.
       Check their last completed native tick, and suppress the Escape edge now
       so no generated direction reaches the pause menu on the transition tick. */
    int active=mouse_enabled && foreground && !hfr_menu_visible() &&
        (subtick_player.armed || proj_slice.armed) && !(input&0x08) &&
        !(GetAsyncKeyState(VK_ESCAPE)&0x8000) && !guard_failed;
    POINT cursor; RECT client;
    const float* bounds=(const float*)(base+game->bounds);
    const float* origin=(const float*)(base+0x53cac0);
    int rw=*(const int*)(base+0xc6e064),rh=*(const int*)(base+0xc6e068);
    if (active) {
        active=GetCursorPos(&cursor) && ScreenToClient(mouse_window_handle,&cursor) &&
               GetClientRect(mouse_window_handle,&client) &&
               mouse_follow_target((float)cursor.x,(float)cursor.y,client.right,client.bottom,
                                   rw,rh,origin,bounds,&mouse_target[0],&mouse_target[1]);
    }
    unsigned raw=((GetAsyncKeyState(VK_LBUTTON)&0x8000)?1:0) |
                 ((GetAsyncKeyState(VK_RBUTTON)&0x8000)?2:0) |
                 ((GetAsyncKeyState(mouse_bomb_button==2?VK_XBUTTON2:VK_XBUTTON1)&0x8000)?4:0);
    input |= mouse_follow_buttons(&mouse_buttons,raw,active);
    mouse_active=active;
    if (!active || !subtick_player.armed || (input&0xf0)) {
        mouse_anchor_ready=0;mouse_pending[0]=mouse_pending[1]=0;
    }
    if (active) {
        /* Physical arrows/controller directions take precedence while held. */
        if (subtick_player.armed && !(input&0xf0)) {
            const unsigned char* p=(const unsigned char*)(base+game->player);
            float* pos=(float*)(p+game->pl_position);
            if (mouse_direct) {
                /* Relative, unfiltered displacement. Re-anchor across menus/focus/
                   keyboard use, and keep the anchor current at walls: no jump or
                   accumulated overshoot when reversing or releasing focus. */
                float cx=(float)cursor.x/client.right*rw*480.0f/rh-origin[0];
                float cy=(float)cursor.y/client.bottom*480.0f-origin[1];
                if (mouse_anchor_ready) {
                    float gain=(input&SUBTICK_FOCUS)?0.5f:1.0f;
                    float dx=(cx-mouse_last_cursor[0])*gain,dy=(cy-mouse_last_cursor[1])*gain;
                    if (mouse_speed_limit) {
                        /* Gather reports between native updates without moving the
                           gameplay position. Consume at most one speed-limited step
                           per native tick, then discard all excess swipe distance. */
                        mouse_pending[0]+=dx;mouse_pending[1]+=dy;
                        dx=dy=0;
                        if (native_tick && mouse_limited_tick!=ticks) {
                            mouse_limited_tick=ticks;
                            dx=mouse_pending[0];dy=mouse_pending[1];
                            mouse_pending[0]=mouse_pending[1]=0;
                            const float* speeds=(const float*)(p+game->pl_speed_straight);
                            const float* scale=(const float*)(p+game->pl_scale);
                            float sx=fabsf(scale[0]),sy=fabsf(scale[1]);
                            if (!(sx>0) || !isfinite(sx)) {dx=0;sx=1;}
                            if (!(sy>0) || !isfinite(sy)) {dy=0;sy=1;}
                            float distance=hypotf(dx/sx,dy/sy);
                            float limit=speeds[(input&SUBTICK_FOCUS)!=0];
                            if (!(limit>0) || !isfinite(limit)) dx=dy=0;
                            else if (distance>limit) {float ratio=limit/distance;dx*=ratio;dy*=ratio;}
                        }
                    }
                    if (dx!=0 || dy!=0) {
                        pos[0]=subtick_clamp(pos[0]+dx,bounds[0],bounds[2]);
                        pos[1]=subtick_clamp(pos[1]+dy,bounds[1],bounds[3]);
                    }
                } else mouse_pending[0]=mouse_pending[1]=0;
                mouse_last_cursor[0]=cx;mouse_last_cursor[1]=cy;mouse_anchor_ready=1;
                /* Keep the OS pointer on the character too. This prevents reduced
                   sensitivity or a playfield clamp from exhausting desktop travel.
                   Anchoring to the rounded client point avoids synthetic drift. */
                POINT anchor={
                    (LONG)lroundf((pos[0]+origin[0])*rh/480.0f/rw*client.right),
                    (LONG)lroundf((pos[1]+origin[1])/480.0f*client.bottom)};
                POINT screen_anchor=anchor;
                if (ClientToScreen(mouse_window_handle,&screen_anchor) &&
                    SetCursorPos(screen_anchor.x,screen_anchor.y)) {
                    mouse_last_cursor[0]=(float)anchor.x/client.right*rw*480.0f/rh-origin[0];
                    mouse_last_cursor[1]=(float)anchor.y/client.bottom*480.0f-origin[1];
                }
            } else input |= mouse_follow_direction(pos[0],pos[1],mouse_target[0],mouse_target[1],
                (const float*)(p+game->pl_speed_straight),(const float*)(p+game->pl_speed_diagonal),
                (const float*)(p+game->pl_scale),bounds,(input&SUBTICK_FOCUS)!=0);
        }
        if (mouse_direct) {
            const float* pos=(const float*)(base+game->player+game->pl_position);
            mouse_target[0]=pos[0];mouse_target[1]=pos[1];
        }
        mouse_target_fraction[0]=(mouse_target[0]+origin[0])*rh/(480.0f*rw);
        mouse_target_fraction[1]=(mouse_target[1]+origin[1])/480.0f;
    }
    mouse_output=input;
    return input;
}
static uint32_t mouse_poll(uintptr_t argument) {
    mouse_native_input=mouse_poll_original(argument);
    return mouse_sample(mouse_native_input,1);
}
static void mouse_present_move(void) {
    /* Do not call the game's poll routine or mutate its button history on extra
       presentation frames. Only native ticks feed fire/focus/bomb into the game. */
    if (mouse_supported && mouse_direct) mouse_sample(mouse_native_input,0);
}
static int mouse_live_sprite(const void* vm) {
    /* Verified 6ba5a..6ba98: the player's VM is fed from live position. */
    uintptr_t v=(uintptr_t)vm;
    return mouse_supported && mouse_enabled && mouse_direct &&
        v==base+game->player+0x78c8;
}
