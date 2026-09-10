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

/* Set when ImGui reports misuse; the menu stops drawing rather than the game stopping. */
static bool g_menu_failed = false;
extern "C" void hfr_imgui_failed(const char* expr, const char* file, int line) {
    if (g_menu_failed) return;
    g_menu_failed = true;
    const char* base = file;
    for (const char* p = file; *p; ++p) if (*p == '/' || *p == '\\') base = p + 1;
    hfr_ui_report("menu: disabled after an internal check failed (%s at %s:%d)", expr, base, line);
}

namespace {
bool g_ready = false;
bool g_visible = false;
/* Set whenever the menu is opened, and whenever the surface it is drawn on changes size.
   ImGui's own "place this window when it appears" condition cannot be used for either: while
   the menu is hidden this file does not call NewFrame at all, so ImGui's frame counter is
   frozen and the window never counts as newly appearing when it comes back. That is what left
   the menu holding a position from a viewport that no longer existed -- open at 1817x1156,
   game switches resolution to 640x480, and the window is now off the edge of the screen with
   no way to bring it back, because reopening did not move it either. */
bool g_place_next = true;
ImVec2 g_last_display(0, 0);
bool g_objects = false;
ImGuiContext* g_ctx = nullptr;
int  g_hint_frames = 0;      /* a short "press this for settings" note after startup */

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
    g_hint_frames = 600;     /* the features are invisible until someone opens the menu */
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
extern "C" void hfr_menu_toggle(void) {
    if (!g_ready) return;
    g_visible = !g_visible;
    g_hint_frames = 0;
    if (g_visible) g_place_next = true;      /* opening always puts it somewhere visible */
}
extern "C" int  hfr_menu_visible(void) { return g_ready && g_visible; }

extern "C" int hfr_menu_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result) {
    if (!g_ready) return 0;
    /* Report whether the key is *down*, never that it was pressed: the runtime also watches
       the key directly, and two sources reporting the same press as two events is what used
       to toggle the menu twice and leave it looking dead. Two sources reporting a level
       cannot disagree about how many presses happened. Swallow the key either way. */
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYUP) {
        if ((int)wp == hfr_ui_menu_key()) {
            hfr_menu_key_down(msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
            if (result) *result = 0;
            return 1;
        }
    }
    /* A key held as the window loses focus never gets its KEYUP here. */
    if (msg == WM_KILLFOCUS) hfr_menu_key_down(0);
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
void help(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
}
bool toggle(const char* label, int id) {
    bool v = hfr_ui_get(id) != 0;
    if (ImGui::Checkbox(label, &v)) { hfr_ui_set(id, v); return true; }
    return false;
}

void draw_hint(void) {
    ImGuiIO& io = ImGui::GetIO();
    float a = g_hint_frames > 90 ? 1.0f : (float)g_hint_frames / 90.0f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - 24.0f * io.FontGlobalScale),
                            ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.65f * a);
    if (ImGui::Begin("##hfr_hint", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs)) {
        int key = hfr_ui_menu_key();
        const char* name = key == VK_F11 ? "F11" : (key == VK_INSERT ? "Insert" : "the menu key");
        ImGui::TextColored(ImVec4(1, 1, 1, a), "Press %s for scaling and filter settings", name);
    }
    ImGui::End();
}

