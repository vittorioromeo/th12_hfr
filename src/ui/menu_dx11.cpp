#include "menu_renderer.h"
#include <d3d11.h>
#include "../../third_party/imgui/imgui.h"
#include "../../third_party/imgui/backends/imgui_impl_dx11.h"
bool hfr_menu_renderer_init(void* p) {
    auto* d=static_cast<ID3D11Device*>(p);
    ID3D11DeviceContext* context=nullptr;d->GetImmediateContext(&context);
    bool ok=ImGui_ImplDX11_Init(d,context);context->Release();return ok;
}
void hfr_menu_renderer_shutdown() {ImGui_ImplDX11_Shutdown();}
void hfr_menu_renderer_invalidate() {ImGui_ImplDX11_InvalidateDeviceObjects();}
bool hfr_menu_renderer_create() {return ImGui_ImplDX11_CreateDeviceObjects();}
void hfr_menu_renderer_new_frame() {ImGui_ImplDX11_NewFrame();}
void hfr_menu_renderer_draw(ImDrawData* d) {ImGui_ImplDX11_RenderDrawData(d);}
