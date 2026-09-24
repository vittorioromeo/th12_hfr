#include <d3d11.h>
#include <dxgi.h>
#include "ui_api.h"
#include "menu_key.h"
#include "speed_keys.h"
static WNDPROC original_wndproc;
static HWND window;
static ID3D11Device* device;
static struct menu_key key_state;
static struct hfr_speed_keys speed_keys;
static int toggle_requested;
extern "C" void hfr_menu_requested(void) {toggle_requested=1;}
extern "C" void hfr_menu_key_down(int down) {
    if (down) key_state.msg_tapped=1;
    key_state.msg_down=!!down;
}
static LRESULT CALLBACK window_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    LRESULT result;
    if (hfr_menu_wndproc(hwnd,msg,wp,lp,&result)) return result;
    return CallWindowProcA(original_wndproc,hwnd,msg,wp,lp);
}
/* No backbuffer/view is retained across Present: the game remains free to resize
   its swap chain. ImGui saves pipeline state; we additionally preserve all OM targets. */
extern "C" void hfr_d3d11_overlay(void* object) {
    auto* swap=static_cast<IDXGISwapChain*>(object);
    DXGI_SWAP_CHAIN_DESC desc;
    ID3D11Device* current=nullptr;
    if (FAILED(swap->GetDesc(&desc)) || FAILED(swap->GetDevice(__uuidof(ID3D11Device),reinterpret_cast<void**>(&current)))) return;
    if (current!=device || desc.OutputWindow!=window) {
        hfr_menu_shutdown();
        if (window && original_wndproc && reinterpret_cast<WNDPROC>(GetWindowLongPtrA(window,GWLP_WNDPROC))==window_proc)
            SetWindowLongPtrA(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(original_wndproc));
        if (device) device->Release();
        device=current;device->AddRef();window=desc.OutputWindow;
        original_wndproc=nullptr;
        if (hfr_menu_init(device,window)) {
            SetLastError(0);
            original_wndproc=reinterpret_cast<WNDPROC>(SetWindowLongPtrA(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(window_proc)));
            if (!original_wndproc) {hfr_ui_report("D3D11 menu: window hook failed");hfr_menu_shutdown();}
            else hfr_ui_report("D3D11 menu initialized (%ux%u)",desc.BufferDesc.Width,desc.BufferDesc.Height);
        }
    }
    current->Release();
    int key=hfr_ui_menu_key();
    if (!key || !original_wndproc) return;
    DWORD foreground_pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground_pid);
    if (menu_key_press(&key_state,(GetAsyncKeyState(key)&0x8000)!=0,foreground_pid==GetCurrentProcessId())) toggle_requested=1;
    hfr_speed_keys_poll(&speed_keys,foreground_pid==GetCurrentProcessId());
    if (toggle_requested) {
        toggle_requested=0;hfr_menu_toggle();hfr_ui_report("menu: %s",hfr_menu_visible()?"opened":"closed");
    }
    ID3D11Texture2D* buffer=nullptr;ID3D11RenderTargetView* target=nullptr;
    if (FAILED(swap->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&buffer)))) return;
    D3D11_TEXTURE2D_DESC size;buffer->GetDesc(&size);
    HRESULT hr=device->CreateRenderTargetView(buffer,nullptr,&target);buffer->Release();
    if (FAILED(hr)) return;
    ID3D11DeviceContext* context=nullptr;device->GetImmediateContext(&context);
    ID3D11RenderTargetView* saved[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]={};
    ID3D11DepthStencilView* depth=nullptr;
    context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,saved,&depth);
    context->OMSetRenderTargets(1,&target,nullptr);
    hfr_menu_render(device,static_cast<int>(size.Width),static_cast<int>(size.Height));
    context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,saved,depth);
    for (auto* view:saved) if (view) view->Release();
    if (depth) depth->Release();
    context->Release();target->Release();
}
