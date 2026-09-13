#pragma once
struct ImDrawData;
/* One small graphics adapter is linked with the same menu on each architecture. */
bool hfr_menu_renderer_init(void* device);
void hfr_menu_renderer_shutdown();
void hfr_menu_renderer_invalidate();
bool hfr_menu_renderer_create();
void hfr_menu_renderer_new_frame();
void hfr_menu_renderer_draw(ImDrawData* data);
