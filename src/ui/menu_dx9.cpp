#include "menu_renderer.h"
#include <d3d9.h>
#include "../../third_party/imgui/imgui.h"
#include "../../third_party/imgui/backends/imgui_impl_dx9.h"
bool hfr_menu_renderer_init(void* d) {return ImGui_ImplDX9_Init(static_cast<IDirect3DDevice9*>(d));}
void hfr_menu_renderer_shutdown() {ImGui_ImplDX9_Shutdown();}
void hfr_menu_renderer_invalidate() {ImGui_ImplDX9_InvalidateDeviceObjects();}
bool hfr_menu_renderer_create() {return ImGui_ImplDX9_CreateDeviceObjects();}
void hfr_menu_renderer_new_frame() {ImGui_ImplDX9_NewFrame();}
void hfr_menu_renderer_draw(ImDrawData* d) {ImGui_ImplDX9_RenderDrawData(d);}
