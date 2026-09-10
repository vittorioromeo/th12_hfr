/* ------------------------------------------------------------------ in-game menu
 * A Dear ImGui overlay drawn into the real back buffer after the game's image has been
 * scaled into it, so the menu is always at the display's resolution rather than being
 * magnified along with the game.
 *
 * The menu is mouse-driven on purpose. The game reads the keyboard directly rather than
 * through the message queue, so anything typed here would also reach the player; keeping
 * the menu to sliders, combo boxes and check boxes means the game can go on running
 * underneath without its input being intercepted.
 */
#include "ui_api.h"
#include "../../third_party/imgui/imgui.h"
#include "../../third_party/imgui/backends/imgui_impl_dx9.h"
#include "../../third_party/imgui/backends/imgui_impl_win32.h"
#include <cstdio>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
bool g_ready = false;
bool g_visible = false;
bool g_objects = false;
ImGuiContext* g_ctx = nullptr;

void style_for(float height) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    ImGui::StyleColorsDark();
    s.WindowRounding = 4.0f; s.FrameRounding = 3.0f; s.GrabRounding = 3.0f;
    s.WindowBorderSize = 1.0f; s.WindowPadding = ImVec2(10, 10);
    s.Colors[ImGuiCol_WindowBg].w = 0.92f;
    /* one logical size at 720p, scaled up so the menu stays legible on a 4K display */
    float scale = height / 720.0f;
    if (scale < 1.0f) scale = 1.0f;
    if (scale > 3.0f) scale = 3.0f;
    s.ScaleAllSizes(scale);
    ImGui::GetIO().FontGlobalScale = scale;
}
} // namespace

extern "C" int hfr_menu_init(IDirect3DDevice9* dev, HWND hwnd) {
    if (g_ready) return 1;
    if (!dev || !hwnd) return 0;
    IMGUI_CHECKVERSION();
    g_ctx = ImGui::CreateContext();
    if (!g_ctx) return 0;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;                 /* the game's INI is the only settings file */
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;   /* the game owns the cursor */
    if (!ImGui_ImplWin32_Init(hwnd) || !ImGui_ImplDX9_Init(dev)) {
        ImGui::DestroyContext(g_ctx); g_ctx = nullptr;
        return 0;
    }
    style_for(720.0f);
    g_ready = true; g_objects = true;
    return 1;
}
extern "C" void hfr_menu_shutdown(void) {
    if (!g_ready) return;
    ImGui_ImplDX9_Shutdown(); ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(g_ctx); g_ctx = nullptr;
    g_ready = false; g_objects = false;
}
/* Device objects live in the default pool, so they go before a reset and come back after. */
extern "C" void hfr_menu_invalidate(void) {
    if (g_ready && g_objects) { ImGui_ImplDX9_InvalidateDeviceObjects(); g_objects = false; }
}
extern "C" void hfr_menu_toggle(void) { if (g_ready) g_visible = !g_visible; }
extern "C" int  hfr_menu_visible(void) { return g_ready && g_visible; }

extern "C" int hfr_menu_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result) {
    if (!g_ready) return 0;
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        if ((int)wp == hfr_ui_menu_key()) { g_visible = !g_visible; if (result) *result = 0; return 1; }
    }
    if (!g_visible) return 0;
    /* While the menu is up the cursor must be visible even though the game hides it. */
    if (msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT) {
        ::SetCursor(::LoadCursorA(nullptr, IDC_ARROW));
        if (result) *result = TRUE;
        return 1;
    }
    LRESULT handled = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);
    ImGuiIO& io = ImGui::GetIO();
    /* Swallow only what the menu is actually using, so the game keeps its own input. */
    bool mouse = msg == WM_MOUSEMOVE || msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL ||
                 (msg >= WM_LBUTTONDOWN && msg <= WM_MBUTTONDBLCLK) ||
                 msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP || msg == WM_XBUTTONDBLCLK;
    if (handled || (mouse && io.WantCaptureMouse)) { if (result) *result = 0; return 1; }
    return 0;
}

namespace {
void draw_video_section(void) {
    const char* modes[] = { "Stretch to fill", "Fit, keep aspect ratio", "Pixel perfect (whole multiples)" };
    int scaling = hfr_ui_get(UI_SCALING);
    if (ImGui::Combo("Scaling", &scaling, modes, IM_ARRAYSIZE(modes))) hfr_ui_set(UI_SCALING, scaling);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pixel perfect keeps every game pixel the same size, at the cost of\n"
                          "larger black bars when the window is not a whole multiple of 640x480.");

    int count = hfr_ui_filter_count();
    int filter = hfr_ui_get(UI_FILTER);
    if (filter < 0 || filter >= count) filter = 0;
    if (ImGui::BeginCombo("Filter", hfr_ui_filter_name(filter))) {
        for (int i = 0; i < count; ++i) {
            bool sel = i == filter;
            char label[80];
            if (hfr_ui_filter_is_fixed_scale(i)) snprintf(label, sizeof label, "%s (fixed scale)", hfr_ui_filter_name(i));
            else snprintf(label, sizeof label, "%s", hfr_ui_filter_name(i));
            if (ImGui::Selectable(label, sel)) hfr_ui_set(UI_FILTER, i);
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Any .hlsl file in the shaders folder next to the game appears here.\n"
                          "A filter that fails to compile is reported in touhou_hfr.log.");

    bool resizable = hfr_ui_get(UI_RESIZABLE) != 0;
    if (ImGui::Checkbox("Resizable window", &resizable)) hfr_ui_set(UI_RESIZABLE, resizable);
    ImGui::SameLine();
    bool snap = hfr_ui_get(UI_SNAP_ASPECT) != 0;
    if (ImGui::Checkbox("Snap to 4:3 while dragging", &snap)) hfr_ui_set(UI_SNAP_ASPECT, snap);

    bool borderless = hfr_ui_get(UI_FULLSCREEN_MODE) != 0;
    if (ImGui::Checkbox("Borderless fullscreen", &borderless)) hfr_ui_set(UI_FULLSCREEN_MODE, borderless);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("On: the game's fullscreen becomes a borderless window at the desktop\n"
                          "resolution. Off: its original exclusive 640x480 mode switch.");
}
void draw_window(void) {
    ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Touhou HFR", &g_visible, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        char line[256];
        hfr_ui_status(line, sizeof line);
        ImGui::TextUnformatted(line);
        hfr_ui_scale_info(line, sizeof line);
        ImGui::TextDisabled("%s", line);
        ImGui::Separator();
        draw_video_section();
        ImGui::Separator();
        int latency = hfr_ui_get(UI_MAX_FRAME_LATENCY);
        if (ImGui::SliderInt("Frame queue", &latency, 0, 3, latency == 0 ? "driver default" : "%d frame(s)"))
            hfr_ui_set(UI_MAX_FRAME_LATENCY, latency);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How many frames the driver may queue. 1 is the lowest display latency.");
        ImGui::Separator();
        if (ImGui::Button("Save to touhou_hfr.ini")) hfr_ui_save();
        ImGui::SameLine();
        ImGui::TextDisabled("changes apply immediately; saving makes them the default");
    }
    ImGui::End();
}
} // namespace

extern "C" void hfr_menu_render(IDirect3DDevice9* dev, int width, int height) {
    if (!g_ready || !g_visible) return;
    if (!g_objects) { if (!ImGui_ImplDX9_CreateDeviceObjects()) return; g_objects = true; }
    static int last_height = 0;
    if (height != last_height) { last_height = height; style_for((float)height); }
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);   /* the swap chain, not the game */
    ImGui::NewFrame();
    draw_window();
    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    (void)dev;
}
