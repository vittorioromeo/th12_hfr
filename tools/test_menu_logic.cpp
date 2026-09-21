/* What the menu offers, with no graphics device and no window.
   `tools/test_menu.c` proves the overlay draws on a real Direct3D 9 device; this proves the
   sections contain what they should, which is a different kind of bug and the one that has
   actually shipped. The runtime side of ui_api.h is stubbed and records which settings each
   section asked for: a control that is drawn reads its own value, so the set of settings
   queried is the set of controls offered. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include "../src/ui/ui_api.h"
#include "../third_party/imgui/imgui.h"
#include "../src/ui/menu_renderer.h"

static int g_value[UI_SETTING_COUNT];
static int g_read[UI_SETTING_COUNT];
static int g_failed;
static void fail(const char* what) { printf("FAIL: %s\n", what); g_failed = 1; }

extern "C" {
int         hfr_ui_get(int id) { if (id>=0 && id<UI_SETTING_COUNT) { ++g_read[id]; return g_value[id]; } return 0; }
void        hfr_ui_set(int id, int v) { if (id>=0 && id<UI_SETTING_COUNT) g_value[id]=v; }
int         hfr_ui_filter_count(void) { return 2; }
const char* hfr_ui_filter_name(int i) { return i==0 ? "nearest" : "bilinear"; }
int         hfr_ui_filter_is_fixed_scale(int) { return 0; }
int         hfr_ui_post_count(void) { return 1; }
const char* hfr_ui_post_name(int) { return "cas"; }
void        hfr_ui_save(void) {}
void        hfr_ui_status(char* b, int n) { snprintf(b,(size_t)n,"status"); }
void        hfr_ui_rate_info(char* b, int n) { snprintf(b,(size_t)n,"rates"); }
void        hfr_ui_scale_info(char* b, int n) { snprintf(b,(size_t)n,"scale"); }
int         hfr_ui_menu_key(void) { return VK_F11; }
int         hfr_ui_simulation_locked(void) { return 0; }
int         hfr_ui_simulation_patched(void) { return 1; }
const char* hfr_ui_present_path(void) { return "test"; }
const char* hfr_ui_dim_special_name(void) { return "UFOs"; }
int hfr_ui_toggle_count(void) {return 0;}
const char* hfr_ui_toggle_label(int i) {(void)i;return "";}
const char* hfr_ui_toggle_tip(int i) {(void)i;return "";}
int hfr_ui_toggle_get(int i) {(void)i;return 0;}
void hfr_ui_toggle_set(int i,int v) {(void)i;(void)v;}
int         hfr_ui_system_count(void) { return 0; }
const char* hfr_ui_system_name(int) { return ""; }
int         hfr_ui_system_get(int) { return 0; }
void        hfr_ui_system_set(int, int) {}
void        hfr_ui_report(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); printf("REPORT: "); vprintf(fmt, ap); printf("\n"); va_end(ap); g_failed = 1;
}
void        hfr_menu_draw_sections_for_test(void);
void        hfr_menu_key_down(int) {}
/* No device is created, so the renderer adapter is never reached; these satisfy the linker. */
}
bool hfr_menu_renderer_init(void*) { return true; }
void hfr_menu_renderer_shutdown() {}
void hfr_menu_renderer_invalidate() {}
bool hfr_menu_renderer_create() { return true; }
void hfr_menu_renderer_new_frame() {}
void hfr_menu_renderer_draw(ImDrawData*) {}

/* One frame with no backend: ImGui only needs a built font atlas and a display size. */
static void frame(void) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 960.0f);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    ImGui::Begin("test");
    hfr_menu_draw_sections_for_test();
    ImGui::End();
    ImGui::EndFrame();
    ImGui::Render();
}
static const struct { int id; const char* name; } DIMS[] = {
    { UI_DIM_BACKGROUND,   "dim background" },
    { UI_DIM_ITEMS,        "fade items" },
    { UI_DIM_EFFECTS,      "fade effects" },
    { UI_DIM_SPECIAL,      "fade special" },
    { UI_DIM_PLAYER_SHOTS, "fade player shots" },
};
static void run(int video_available, const char* label) {
    memset(g_read, 0, sizeof g_read);
    g_value[UI_VIDEO_AVAILABLE] = video_available;
    g_value[UI_DIM_AVAILABLE] = 1;
    g_value[UI_DIM_CLASSES] = (1<<5) - 1;
    g_value[UI_OWN_PRESENT] = 1;
    frame();
    for (auto& d : DIMS)
        if (!g_read[d.id]) { char m[128]; snprintf(m,sizeof m,"%s: the %s control was not offered",label,d.name); fail(m); }
    /* The scaler's own controls should appear only when there is a backend for them. */
    if (video_available && !g_read[UI_SCALING]) fail("with a video backend: no scaling control");
    if (!video_available && g_read[UI_SCALING]) fail("without a video backend: a scaling control was offered anyway");
}
int main(void) {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    unsigned char* pixels; int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    io.Fonts->SetTexID((ImTextureID)(intptr_t)1);
    frame();   /* a first frame so styles and windows exist before anything is asserted */

    run(1, "with a video backend");
    /* The regression: an experimental backend with no scaler still fades, and the menu has
       to say so. A blanket "video is unavailable" early-out hid every dimming control. */
    run(0, "without a video backend");

    /* A game with no dimming rules must still draw the sliders, disabled, rather than
       nothing at all -- the menu says why a control cannot be used, it does not hide it. */
    memset(g_read, 0, sizeof g_read);
    g_value[UI_VIDEO_AVAILABLE] = 0; g_value[UI_DIM_AVAILABLE] = 0; g_value[UI_DIM_CLASSES] = 0;
    frame();
    for (auto& d : DIMS)
        if (!g_read[d.id]) { char m[128]; snprintf(m,sizeof m,"with no dimming rules: the %s control vanished",d.name); fail(m); }

    ImGui::DestroyContext();
    printf(g_failed ? "FAIL: menu contents\n"
                    : "PASS: menu sections offer dimming with and without a video backend, scaler only with one\n");
    return g_failed ? 1 : 0;
}
