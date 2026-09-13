/* Settings and status for a fixed-60 engine; menu contents and input handling are shared. */
static int pending_rate, menu_key_code=VK_F11;
int hfr_ui_get(int id) {
    switch (id) {
    case UI_FIXED_LOGIC: return 1;
    case UI_SOFTWARE_CURSOR: return 1;
    case UI_FPS: return fps;
    case UI_VSYNC: return vsync;
    case UI_ENEMY_INTERP: return interpolate;
    case UI_SUBTICK_INPUT: return subtick;
    case UI_SUBSTEP: return substep;
    case UI_SUBTICK_AVAILABLE: return game->player_motion!=0;
    case UI_SUBSTEP_AVAILABLE: return game->projectile!=0;
    case UI_DEBUG: return debug;
    default: return 0;
    }
}
void hfr_ui_set(int id,int value) {
    switch (id) {
    case UI_FPS: fps=value<=0?0:(value<60?60:(value>1000?1000:value));pending_rate=1;break;
    case UI_VSYNC: vsync=!!value;break;
    case UI_ENEMY_INTERP: interpolate=!!value;memset(history,0,sizeof history);break;
    case UI_SUBTICK_INPUT: subtick=!!value;memset(history,0,sizeof history);break;
    /* Forget where the projectile motion had got to, so the frame the switch is thrown in
       keeps the whole-frame step the native pass already gave it and the slices start
       clean at the next frame. */
    case UI_SUBSTEP: substep=!!value;proj_slice.moved_to=0;memset(history,0,sizeof history);break;
    case UI_DEBUG: debug=!!value;break;
    }
}
static void save_int(const char* section,const char* key,int value) {
    char text[32];snprintf(text,sizeof text,"%d",value);
    if (!WritePrivateProfileStringA(section,key,text,ini)) LOG("Could not save %s.%s (error %lu)",section,key,GetLastError());
}
void hfr_ui_save(void) {
    save_int("hfr","fps",fps);save_int("hfr","debug",debug);
    save_int("fixed60","vsync",vsync);save_int("fixed60","interpolate",interpolate);
    save_int("fixed60","subtick",subtick);save_int("fixed60","substep",substep);
}
void hfr_ui_status(char* out,int n) {
    snprintf(out,n,"%s | %d FPS target | 60 Hz gameplay%s",game->name,rate,guard_failed?" | DRAW GUARD FAILED":"");
}
void hfr_ui_rate_info(char* out,int n) {
    if (measured_present<=0) {snprintf(out,n,"measuring...");return;}
    snprintf(out,n,"presenting %.0f/s, simulating %.0f/s",measured_present,measured_update);
}
void hfr_ui_scale_info(char* out,int n) {snprintf(out,n,"D3D11 | native game scaling | %s",subtick_active()?"sub-tick player movement":"position interpolation");}
int hfr_ui_menu_key(void) {return menu_key_code;}
int hfr_ui_simulation_locked(void) {return 0;} /* Presentation never reconfigures native simulation. */
int hfr_ui_simulation_patched(void) {return 1;}
int hfr_ui_filter_count(void) {return 0;}
const char* hfr_ui_filter_name(int i) {(void)i;return "Unavailable";}
int hfr_ui_filter_is_fixed_scale(int i) {(void)i;return 0;}
int hfr_ui_post_count(void) {return 0;}
const char* hfr_ui_post_name(int i) {(void)i;return "Unavailable";}
const char* hfr_ui_dim_special_name(void) {return NULL;}
int hfr_ui_system_count(void) {return 0;}
const char* hfr_ui_system_name(int i) {(void)i;return "";}
int hfr_ui_system_get(int i) {(void)i;return 0;}
void hfr_ui_system_set(int i,int v) {(void)i;(void)v;}
const char* hfr_ui_present_path(void) {return "DxLib / D3D11";}
void hfr_ui_report(const char* fmt,...) {
    if (!logfile) return;
    va_list ap;va_start(ap,fmt);vfprintf(logfile,fmt,ap);va_end(ap);fputc('\n',logfile);fflush(logfile);
}
