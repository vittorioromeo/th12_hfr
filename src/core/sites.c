static const uint8_t* site_expected(uintptr_t addr, size_t n) {
    const struct GameIdentity* game=g_game->identity;
    for (size_t i=0;i<game->signature_count;++i)
        if (game->signatures[i].addr==addr && game->signatures[i].size>=n)
            return game->signatures[i].bytes;
    LOG("INTERNAL ERROR: missing signature @%08x size=%u",(unsigned)addr,(unsigned)n);
    return NULL;
}
static void site_hook(uintptr_t addr, size_t n) { hook_site(addr,n,site_expected(addr,n)); }
static void site_call(uintptr_t addr, void* fn) { patch_call(addr,fn,site_expected(addr,5)); }
