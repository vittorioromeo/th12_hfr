/* Synthetic replay fixtures exercise our extension, not the game's compression. */
static void test_replay_roundtrip(void) {
    uint8_t manager[0x300]={0};
    G_REPLAY_MANAGER=manager;
    for(size_t i=0;i<g_class_count;++i)g_sub_enabled[i]=(i%2)==0;
    cfg.substep=1;cfg.subtick_input=1;g_refresh=360;set_logic_rate(144);
    for(int s=0;s<8;++s) {
        *(uint32_t*)(manager+g_game->layout.replay_stages+s*4)=1;
        g_rec[s].n=0;g_rec[s].failed=0;
        for(int i=0;i<1031;++i)tickbuf_push(&g_rec[s],(uint8_t)((i/300+s)%32));
    }
    struct ReplaySettings expected=current_settings();
    uint32_t size;uint8_t* out=replay_build_extension(&size);assert(out && size>72);
    unsigned chunks=0;
    for(uint32_t i=0;i<size;) {
        uint32_t n;memcpy(&n,out+i+4,4);assert(n>=12 && n<=size-i);++chunks;
        if(out[i+8]==HFR_META_CHUNK_TYPE) {
            assert(replay_parse_metadata(out+i,n)==1);
            assert(!memcmp(&expected,&g_replay_settings,sizeof expected));
            out[i+20]^=1;assert(replay_parse_metadata(out+i,n)==-1);out[i+20]^=1;
            out[i+18]^=1;assert(replay_parse_metadata(out+i,n)==-1);out[i+18]^=1;
            assert(replay_parse_metadata(out+i,n-1)==-1);
        }else if(out[i+8]==HFR_INPUT_CHUNK_TYPE) {
            replay_parse_input_chunk(out+i,n);
            for(int s=0;s<8;++s)assert(g_play[s].n==g_rec[s].n && !memcmp(g_play[s].d,g_rec[s].d,g_rec[s].n));
        }
        i+=n;
    }
    assert(chunks==3);free(out);
    /* Exercise real file append/read and the native USER offset. */
    char dir[MAX_PATH];GetModuleFileNameA(NULL,dir,sizeof dir);char* slash=strrchr(dir,'\\');assert(slash);strcpy(slash+1,"replay");CreateDirectoryA(dir,NULL);
    char path[MAX_PATH];assert(replay_path(path,sizeof path,"hfr_synthetic.rpy"));
    FILE* f=fopen(path,"wb");assert(f);uint8_t header[0x24]={0};memcpy(header,g_game->identity->replay_magic,4);header[0xc]=0x24;
    assert(fwrite(header,1,sizeof header,f)==sizeof header);fclose(f);
    replay_append_chunk("hfr_synthetic.rpy");
    assert(replay_read_chunk("hfr_synthetic.rpy")==144 && g_replay_metadata==1);
    for(int s=0;s<8;++s)assert(g_play[s].n==1031 && !memcmp(g_rec[s].d,g_play[s].d,1031));
    cfg.substep=0;cfg.subtick_input=0;g_sub_enabled[0]=0;
    struct ReplaySettings before=current_settings();
    g_replay_rate=144;g_replay_playing=0;*(int*)(manager+0x10)=1;
    replay_check();assert(cfg.substep && cfg.subtick_input && g_logic_rate==144 && g_sub_enabled[0]);
    recompute_rate(360);assert(g_logic_rate==144);
    *(int*)(manager+0x10)=0;replay_check();
    assert(!cfg.substep && !cfg.subtick_input && g_logic_rate==60 && !g_sub_enabled[0]);
    struct ReplaySettings after=current_settings();assert(before.flags==after.flags && before.nodes==after.nodes);
    cfg.substep=1;cfg.subtick_input=1;
    g_rec[1].failed=1;assert(!replay_build_extension(&size));g_rec[1].failed=0;
    /* Stock replays still get simulation metadata, but no high-rate input. */
    cfg.substep=0;set_logic_rate(360);out=replay_build_extension(&size);assert(out);
    uint32_t n;memcpy(&n,out+4,4);assert(replay_parse_metadata(out+n,size-n)==1 && g_replay_settings.rate==60);
    free(out);DeleteFileA(path);G_REPLAY_MANAGER=NULL;cfg.substep=1;
    puts("PASS: replay extension/file round-trip (8 stages), metadata compatibility, settings restore, stock mode, failed recording");
}
