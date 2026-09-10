/* Compiles filter shaders exactly as the runtime does, against whichever d3dx9 is given.
   Used to check that bundled and user shaders fit the target profile before shipping them.
   The prologue and the pass rules come from the runtime's own header, so this cannot drift
   from what the game actually does.
   Usage: shader_check.exe <d3dx9.dll> <profile> <file.hlsl> [more.hlsl ...] */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/core/shader_parse.h"

typedef HRESULT (WINAPI *CompileFn)(LPCSTR,UINT,const void*,void*,LPCSTR,LPCSTR,DWORD,void**,void**,void**);
typedef struct { void** lpVtbl; } Buf;
static void* buf_ptr(void* b){ return ((void*(WINAPI*)(void*))((Buf*)b)->lpVtbl[3])(b); }
static DWORD buf_len(void* b){ return ((DWORD(WINAPI*)(void*))((Buf*)b)->lpVtbl[4])(b); }
static void  buf_free(void* b){ if (b) ((ULONG(WINAPI*)(void*))((Buf*)b)->lpVtbl[2])(b); }

static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    char* d = (n >= 0) ? (char*)malloc((size_t)n + 1) : NULL;
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); fclose(f); return NULL; }
    d[n] = 0; fclose(f);
    return d;
}

int main(int argc, char** argv) {
    if (argc < 4) { puts("usage: shader_check <d3dx9.dll> <profile> <file.hlsl>..."); return 2; }
    HMODULE m = LoadLibraryA(argv[1]);
    CompileFn compile = m ? (CompileFn)GetProcAddress(m, "D3DXCompileShader") : NULL;
    if (!compile) { printf("FAIL: no D3DXCompileShader in %s\n", argv[1]); return 2; }
    const char* profile = argv[2];
    int bad = 0;

    for (int i = 3; i < argc; ++i) {
        char* text = read_file(argv[i]);
        if (!text) { printf("FAIL %s: cannot read\n", argv[i]); bad = 1; continue; }

        struct ShaderPass passes[HFR_MAX_PASSES];
        const char* header; size_t header_len; int total = 0; const char* err = NULL;
        int n = shader_split(text, &header, &header_len, passes, &total, &err);
        if (!n) { printf("FAIL %s: %s\n", argv[i], err); bad = 1; free(text); continue; }

        size_t plen = sizeof SHADER_PROLOGUE - 1;
        int file_bad = 0;
        for (int j = 0; j < n; ++j) {
            size_t len = plen + header_len + passes[j].len;
            char* src = (char*)malloc(len + 1);
            if (!src) { file_bad = 1; break; }
            memcpy(src, SHADER_PROLOGUE, plen);
            memcpy(src + plen, header, header_len);
            memcpy(src + plen + header_len, passes[j].body, passes[j].len);
            src[len] = 0;

            void *sh = NULL, *msg = NULL;
            HRESULT hr = compile(src, (UINT)len, NULL, NULL, "main", profile, 0, &sh, &msg, NULL);
            if (FAILED(hr) || !sh) {
                printf("FAIL %s pass %d (%s): hr=0x%08lx\n%.*s\n", argv[i], j, profile, (long)hr,
                       msg ? (int)buf_len(msg) : 0, msg ? (char*)buf_ptr(msg) : "");
                file_bad = 1;
            } else {
                printf("OK   %s pass %d (%s): %lu bytes of bytecode, scale %d%s\n",
                       argv[i], j, profile, (unsigned long)buf_len(sh), passes[j].scale,
                       passes[j].want_float ? ", float target" : "");
                if (msg && buf_len(msg)) printf("     warnings: %.*s\n", (int)buf_len(msg), (char*)buf_ptr(msg));
            }
            buf_free(sh); buf_free(msg); free(src);
        }
        if (n > 1 && !file_bad) printf("     %s: %d passes, %dx overall\n", argv[i], n, total);
        bad |= file_bad;
        free(text);
    }
    return bad;
}
