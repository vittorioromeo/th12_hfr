/* Read-only executable detection regression. No game process or window is created.
 * In particular, TH08's 21 MiB virtual image must not hit the former 16 MiB cap. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/identity.h"

int main(int argc, char** argv) {
    assert(argc == 2);
    FILE* f = fopen(argv[1], "rb"); assert(f);
    assert(!fseek(f, 0, SEEK_END));
    long length = ftell(f); assert(length > 0 && length <= 16 * 1024 * 1024);
    rewind(f);
    uint8_t* file = malloc((size_t)length); assert(file);
    assert(fread(file, 1, (size_t)length, f) == (size_t)length); fclose(f);
    size_t size = 0;
    uint8_t* image = map_game_file(file, (size_t)length, &size); assert(image);
    const struct GameIdentity* id = identify_image(image, size); assert(id);
    assert(size >= id->image_size);
    for (size_t s = 0; s < id->signature_count; ++s) {
        const struct GameSignature* signature = &id->signatures[s];
        for (size_t i = 0; i < signature->size; ++i) {
            size_t offset = signature->addr - 0x400000 + i;
            image[offset] ^= 1;
            assert(!identify_image(image, size));
            image[offset] ^= 1;
        }
    }
    IMAGE_DOS_HEADER* dos = (void*)file;
    IMAGE_NT_HEADERS32* nt = (void*)(file + dos->e_lfanew);
    DWORD saved = nt->OptionalHeader.SizeOfImage;
    nt->OptionalHeader.SizeOfImage = 64 * 1024 * 1024 + 1;
    assert(!map_game_file(file, (size_t)length, &size));
    nt->OptionalHeader.SizeOfImage = saved;
    printf("PASS: %s, image=%lu bytes, all signature bytes reject mutation, oversized allocation rejected\n",
           id->name, (unsigned long)saved);
    free(image); free(file);
    return 0;
}
