/* D3D8 interface translation only. Device creation, scaling and the menu stay
 * in the existing D3D9 backend. The caller transfers one D3D9 reference. */
#include "../../third_party/d3d8to9/source/d3d8to9.hpp"
#include "../../third_party/d3d8to9/source/d3dx9.hpp"

PFN_D3DXAssembleShader D3DXAssembleShader = nullptr;
PFN_D3DXDisassembleShader D3DXDisassembleShader = nullptr;
PFN_D3DXLoadSurfaceFromSurface D3DXLoadSurfaceFromSurface = nullptr;

extern "C" void* WINAPI hfr_bridge_create8(void* object) {
    auto* d3d = static_cast<IDirect3D9*>(object);
    if (!d3d) return nullptr;
    /* D3DX is only wanted for two corners of the translation: assembling Direct3D 8 shader
       bytecode, which these games do not use, and a surface copy between unlike formats. The
       translation null-checks all three entry points, so a machine without the DirectX
       End-User Runtime still gets the bridge; any D3DX 9 will do. */
    static HMODULE d3dx = nullptr;
    for (int version = 43; version >= 24 && !d3dx; --version) {
        wchar_t name[32]; wsprintfW(name, L"d3dx9_%d.dll", version);
        d3dx = LoadLibraryW(name);
    }
    if (d3dx) {
        D3DXAssembleShader = reinterpret_cast<PFN_D3DXAssembleShader>(GetProcAddress(d3dx, "D3DXAssembleShader"));
        D3DXDisassembleShader = reinterpret_cast<PFN_D3DXDisassembleShader>(GetProcAddress(d3dx, "D3DXDisassembleShader"));
        D3DXLoadSurfaceFromSurface = reinterpret_cast<PFN_D3DXLoadSurfaceFromSurface>(GetProcAddress(d3dx, "D3DXLoadSurfaceFromSurface"));
    }
    return new Direct3D8(d3d);
}
