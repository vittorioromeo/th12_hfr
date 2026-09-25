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
    case UI_DIM_AVAILABLE: return (game->dim_rule_count!=0 || game->dim_pool_count!=0) && game->vm_colour!=0;
    case UI_DIM_CLASSES: {
        int mask=0;
        for (size_t i=0;i<game->dim_rule_count;++i) {
            int c=game->dim_rules[i].category;
            if (c>=0 && c<DIM_COUNT) mask|=1<<c;
        }
        for (size_t i=0;i<game->dim_pool_count;++i) {
            int c=game->dim_pools[i].category;
            if (c>=0 && c<DIM_COUNT) mask|=1<<c;
        }
        return mask;
    }
    case UI_DIM_BACKGROUND: case UI_DIM_ITEMS: case UI_DIM_EFFECTS:
    case UI_DIM_SPECIAL: case UI_DIM_PLAYER_SHOTS:
        return dim_percent[id-UI_DIM_BACKGROUND];
    case UI_DEBUG: return debug;
    case UI_GAME_SPEED: return speed_pct;
    case UI_SPEED_KEY_SLOWER: return speed_keys[0];
    case UI_SPEED_KEY_FASTER: return speed_keys[1];
    case UI_SPEED_KEY_RESET: return speed_keys[2];
    case UI_SPEED_KEYS: return speed_keys_on;
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
    case UI_DIM_BACKGROUND: case UI_DIM_ITEMS: case UI_DIM_EFFECTS:
    case UI_DIM_SPECIAL: case UI_DIM_PLAYER_SHOTS:
        dim_percent[id-UI_DIM_BACKGROUND]=value<0?0:(value>100?100:value);break;
    case UI_DEBUG: debug=!!value;break;
    case UI_SPEED_KEYS: speed_keys_on=!!value;break;
    case UI_GAME_SPEED:
        value=value<10?10:(value>1600?1600:value);
        if (value!=speed_pct) {speed_pct=value;LOG("game speed: %d%%",speed_pct);}
        break;
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
    for (int i=0;i<DIM_COUNT;++i) {char key[32];snprintf(key,sizeof key,"dim_%s",DIM_NAMES[i]);
                                  save_int("video",key,dim_percent[i]);}
    save_int("video","speed_keys",speed_keys_on);
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
int hfr_ui_toggle_count(void) {return 0;}
const char* hfr_ui_toggle_label(int i) {(void)i;return "";}
const char* hfr_ui_toggle_tip(int i) {(void)i;return "";}
int hfr_ui_toggle_get(int i) {(void)i;return 0;}
void hfr_ui_toggle_set(int i,int v) {(void)i;(void)v;}
int hfr_ui_system_count(void) {return 0;}
const char* hfr_ui_system_name(int i) {(void)i;return "";}
int hfr_ui_system_get(int i) {(void)i;return 0;}
void hfr_ui_system_set(int i,int v) {(void)i;(void)v;}
const char* hfr_ui_present_path(void) {return "DxLib / D3D11";}
void hfr_ui_report(const char* fmt,...) {
    if (!logfile) return;
    va_list ap;va_start(ap,fmt);vfprintf(logfile,fmt,ap);va_end(ap);fputc('\n',logfile);fflush(logfile);
}
