/* Compiles a filter shader exactly as the runtime does, against whichever d3dx9 is given.
   Used to check that bundled and user shaders fit the target profile before shipping them.
   Usage: shader_check.exe <d3dx9.dll> <profile> <file.hlsl> [more.hlsl ...] */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
typedef HRESULT (WINAPI *CompileFn)(LPCSTR,UINT,const void*,void*,LPCSTR,LPCSTR,DWORD,void**,void**,void**);
typedef struct { void** lpVtbl; } Buf;
static void* buf_ptr(void* b){ return ((void*(WINAPI*)(void*))((Buf*)b)->lpVtbl[3])(b); }
static DWORD buf_len(void* b){ return ((DWORD(WINAPI*)(void*))((Buf*)b)->lpVtbl[4])(b); }
/* The shader prologue the runtime prepends: the contract every filter is written against. */
static const char* PROLOGUE =
"sampler2D Source : register(s0);\n"
"float4 SourceSize : register(c0);\n"   /* w, h, 1/w, 1/h of the game's image   */
"float4 TargetSize : register(c1);\n"   /* w, h, 1/w, 1/h of what we render into */
"#define SourceSampler Source\n";
int main(int argc, char** argv) {
    if (argc < 4) { puts("usage: shader_check <d3dx9.dll> <profile> <file.hlsl>..."); return 2; }
    HMODULE m = LoadLibraryA(argv[1]);
    CompileFn compile = m ? (CompileFn)GetProcAddress(m, "D3DXCompileShader") : NULL;
    if (!compile) { printf("FAIL: no D3DXCompileShader in %s\n", argv[1]); return 2; }
    int bad = 0;
    for (int i = 3; i < argc; ++i) {
        FILE* f = fopen(argv[i], "rb");
        if (!f) { printf("FAIL %s: cannot open\n", argv[i]); bad = 1; continue; }
        fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
        size_t plen = strlen(PROLOGUE);
        char* src = malloc(plen + n + 1);
        memcpy(src, PROLOGUE, plen);
        if (fread(src + plen, 1, n, f) != (size_t)n) { printf("FAIL %s: short read\n", argv[i]); bad = 1; fclose(f); free(src); continue; }
        fclose(f);
        src[plen + n] = 0;
        void *sh = NULL, *err = NULL;
        HRESULT hr = compile(src, (UINT)(plen + n), NULL, NULL, "main", argv[2], 0, &sh, &err, NULL);
        if (FAILED(hr) || !sh) {
            printf("FAIL %s (%s): hr=0x%08lx\n%.*s\n", argv[i], argv[2], (long)hr,
                   err ? (int)buf_len(err) : 0, err ? (char*)buf_ptr(err) : "");
            bad = 1;
        } else {
            /* instruction slots: bytecode words minus the version and end tokens, roughly */
            printf("OK   %s (%s): %lu bytes of bytecode\n", argv[i], argv[2], (unsigned long)buf_len(sh));
            if (err && buf_len(err)) printf("     warnings: %.*s\n", (int)buf_len(err), (char*)buf_ptr(err));
        }
        free(src);
    }
    return bad;
}
