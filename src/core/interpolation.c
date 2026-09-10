/* ------------------------------------------------------------------ enemy render interpolation
 * Enemies (ECL) stay on stock 60 Hz logic. To make them move smoothly we place their sprites, on every
 * tick, at the position interpolated between the last two frame positions (same lag as sub-stepped objects).
 */
#define G_ENEMY_MANAGER   (*(uint8_t**)g_game->addr.enemy_manager)
#define G_ANM_MANAGER     (*(uint8_t**)g_game->addr.anm_manager)
typedef float* (*GetVmFn)(uint8_t* mgr, int id);
static float* anm_get_vm(uint8_t* mgr, int id) {
    float* r; uint8_t* m = mgr;
    __asm__ volatile ("push %2\n\t" "call *%3\n\t" : "=a"(r), "+d"(m) : "r"(id), "r"(g_game->addr.anm_get_vm) : "ecx", "memory", "cc");
    return r;
}
struct EnemyTrack { uint8_t* enemy; float last[3]; float prev[3]; unsigned seen; };
#define TRACK_N 2048
static struct EnemyTrack g_tracks[TRACK_N];
static unsigned g_major_count;
static struct EnemyTrack* track_find(uint8_t* e) {
    unsigned h = ((uintptr_t)e >> 4) & (TRACK_N - 1);
    struct EnemyTrack* stale = NULL;
    for (unsigned i = 0; i < 64; i++) {
        struct EnemyTrack* t = &g_tracks[(h + i) & (TRACK_N - 1)];
        if (t->enemy == e) return t;
        if (!stale && (t->enemy == NULL || t->seen + 2 < g_major_count)) stale = t;   /* free or not seen for 2 frames */
    }
    if (stale) { stale->enemy = NULL; }
    return stale;
}
static void enemy_interp(double phase) {
    if (!cfg.substep || !cfg.enemy_interp || !g_game->place_enemy) return;
    uint8_t* em = G_ENEMY_MANAGER; uint8_t* am = G_ANM_MANAGER;
    if (!em || !am) return;
    int capture = g_major && !g_skip_update;
    if (capture) g_major_count++;
    float alpha = (float)(phase + g_dt); if (alpha > 1.0f) alpha = 1.0f;   /* fraction of the frame's motion to show */
    uint32_t list = g_game->layout.enemy_list ? g_game->layout.enemy_list : 0x68;
    for (uint32_t* node = *(uint32_t**)(em + list); node; node = (uint32_t*)node[1]) {
        uint8_t* e = (uint8_t*)node[0];
        if (!e) continue;
        uint32_t flags = *(uint32_t*)(e + g_game->layout.enemy_flags);
        if (flags & g_game->layout.enemy_skip_mask) continue; /* adapter-specific excluded enemy states */
        float* P = (float*)(e + g_game->layout.enemy_position);
        struct EnemyTrack* t = track_find(e);
        if (!t) continue;
        if (capture) {
            if (t->enemy == e && t->seen == g_major_count - 1) { memcpy(t->prev, t->last, 12); }
            else { memcpy(t->prev, P, 12); }
            memcpy(t->last, P, 12); t->enemy = e; t->seen = g_major_count;
        } else if (t->enemy != e) { memcpy(t->prev, P, 12); memcpy(t->last, P, 12); t->enemy = e; t->seen = g_major_count; }
        float d[3] = { t->last[0] - t->prev[0], t->last[1] - t->prev[1], t->last[2] - t->prev[2] };
        if (fabsf(d[0]) > 48.0f || fabsf(d[1]) > 48.0f) d[0] = d[1] = d[2] = 0;   /* teleport */
        float R[3] = { t->last[0] - d[0] * (1.0f - alpha), t->last[1] - d[1] * (1.0f - alpha), t->last[2] - d[2] * (1.0f - alpha) };
        g_game->place_enemy(e, am, flags, R);
    }
}
