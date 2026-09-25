#pragma once
/* The game speed's hotkeys, shared by both runtimes' key polling (window.c for the x86 games,
   overlay_dx11.cpp for New Classic). Polled only, like the size-cycle key: a press is a level
   that rises while the game has focus. The speed itself goes through hfr_ui_set, so the menu,
   the keys and the on-screen note always agree about it. */
#include "ui_api.h"
#include "menu_key.h"

/* The presets the keys and the menu step through, in percent. */
static const int hfr_speed_presets[] = { 25, 50, 75, 100, 150, 200, 300, 400, 800 };
#define HFR_SPEED_PRESET_COUNT ((int)(sizeof hfr_speed_presets / sizeof hfr_speed_presets[0]))

/* The next preset above (dir > 0) or below (dir < 0) a speed that need not be a preset. */
static int hfr_speed_step(int pct, int dir) {
    if (dir > 0) {
        for (int i = 0; i < HFR_SPEED_PRESET_COUNT; ++i) if (hfr_speed_presets[i] > pct) return hfr_speed_presets[i];
        return hfr_speed_presets[HFR_SPEED_PRESET_COUNT - 1];
    }
    for (int i = HFR_SPEED_PRESET_COUNT - 1; i >= 0; --i) if (hfr_speed_presets[i] < pct) return hfr_speed_presets[i];
    return hfr_speed_presets[0];
}

struct hfr_speed_keys { struct menu_key slower, faster, reset; };

static int hfr_speed_key_down(int vk) {
    return vk > 0 && vk < 256 && (GetAsyncKeyState(vk) & 0x8000) != 0;
}
/* Call once a presentation with whether the game has the keyboard. With the keys switched off
   (F11 -> Timing, [video] speed_keys=0) every press is ignored, so a stray press of a key that
   sits beside the arrows on a compact keyboard does nothing. */
static void hfr_speed_keys_poll(struct hfr_speed_keys* k, int focus) {
    if (!hfr_ui_get(UI_SPEED_KEYS)) focus = 0;   /* also forgets any key held when they were switched off */
    int cur = hfr_ui_get(UI_GAME_SPEED), want = cur;
    if (menu_key_press(&k->slower, hfr_speed_key_down(hfr_ui_get(UI_SPEED_KEY_SLOWER)), focus)) want = hfr_speed_step(cur, -1);
    if (menu_key_press(&k->faster, hfr_speed_key_down(hfr_ui_get(UI_SPEED_KEY_FASTER)), focus)) want = hfr_speed_step(cur, +1);
    if (menu_key_press(&k->reset,  hfr_speed_key_down(hfr_ui_get(UI_SPEED_KEY_RESET)),  focus)) want = 100;
    if (want != cur) hfr_ui_set(UI_GAME_SPEED, want);
}
