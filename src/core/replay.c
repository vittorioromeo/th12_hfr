/* ------------------------------------------------------------------ replay awareness */
/* A replay's rate is retained across device resets; no chunk means stock 60 Hz. */
/* HFRM v1: identifies the simulation, not just the presentation rate.
   USER payload: magic[4], u16 schema, u16 game, u32 simulation revision,
   u32 flags (substep=1, subtick_input=2), u32 node mask, u32 logic rate. */
#define HFR_SIMULATION_REVISION 1
#define HFR_META_CHUNK_TYPE 0x4a
struct ReplaySettings { uint32_t flags, nodes, rate; };
static struct ReplaySettings g_replay_settings, g_saved_settings;
static int g_replay_metadata, g_settings_active;
static struct ReplaySettings current_settings(void) {
    struct ReplaySettings s={(cfg.substep?1u:0u)|(cfg.subtick_input?2u:0u),0,(uint32_t)g_logic_rate};
    for(size_t i=0;i<g_class_count;++i) if(g_sub_enabled[i]) s.nodes|=1u<<i;
    return s;
}
static void apply_settings(struct ReplaySettings s) {
    cfg.substep=!!(s.flags&1);cfg.subtick_input=!!(s.flags&2);
    for(size_t i=0;i<g_class_count;++i)g_sub_enabled[i]=!!(s.nodes&(1u<<i));
}
static void restore_replay_settings(void) {
    if(g_settings_active){apply_settings(g_saved_settings);g_settings_active=0;}
}
static void replay_write_metadata(uint8_t out[36]) {
    memset(out,0,36);memcpy(out,"USER",4);uint32_t size=36;memcpy(out+4,&size,4);out[8]=HFR_META_CHUNK_TYPE;
    memcpy(out+12,"HFRM",4);uint16_t schema=1,game=(uint16_t)g_game->identity->id;
    uint32_t revision=HFR_SIMULATION_REVISION;struct ReplaySettings settings=current_settings();
    memcpy(out+16,&schema,2);memcpy(out+18,&game,2);memcpy(out+20,&revision,4);
    memcpy(out+24,&settings.flags,4);memcpy(out+28,&settings.nodes,4);memcpy(out+32,&settings.rate,4);
}
static int replay_parse_metadata(const uint8_t* p,uint32_t size) {
    if(size!=36 || memcmp(p+12,"HFRM",4))return -1;
    uint16_t schema,game;uint32_t revision;struct ReplaySettings settings;
    memcpy(&schema,p+16,2);memcpy(&game,p+18,2);memcpy(&revision,p+20,4);
    memcpy(&settings.flags,p+24,4);memcpy(&settings.nodes,p+28,4);memcpy(&settings.rate,p+32,4);
    if(schema!=1 || game!=g_game->identity->id || revision!=HFR_SIMULATION_REVISION ||
        (settings.flags&~3u) || (g_class_count<32 && (settings.nodes>>g_class_count)) ||
        settings.rate<60 || settings.rate>1000 || (!(settings.flags&1) && settings.rate!=60))return -1;
    g_replay_settings=settings;return 1;
}
static void replay_check(void) {
    uint8_t* rm = G_REPLAY_MANAGER;
    int playing = rm && *(int*)(rm + 0x10) == 1;
    if (playing != g_replay_playing) {
        if(playing && g_replay_metadata==1) {
            g_saved_settings=current_settings();apply_settings(g_replay_settings);g_settings_active=1;
            LOG("Applied recorded simulation settings (revision %u)",HFR_SIMULATION_REVISION);
        } else if(!playing) restore_replay_settings();
        g_replay_playing = playing;
        /* A replay with no recorded rate is a stock replay, so playback drops to 60 and runs the
           simulation the file was made with. That is right -- and it is also why "record a stock
           replay, play it back with sub-stepping on" proves nothing: this line quietly turns the
           sub-stepping off again, so the test compared stock with stock. `replay_trace` keeps the
           current rate instead, which is what makes that test say something. */
        int want = playing ? (g_replay_rate ? g_replay_rate : 60) : g_refresh;
        if (playing && cfg.replay_trace) want = g_logic_rate;
        if (cfg.fps > 0 && !playing) want = cfg.fps;
        LOG("replay playback %s -> logic rate %d", playing ? "started" : "ended", want);
        set_logic_rate(want);
    }
}


/* ------------------------------------------------------------------ replay file chunk (recording rate) */
#define HFR_CHUNK_TYPE 0x48   /* 'H' : our USER chunk type */
/* Where the game keeps its replays: beside the executable, unless the game has a data
   directory of its own (TH13 keeps a "%APPDATA%\ShanghaiAlice\th13\" string, with the
   trailing separator, and chdirs into it around every save and load; empty when APPDATA
   is unset, in which case it falls back to the game directory like the older games). */
