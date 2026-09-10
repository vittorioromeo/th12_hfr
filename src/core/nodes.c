/* ------------------------------------------------------------------ node classification */
static int g_sub_enabled[MAX_NODE_CLASSES];

static int node_mode(uint32_t func) {
    if (!cfg.substep) return MODE_FRAME;
    for (size_t i = 0; i < g_class_count; i++)
        if (g_classes[i].func == func) return g_sub_enabled[i] ? g_classes[i].mode : MODE_FRAME;
    return MODE_FRAME;
}