void draw_display_section(void) {
    /* A d3d9 wrapper such as PivotDX9 presents the game itself, so it -- not this patch --
       decides how the image reaches the window. The filters still work, because they run
       before that point, but placing the image inside the window no longer does. Say so and
       disable the two controls rather than leaving them to be adjusted with no effect. */
    bool own = hfr_ui_get(UI_OWN_PRESENT) != 0;
    if (!own) {
        ImGui::TextDisabled("A d3d9 wrapper is presenting this game, so it decides how the");
        ImGui::TextDisabled("image fills the window. Scaling and borderless fullscreen below");
        ImGui::TextDisabled("cannot take effect; filters still can. Rename d3d9.dll in the");
        ImGui::TextDisabled("game's folder to use them -- this patch replaces what it does.");
        ImGui::Separator();
    }
    const char* modes[] = { "Stretch to fill", "Fit, keep aspect ratio", "Pixel perfect (whole multiples)" };
    int scaling = hfr_ui_get(UI_SCALING);
    if (scaling < 0 || scaling > 2) scaling = 1;
    ImGui::BeginDisabled(!own);
    if (ImGui::Combo("Scaling", &scaling, modes, IM_ARRAYSIZE(modes))) hfr_ui_set(UI_SCALING, scaling);
    ImGui::EndDisabled();
    help("Pixel perfect keeps every game pixel the same size, at the cost of\n"
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
    help("Any .hlsl file in the shaders folder next to the game appears here.\n"
         "A filter that fails to compile is reported in touhou_hfr.log.");

    toggle("Resizable window", UI_RESIZABLE);
    ImGui::SameLine();
    toggle("Snap to 4:3 while dragging", UI_SNAP_ASPECT);
    ImGui::BeginDisabled(!own);
    toggle("Borderless fullscreen", UI_FULLSCREEN_MODE);
    ImGui::EndDisabled();
    help("On: the game's fullscreen becomes a borderless window covering the\n"
         "monitor at its own resolution, instead of a 640x480 mode change.");
}

void draw_timing_section(void) {
    bool locked = hfr_ui_simulation_locked() != 0;
    if (locked) {
        ImGui::TextDisabled("Locked while a stage is running: these change the simulation,");
        ImGui::TextDisabled("and the replay being recorded describes the settings it started with.");
    }
    ImGui::BeginDisabled(locked);

    static const int rates[] = { 0, 60, 120, 144, 165, 240, 360, 480 };
    static const char* rate_names[] = { "Auto (display rate)", "60", "120", "144", "165", "240", "360", "480" };
    int fps = hfr_ui_get(UI_FPS), index = -1;
    for (int i = 0; i < IM_ARRAYSIZE(rates); ++i) if (rates[i] == fps) index = i;
    char current[32];
    if (index < 0) snprintf(current, sizeof current, "%d (from the INI)", fps);
    if (ImGui::BeginCombo("Tick rate", index < 0 ? current : rate_names[index])) {
        for (int i = 0; i < IM_ARRAYSIZE(rates); ++i)
            if (ImGui::Selectable(rate_names[i], i == index)) hfr_ui_set(UI_FPS, rates[i]);
        ImGui::EndCombo();
    }
    help("How often the game's logic runs. Auto follows the display, which is\n"
         "what you want unless you are comparing against stock 60 Hz.");

    toggle("Sub-step gameplay", UI_SUBSTEP);
    help("Off: stock 60 Hz logic, presented at the display rate. On: bullets,\n"
         "the player, lasers, items and sprites advance a fraction of a frame\n"
         "at a time, which is what makes motion smooth at high refresh rates.");
    toggle("Sub-tick input", UI_SUBTICK_INPUT);
    help("Poll movement and focus every tick instead of once per frame.\n"
         "Recorded into replays and reproduced on playback.");
    toggle("Interpolate enemy sprites", UI_ENEMY_INTERP);
    help("Enemy scripts run at 60 Hz; this draws their sprites between those\n"
         "positions so they move as smoothly as everything else.");

    if (ImGui::TreeNode("Sub-stepped subsystems")) {
        ImGui::TextDisabled("For narrowing down a problem; the defaults are what has been tested.");
        int n = hfr_ui_system_count();
        for (int i = 0; i < n; ++i) {
            bool v = hfr_ui_system_get(i) != 0;
            ImGui::PushID(i);
            if (ImGui::Checkbox(hfr_ui_system_name(i), &v)) hfr_ui_system_set(i, v);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    ImGui::EndDisabled();
}

void draw_presentation_section(void) {
    bool vsync = hfr_ui_get(UI_VSYNC) != 0;
    if (ImGui::Checkbox("Vertical sync", &vsync)) hfr_ui_set(UI_VSYNC, vsync);
    help("Rebuilds the presentation chain, which takes effect on the next frame.");

    int latency = hfr_ui_get(UI_MAX_FRAME_LATENCY);
    if (ImGui::SliderInt("Frame queue", &latency, 0, 3, "%d")) hfr_ui_set(UI_MAX_FRAME_LATENCY, latency);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", latency == 0 ? "(driver default)" : "frame(s)");
    help("How many frames the driver may queue before it makes the game wait.\n"
         "1 is the lowest display latency. Needs Direct3D 9Ex.");

    ImGui::Spacing();
    ImGui::TextDisabled("Direct3D 9Ex: %s", hfr_ui_get(UI_D3D9EX) ? "in use" : "not in use");
    ImGui::TextDisabled("Presenting through %s", hfr_ui_present_path());
    ImGui::TextDisabled("Both are decided when the device is created; change them in the INI.");
}

void draw_diagnostics_section(void) {
    toggle("Verbose log", UI_DEBUG);
    help("Writes periodic engine state into touhou_hfr.log. For bug reports.");
    int key = hfr_ui_menu_key();
    ImGui::TextDisabled("This menu opens with virtual key 0x%02x%s", key, key == VK_F11 ? " (F11)" : "");
    ImGui::TextDisabled("Change it with video.menu_key in touhou_hfr.ini.");
}

void draw_window(void) {
    ImGuiIO& io = ImGui::GetIO();
    float k = io.FontGlobalScale;
    bool display_changed = io.DisplaySize.x != g_last_display.x || io.DisplaySize.y != g_last_display.y;
    g_last_display = io.DisplaySize;
    /* A fixed, resizable window rather than one that fits its contents: with a tab bar,
       auto-fitting would change the window's size every time you changed tab. Opening it
       always centres it, so it is always somewhere visible, and it stays draggable after. */
    if (g_place_next) {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(620 * k, 420 * k), ImGuiCond_FirstUseEver);
        g_place_next = false;
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(400 * k, 240 * k), io.DisplaySize);
    /* No close button: the menu key is the only way in and out, so the menu can never be
       left in a state where that key looks like it has stopped working. */
    if (ImGui::Begin("Touhou HFR", nullptr, ImGuiWindowFlags_NoCollapse)) {
        /* ImGui's own clamping only keeps a window's title bar on screen, which still leaves
           almost all of it outside a viewport that has just shrunk a long way. Put the whole
           window back inside. */
        if (display_changed) {
            ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize(), want = pos;
            if (want.x + size.x > io.DisplaySize.x) want.x = io.DisplaySize.x - size.x;
            if (want.y + size.y > io.DisplaySize.y) want.y = io.DisplaySize.y - size.y;
            if (want.x < 0.0f) want.x = 0.0f;
            if (want.y < 0.0f) want.y = 0.0f;
            if (want.x != pos.x || want.y != pos.y) ImGui::SetWindowPos(want);
        }
        char line[256];
        hfr_ui_status(line, sizeof line);
        ImGui::TextUnformatted(line);
        hfr_ui_scale_info(line, sizeof line);
        ImGui::TextDisabled("%s", line);

        /* The tabs scroll inside the window; the save row stays pinned to the bottom. */
        float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        ImGui::BeginChild("##hfr_body", ImVec2(0, -footer));
        ImGui::PushItemWidth(240.0f * k);   /* leave the labels room instead of filling the row */
        if (ImGui::BeginTabBar("##hfr_tabs")) {
            if (ImGui::BeginTabItem("Display"))      { ImGui::Spacing(); draw_display_section();      ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Timing"))       { ImGui::Spacing(); draw_timing_section();       ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Presentation")) { ImGui::Spacing(); draw_presentation_section(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Diagnostics"))  { ImGui::Spacing(); draw_diagnostics_section();  ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        ImGui::PopItemWidth();
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button("Save to touhou_hfr.ini")) hfr_ui_save();
        ImGui::SameLine();
        ImGui::TextDisabled("changes apply now; saving keeps them");
    }
    ImGui::End();
}
} // namespace

extern "C" void hfr_menu_render(IDirect3DDevice9* dev, int width, int height) {
    if (!g_ready || g_menu_failed || (!g_visible && g_hint_frames <= 0)) return;
    if (!g_objects) {
        if (!ImGui_ImplDX9_CreateDeviceObjects()) {
            static bool told = false;
            if (!told) { told = true; hfr_ui_report("menu: the overlay's device objects could not be built; it cannot draw"); }
            return;
        }
        g_objects = true;
    }
    static int last_height = 0;
    if (height != last_height) { last_height = height; style_for((float)height); }
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);   /* the swap chain, not the game */
    ImGui::NewFrame();
    if (g_visible) draw_window();
    else if (g_hint_frames > 0) { draw_hint(); --g_hint_frames; }
    ImGui::EndFrame();
    ImGui::Render();
    if (!g_menu_failed) ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    (void)dev;
}
