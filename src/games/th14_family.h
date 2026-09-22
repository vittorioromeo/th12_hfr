#pragma once
/* The enemy and option sprite placement TH14 and TH15 share: one engine, different offsets.
   TH14_DEVNOTES 9 says what these are for; each game's adapter supplies the numbers. */
struct Th14EnemySprites {
    uint32_t sub;            /* the enemy's sub-object holding the three arrays below */
    uint32_t ids, offs, parent;   /* 14 VM ids, 14 x three float offsets, 14 parent slots */
    uint32_t absolute;       /* flag bit: the sprite positions are absolute */
    unsigned vm_pos;         /* float index of a VM's position */
    unsigned parent_pos;     /* float index of what a parent VM contributes */
};
static void th14_family_place_enemy(const struct Th14EnemySprites* L, uint8_t* e, uint8_t* am, uint32_t flags, const float* R) {
    uint8_t* in = e + L->sub;
    int* ids = (int*)(in + L->ids); float* offs = (float*)(in + L->offs); int* parent = (int*)(in + L->parent);
    for (int i = 0; i < 14; i++) {
        if (!ids[i]) continue;
        float* vm = anm_get_vm(am, ids[i]);
        if (!vm) continue;
        float x = R[0], y = R[1], z = R[2];
        if (!(flags & L->absolute)) {
            x += offs[i*3]; y += offs[i*3+1]; z += offs[i*3+2];
            if (parent[i] >= 0 && parent[i] < 14 && ids[parent[i]]) {
                float* pvm = anm_get_vm(am, ids[parent[i]]);
                if (pvm) { x += pvm[L->parent_pos]; y += pvm[L->parent_pos + 1]; z += pvm[L->parent_pos + 2]; }
            }
        }
        vm[L->vm_pos] = x; vm[L->vm_pos + 1] = y; vm[L->vm_pos + 2] = z;
    }
}
/* `count` options of `stride` bytes at player+`base` (eight of 0xe4 on TH14 and TH15, four of
   0xf0 on TH18): active flag at +0x00, position as fixed point in 1/128 of a pixel at +0x5c and
   +0x60, the two ANM VM ids at +0xb0 and +0xb4. */
struct Th14OptionState { int32_t last[2], prev[2]; unsigned seen; };
static void th14_family_place_options(uint32_t base, uint32_t stride, int count, unsigned vm_pos,
                                      struct Th14OptionState* st, unsigned* frames,
                                      uint8_t* am, float alpha, int capture) {
    uint8_t* pl = g_game->addr.player ? *(uint8_t**)g_game->addr.player : NULL;
    if (!pl) return;
    if (capture) ++*frames;
    for (int i = 0; i < count; ++i) {
        uint8_t* o = pl + base + i * stride;
        if (!*(const uint32_t*)o) { st[i].seen = 0; continue; }   /* this one is not out */
        const int32_t* P = (const int32_t*)(o + 0x5c);
        if (capture) {
            /* Seen on the previous frame: last becomes prev. Otherwise it has just appeared and
               has no motion to show yet. */
            if (st[i].seen && st[i].seen == *frames - 1) { st[i].prev[0] = st[i].last[0]; st[i].prev[1] = st[i].last[1]; }
            else { st[i].prev[0] = P[0]; st[i].prev[1] = P[1]; }
            st[i].last[0] = P[0]; st[i].last[1] = P[1];
            st[i].seen = *frames;
        } else if (st[i].seen != *frames) {
            st[i].prev[0] = st[i].last[0] = P[0];
            st[i].prev[1] = st[i].last[1] = P[1];
            st[i].seen = *frames;
        }
        float d[2] = { (float)(st[i].last[0] - st[i].prev[0]), (float)(st[i].last[1] - st[i].prev[1]) };
        /* A whole screen in one frame is the game putting an option somewhere, not moving it. */
        if (d[0] < -6144.0f || d[0] > 6144.0f || d[1] < -6144.0f || d[1] > 6144.0f) d[0] = d[1] = 0;
        float x = ((float)st[i].last[0] - d[0] * (1.0f - alpha)) * (1.0f / 128.0f);
        float y = ((float)st[i].last[1] - d[1] * (1.0f - alpha)) * (1.0f / 128.0f);
        for (int k = 0; k < 2; ++k) {
            int id = *(const int*)(o + 0xb0 + k * 4);
            if (!id) continue;
            float* vm = anm_get_vm(am, id);
            if (!vm) continue;
            vm[vm_pos] = x; vm[vm_pos + 1] = y; vm[vm_pos + 2] = 0.0f;
        }
    }
}
