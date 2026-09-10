/* Shared by the DLL, launcher and harness. Identify code, not filenames. */
#pragma once
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
struct GameSignature { uintptr_t addr; size_t size; uint8_t bytes[32]; };
/* A place another patch is known to take over, and what the stock executable has there.
   Deliberately not part of identification: the game is identified first, from signatures
   that no known patch touches, and only then are these checked -- so a mismatch means
   "something else has patched this game", never "this is the wrong game". Conflating the
   two would report a modified executable to someone whose executable is fine. */
struct ConflictSite { uintptr_t addr; size_t size; uint8_t bytes[8]; const char* what; };
#include "games/th11_signatures.h"
#include "games/th12_signatures.h"
#include "games/th11_conflicts.h"
#include "games/th12_conflicts.h"
struct GameIdentity {
    unsigned id, image_size;
    const char *name, *legacy_ini, *replay_magic;
    const char* executables[2]; /* English first, then Japanese. */
    const struct GameSignature* signatures;
    size_t signature_count;
    const struct ConflictSite* conflicts;    /* may be NULL: no sites known for that game yet */
    size_t conflict_count;
};
static const struct GameIdentity game_identities[] = {
    {11,0xcd000,"TH11 v1.00a","th11_hfr.ini","t11r",{"th11e.exe","th11.exe"},th11_signatures,sizeof th11_signatures/sizeof *th11_signatures,th11_conflicts,sizeof th11_conflicts/sizeof *th11_conflicts},
    {12,0xd9000,"TH12 v1.00b","th12_hfr.ini","t12r",{"th12e.exe","th12.exe"},th12_signatures,sizeof th12_signatures/sizeof *th12_signatures,th12_conflicts,sizeof th12_conflicts/sizeof *th12_conflicts},
};
#define GAME_COUNT (sizeof game_identities / sizeof *game_identities)
static const IMAGE_NT_HEADERS32* image_header(const uint8_t* image, size_t size) {
    if (size < sizeof(IMAGE_DOS_HEADER)) return NULL;
    const IMAGE_DOS_HEADER* dos=(const void*)image;
    if (dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 ||
        (size_t)dos->e_lfanew>size || size-(size_t)dos->e_lfanew<sizeof(IMAGE_NT_HEADERS32)) return NULL;
    const IMAGE_NT_HEADERS32* nt=(const void*)(image+dos->e_lfanew);
    if (nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
        nt->FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER32) ||
        nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.ImageBase!=0x400000) return NULL;
    return nt;
}
static const struct GameIdentity* identify_image(const uint8_t* image, size_t size) {
    const IMAGE_NT_HEADERS32* nt=image_header(image,size);
    if (!nt || nt->OptionalHeader.SizeOfImage>size) return NULL;
    for (size_t g=0;g<GAME_COUNT;++g) {
        const struct GameIdentity* game=&game_identities[g];
        if (nt->OptionalHeader.SizeOfImage!=game->image_size) continue;
        size_t i=0;
        for (;i<game->signature_count;++i) {
            const struct GameSignature* s=&game->signatures[i];
            size_t off=s->addr-0x400000;
            if (off>size || s->size>size-off || memcmp(image+off,s->bytes,s->size)) break;
        }
        if (i==game->signature_count) return game;
    }
    return NULL;
}
/* Which of the game's frame-loop sites no longer holds its stock bytes, or -1 if all do.
   Call only on an image that has already been identified. */
static int conflict_scan(const struct GameIdentity* game, const uint8_t* image, size_t size) {
    if (!game || !game->conflicts) return -1;
    for (size_t i = 0; i < game->conflict_count; ++i) {
        const struct ConflictSite* c = &game->conflicts[i];
        size_t off = c->addr - 0x400000;
        if (off > size || c->size > size - off) continue;
        if (memcmp(image + off, c->bytes, c->size)) return (int)i;
    }
    return -1;
}

/* Map an on-disk PE as inert bytes; never LoadLibrary an executable here. */
static uint8_t* map_game_file(const uint8_t* file, size_t size, size_t* image_size) {
    const IMAGE_NT_HEADERS32* nt=image_header(file,size);
    if (!nt || nt->OptionalHeader.SizeOfImage>16*1024*1024 ||
        nt->OptionalHeader.SizeOfHeaders>size || nt->OptionalHeader.SizeOfHeaders>nt->OptionalHeader.SizeOfImage) return NULL;
    const IMAGE_SECTION_HEADER* s=IMAGE_FIRST_SECTION(nt);
    size_t off=(const uint8_t*)s-file;
    if (off>size || nt->FileHeader.NumberOfSections>(size-off)/sizeof *s) return NULL;
    uint8_t* image=calloc(1,nt->OptionalHeader.SizeOfImage);
    if (!image) return NULL;
    memcpy(image,file,nt->OptionalHeader.SizeOfHeaders);
    for (unsigned i=0;i<nt->FileHeader.NumberOfSections;++i) {
        if (s[i].PointerToRawData>size || s[i].SizeOfRawData>size-s[i].PointerToRawData ||
            s[i].VirtualAddress>nt->OptionalHeader.SizeOfImage || s[i].SizeOfRawData>nt->OptionalHeader.SizeOfImage-s[i].VirtualAddress) {
            free(image); return NULL;
        }
        memcpy(image+s[i].VirtualAddress,file+s[i].PointerToRawData,s[i].SizeOfRawData);
    }
    *image_size=nt->OptionalHeader.SizeOfImage;
    return image;
}
