/* ------------------------------------------------------------------ the keyboard while the menu is typed in
   See ui/typing_block.h. Every x86 game reads its keyboard with DirectInput and, as a
   fallback, GetKeyboardState; both are answered through the filter. DirectInput is reached
   through the interface the game creates: its CreateDevice, then the keyboard device's
   GetDeviceState, are redirected in their tables. Those tables belong to DirectInput and are
   shared by every device of a kind, so the hook tells the keyboard by the size it asks for
   (256 bytes) and passes a gamepad's or mouse's read straight through. */
#include "../ui/typing_block.h"

static struct typing_block g_tb_vk, g_tb_dik;

static BOOL (WINAPI *orig_GetKeyboardState)(PBYTE);
static BOOL WINAPI hook_GetKeyboardState(PBYTE keys) {
    BOOL r = orig_GetKeyboardState(keys);
    if (r && keys) typing_block_state(&g_tb_vk, keys, 0);
    return r;
}

/* A redirected table slot and what it held. A game makes one DirectInput object and one
   device table, but the A and W interfaces have separate tables, so a few are kept. */
struct vt_hook { void** vtable; void* orig; };
static struct vt_hook g_di_create[4], g_di_state[4];

static void* vt_orig(struct vt_hook* h, int n, void** vtable) {
    for (int i = 0; i < n; ++i) if (h[i].vtable == vtable) return h[i].orig;
    return NULL;
}
static void vt_redirect(struct vt_hook* h, int n, void** vtable, int slot, void* hook) {
    if (!vtable || vt_orig(h, n, vtable) || vtable[slot] == hook) return;
    for (int i = 0; i < n; ++i) {
        if (h[i].vtable) continue;
        DWORD old;
        if (!VirtualProtect(&vtable[slot], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return;
        h[i].vtable = vtable; h[i].orig = vtable[slot];
        vtable[slot] = hook;
        VirtualProtect(&vtable[slot], sizeof(void*), old, &old);
        return;
    }
}

typedef HRESULT (WINAPI *DiGetDeviceStateFn)(void* self, DWORD size, void* data);
static HRESULT WINAPI hook_di_get_device_state(void* self, DWORD size, void* data) {
    DiGetDeviceStateFn orig = (DiGetDeviceStateFn)vt_orig(g_di_state, 4, *(void***)self);
    if (!orig) return E_FAIL;   /* cannot happen: only a table it was installed in reaches here */
    HRESULT r = orig(self, size, data);
    if (SUCCEEDED(r) && size == 256 && data) typing_block_state(&g_tb_dik, (unsigned char*)data, 1);
    return r;
}
typedef HRESULT (WINAPI *DiCreateDeviceFn)(void* self, REFGUID guid, void** device, void* outer);
static HRESULT WINAPI hook_di_create_device(void* self, REFGUID guid, void** device, void* outer) {
    DiCreateDeviceFn orig = (DiCreateDeviceFn)vt_orig(g_di_create, 4, *(void***)self);
    if (!orig) return E_FAIL;
    HRESULT r = orig(self, guid, device, outer);
    if (SUCCEEDED(r) && device && *device)
        vt_redirect(g_di_state, 4, *(void***)*device, 9 /* IDirectInputDevice8::GetDeviceState */, (void*)hook_di_get_device_state);
    return r;
}
/* Called with whatever DirectInput8Create handed the game. */
static void typing_block_wrap_dinput(HRESULT r, void** out) {
    if (SUCCEEDED(r) && out && *out)
        vt_redirect(g_di_create, 4, *(void***)*out, 3 /* IDirectInput8::CreateDevice */, (void*)hook_di_create_device);
}

/* The game's own import, for the launcher's way in, where the game's DirectInput8Create is
   the system's rather than this DLL's export. */
typedef HRESULT (WINAPI *DirectInput8CreateFn)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static DirectInput8CreateFn orig_import_DirectInput8Create;
static HRESULT WINAPI hook_import_DirectInput8Create(HINSTANCE hinst, DWORD ver, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
    HRESULT r = orig_import_DirectInput8Create(hinst, ver, riid, out, outer);
    typing_block_wrap_dinput(r, out);
    return r;
}