static int replay_path(char* out, size_t n, const char* name) {
    char dir[MAX_PATH];
    const char* data = g_game && g_game->addr.data_dir ? (const char*)g_game->addr.data_dir : "";
    if (*data) { strncpy(dir, data, MAX_PATH - 1); dir[MAX_PATH - 1] = 0; size_t l = strlen(dir); if (l && dir[l-1] == '\\') dir[l-1] = 0; }
    else { GetModuleFileNameA(NULL, dir, MAX_PATH); char* p = strrchr(dir, '\\'); if (p) *p = 0; }
    size_t a=strlen(dir),b=strlen(name);
    if (a+8+b+1>n) { LOG("Replay path is too long");return 0; }
    memcpy(out,dir,a);memcpy(out+a,"\\replay\\",8);memcpy(out+a+8,name,b+1);return 1;
}
#define HFR_INPUT_CHUNK_TYPE 0x49   /* 'I' : per-tick input chunk: "HFRI", u16 version, u16 rate, u8 nstages, pad[3],
                                       then per stage: u8 stage, pad[3], u32 nticks, u32 npairs, npairs x {u8 bits, u8 run} */
/* Build the complete extension before opening the replay. A failed allocation
   cannot produce a truncated USER chunk or a partially recorded stage stream. */
static uint8_t* replay_build_extension(uint32_t* out_size) {
    size_t cap=128;uint8_t* rm=G_REPLAY_MANAGER;
    if(cfg.subtick_input && rm) for(int i=0;i<8;++i) {
        if(g_rec[i].failed || g_rec[i].n>3600000 || (g_rec[i].n && !g_rec[i].d))return NULL;
        cap+=12+2*(size_t)g_rec[i].n;
    }
    uint8_t* out=calloc(1,cap);if(!out)return NULL;
    int len=snprintf((char*)out+12,52,"touhou_hfr rate=%d",g_logic_rate)+1;
    uint32_t used=(12+len+3)&~3u;
    memcpy(out,"USER",4);memcpy(out+4,&used,4);out[8]=HFR_CHUNK_TYPE;
    replay_write_metadata(out+used);used+=36;
    if(cfg.subtick_input && rm && cfg.substep && g_logic_rate!=60) {
        uint32_t start=used;used+=24;int stages=0;
        for(int s=0;s<8;++s) {
            if(!*(uint32_t*)(rm+g_game->layout.replay_stages+s*4) || !g_rec[s].n)continue;
            uint8_t* hdr=out+used;used+=12;uint32_t pairs=0;
            for(uint32_t i=0;i<g_rec[s].n;) {
                uint8_t v=g_rec[s].d[i];uint32_t j=i;
                while(j<g_rec[s].n && g_rec[s].d[j]==v && j-i<255)++j;
                out[used++]=v;out[used++]=(uint8_t)(j-i);++pairs;i=j;
            }
            hdr[0]=(uint8_t)s;memcpy(hdr+4,&g_rec[s].n,4);memcpy(hdr+8,&pairs,4);++stages;
        }
        if(stages) {
            used=(used+3)&~3u;uint32_t size=used-start;uint8_t* hdr=out+start;
            memcpy(hdr,"USER",4);memcpy(hdr+4,&size,4);hdr[8]=HFR_INPUT_CHUNK_TYPE;
            memcpy(hdr+12,"HFRI",4);uint16_t version=1,rate=(uint16_t)g_logic_rate;
            memcpy(hdr+16,&version,2);memcpy(hdr+18,&rate,2);hdr[20]=(uint8_t)stages;
        }else used=start;
    }
    *out_size=used;return out;
}
static void replay_append_chunk(const char* name) {
    uint32_t size=0;uint8_t* extension=replay_build_extension(&size);
    if(!extension){LOG("Cannot build replay metadata/input: recording too large or out of memory");return;}
    char path[MAX_PATH];if(!replay_path(path,sizeof path,name)){free(extension);return;}
    HANDLE h=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h!=INVALID_HANDLE_VALUE) {
        DWORD start=SetFilePointer(h,0,NULL,FILE_END),written=0;
        if(start!=INVALID_SET_FILE_POINTER && WriteFile(h,extension,size,&written,NULL) && written==size)
            LOG("Replay extension written: %s, rate %d, simulation %u, %u bytes",name,g_logic_rate,HFR_SIMULATION_REVISION,size);
        else {
            if(start!=INVALID_SET_FILE_POINTER){SetFilePointer(h,start,NULL,FILE_BEGIN);SetEndOfFile(h);}
            LOG("Replay extension write failed; original replay retained");
        }
        CloseHandle(h);
    }else LOG("Cannot open replay to append metadata: %s",path);
    free(extension);
}
static void replay_parse_input_chunk(const uint8_t* p, uint32_t size) {
    for (int s = 0; s < 8; s++) g_play[s].n = 0;
    if (size < 24 || memcmp(p + 12, "HFRI", 4) != 0) return;
    uint16_t ver, rate; memcpy(&ver, p + 16, 2); memcpy(&rate, p + 18, 2); int nst = p[20];
    if (ver != 1 || rate < 60 || rate > 1000 || nst > 8) {
        LOG("replay input chunk: unsupported header"); return;
    }
    /* Validate every RLE stage before allocating or accepting any stream. */
    uint32_t check = 24, seen = 0;
    for (int k = 0; k < nst; ++k) {
        if (check > size || size - check < 12) return;
        unsigned s = p[check]; uint32_t nt, np;
        memcpy(&nt,p+check+4,4); memcpy(&np,p+check+8,4); check+=12;
        if (s>=8 || (seen & (1u<<s)) || nt>3600000 || np>(size-check)/2) return;
        seen |= 1u<<s;
        uint32_t sum=0;
        for (uint32_t i=0;i<np;++i) {
            unsigned bits=p[check+2*i], run=p[check+2*i+1];
            if (bits>31 || !run || sum>nt || run>nt-sum) return;
            sum+=run;
        }
        if (sum!=nt) return;
        check+=2*np;
    }
    uint32_t off=24;
    for(int k=0;k<nst;++k) {
        unsigned stage=p[off];uint32_t nt,np;memcpy(&nt,p+off+4,4);memcpy(&np,p+off+8,4);off+=12;
        struct TickBuf* b=&g_play[stage];
        if(nt>b->cap) {
            uint8_t* data=realloc(b->d,nt);
            if(!data){for(int i=0;i<8;++i)g_play[i].n=0;return;}
            b->d=data;b->cap=nt;
        }
        for(uint32_t i=0;i<np;++i){unsigned run=p[off+2*i+1];memset(b->d+b->n,p[off+2*i],run);b->n+=run;}
        off+=2*np;
    }
}
static int replay_read_chunk(const char* name) {
    g_replay_metadata=0;
    for (int s = 0; s < 8; s++) g_play[s].n = 0;
    char path[MAX_PATH]; if (!replay_path(path, sizeof path, name)) return 0;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD sz = GetFileSize(h, NULL); int rate = 0;
    if (sz > 0 && sz < 64 * 1024 * 1024) {
        uint8_t* d = (uint8_t*)malloc(sz + 1); DWORD rd = 0;
        if (d && ReadFile(h, d, sz, &rd, NULL) && rd == sz) {
            d[sz] = 0;
            DWORD start = sz;
            if (sz >= 0x24 && memcmp(d, g_game->identity->replay_magic, 4) == 0) { uint32_t uo; memcpy(&uo, d + 0xc, 4); if (uo >= 0x24 && uo < sz) start = uo; }   /* header +0xc: user data offset */
            for (DWORD i = start; i + 16 <= sz; i++) {
                if (memcmp(d + i, "USER", 4) != 0) continue;
                uint32_t csize; memcpy(&csize, d + i + 4, 4);
                if (csize < 12 || csize > sz - i) continue;
                if (d[i + 8] == HFR_CHUNK_TYPE) {
                    char t[64]; size_t len=csize-12; if(len>=sizeof t)len=sizeof t-1;
                    memcpy(t,d+i+12,len);t[len]=0; const char* k = strstr(t, "rate=");
                    if (k) rate = atoi(k + 5);
                } else if(d[i+8]==HFR_META_CHUNK_TYPE) {
                    g_replay_metadata=replay_parse_metadata(d+i,csize);
                } else if (d[i + 8] == HFR_INPUT_CHUNK_TYPE) {
                    replay_parse_input_chunk(d + i, csize);
                }
                i += csize - 1;
            }
        }
        free(d);
    }
    CloseHandle(h);
    if(g_replay_metadata==1)rate=g_replay_settings.rate;
    return rate >= 60 && rate <= 1000 ? rate : 0;
}
typedef void (__fastcall *ReplaySaveFn)(char* filename, char* name, int p3);
typedef void (__stdcall *ReplayLoadFn)(void* mgr, char* filename);
#define orig_replay_save ((ReplaySaveFn)g_game->addr.replay_save)
#define orig_replay_load ((ReplayLoadFn)g_game->addr.replay_load)
static void __fastcall hfr_replay_save(char* filename, char* name, int p3) {
    orig_replay_save(filename, name, p3);
    replay_append_chunk(filename);
}
static void replay_loaded(const char* filename) {
    g_replay_rate = replay_read_chunk(filename);
    LOG("replay %s loaded for playback: recorded rate %d", filename, g_replay_rate);
    if(g_replay_metadata<0) {
        LOG("WARNING: unsupported replay simulation metadata; playback may desynchronize");
        MessageBoxA(NULL,"This replay uses different or invalid Touhou HFR simulation metadata. Playback may desynchronize. Use the build that recorded it for accurate playback.","Touhou HFR replay compatibility",MB_OK|MB_ICONWARNING);
    } else if(g_replay_rate && !g_replay_metadata) LOG("Legacy HFR replay: rate/input retained, simulation version unknown; use its original build if playback desynchronizes");
}
static void __stdcall hfr_replay_load(void* mgr, char* filename) {
    restore_replay_settings();g_replay_playing=0;
    orig_replay_load(mgr, filename);
    replay_loaded(filename);
}
