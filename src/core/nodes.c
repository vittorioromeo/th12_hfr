/* ------------------------------------------------------------------ node classification */
static int g_sub_enabled[MAX_NODE_CLASSES];

/* Every update callback the runner has walked, with the priority it was registered at and
   whether the profile names it. This is how a new game's class table gets written: the table
   cannot be read out of the executable, because the list is built at runtime from whichever
   systems a scene created, so the honest way to learn it is to watch one stage. Reported on
   the debug stats line, once per distinct callback rather than once per frame. */
enum { NODE_CENSUS = 48 };
static struct { uint32_t func; int prio; unsigned long long calls; } g_node_census[NODE_CENSUS];
static int g_node_census_n, g_node_census_reported;
static void node_seen(uint32_t func, int prio) {
    if (!cfg.debug) return;
    for (int i=0;i<g_node_census_n;++i)
        if (g_node_census[i].func==func) { ++g_node_census[i].calls; return; }
    if (g_node_census_n>=NODE_CENSUS) return;
    g_node_census[g_node_census_n].func=func;
    g_node_census[g_node_census_n].prio=prio;
    g_node_census[g_node_census_n].calls=1;
    ++g_node_census_n; g_node_census_reported=0;
}
static const char* node_name(uint32_t func) {
    for (size_t i=0;i<g_class_count;++i) if (g_classes[i].func==func) return g_classes[i].name;
    return NULL;
}
static void node_census_report(void) {
    if (!cfg.debug || g_node_census_reported || !g_node_census_n) return;
    g_node_census_reported=1;
    for (int i=0;i<g_node_census_n;++i) {
        const char* name=node_name(g_node_census[i].func);
        LOG("update node: 0x%06x priority %d, %llu calls -- %s", (unsigned)g_node_census[i].func,
            g_node_census[i].prio, g_node_census[i].calls, name ? name : "NOT CLASSIFIED");
    }
}
static int node_mode(uint32_t func) {
    if (!cfg.substep) return MODE_FRAME;
    for (size_t i = 0; i < g_class_count; i++)
        if (g_classes[i].func == func) return g_sub_enabled[i] ? g_classes[i].mode : MODE_FRAME;
    return MODE_FRAME;
}
