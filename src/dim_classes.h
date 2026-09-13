/* The classes of drawing the dimming can fade. Shared by both runtimes and by the launchers,
   which pull in the game profiles for identity alone, so this has its own guarded header
   rather than living in either runtime's common header. The order is the order of the
   UI_DIM_* ids in ui/ui_api.h and of DIM_NAMES, which are the INI key names. */
#ifndef HFR_DIM_CLASSES_H
#define HFR_DIM_CLASSES_H
enum { DIM_BACKGROUND = 0, DIM_ITEMS, DIM_EFFECTS, DIM_SPECIAL, DIM_PLAYER_SHOTS, DIM_COUNT, DIM_NONE = -1 };
/* The INI keys are dim_<name> in [video]; both runtimes must spell them the same. */
static const char* const DIM_NAMES[DIM_COUNT] = { "background", "items", "effects", "special", "player_shots" };
#endif
