#pragma once
#include <string.h>
#include "core/file_hash.h"
#include "games/th06nc.c"
/* The same registry is compiled by the 32-bit dispatcher, 64-bit launcher and
   injected backend. Adding an executable does not require three detection lists. */
static const struct FixedGame* const fixed_games[]={&th06nc_game};
#define FIXED_GAME_COUNT (sizeof fixed_games / sizeof *fixed_games)
static const struct FixedGame* fixed_identify_file(const char* path) {
    char hash[65];if (!hfr_file_sha256(path,hash)) return NULL;
    for (size_t i=0;i<FIXED_GAME_COUNT;++i)
        if (!strcmp(hash,fixed_games[i]->sha256)) return fixed_games[i];
    return NULL;
}
