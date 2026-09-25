#pragma once
/* The game's keyboard while a text field in the menu has it.

   The games read the keyboard's state directly (DirectInput, GetKeyboardState,
   GetAsyncKeyState), never the window's messages, so what is typed into the menu -- a game
   speed after Ctrl+click, say -- would reach the game as well: Enter confirming a title-screen
   choice or a pause-menu item, Escape pausing, the arrows moving. The runtimes answer the
   game's reads through the filter below, which reports every key as up while the field is
   being typed in, and each key that was down then as up until it has been let go (so the
   Enter that confirms the value is not seen as a press on the next frame). Everything else,
   the gamepad included, is untouched. */
#include <string.h>
#include "ui_api.h"

/* One table per key numbering: virtual keys, and DirectInput's scan codes. */
struct typing_block { unsigned char held_back[256]; };

/* Whether the game should see key `index` as down, given that it is. Call for every key the
   game reads, down or not: a key seen up is what ends its holding back. */
static int typing_block_key(struct typing_block* t, int typing, int index, int down) {
    unsigned char* h = &t->held_back[index & 255];
    if (typing) { *h = 1; return 0; }
    if (!*h) return down;
    if (!down) *h = 0;
    return 0;
}
/* Whole-state form for a 256-byte table whose top bit is "down" (GetKeyboardState, a
   DirectInput keyboard). `clear_all` zeroes a held-back byte (DirectInput); otherwise only its
   top bit goes (GetKeyboardState keeps the toggle bit in the low one). */
static void typing_block_state(struct typing_block* t, unsigned char* keys, int clear_all) {
    int typing = hfr_menu_typing();
    for (int i = 0; i < 256; ++i)
        if (!typing_block_key(t, typing, i, (keys[i] & 0x80) != 0))
            keys[i] = clear_all ? 0 : (unsigned char)(keys[i] & 0x7f);
}
