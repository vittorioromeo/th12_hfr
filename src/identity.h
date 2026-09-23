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
#include "games/th10_signatures.h"
#include "games/th08_signatures.h"
#include "games/th11_signatures.h"
#include "games/th12_signatures.h"
#include "games/th13_signatures.h"
#include "games/th14_signatures.h"
#include "games/th15_signatures.h"
#include "games/th18_signatures.h"
#include "games/th20_signatures.h"
#include "games/th10_conflicts.h"
#include "games/th11_conflicts.h"
#include "games/th12_conflicts.h"
#include "games/th13_conflicts.h"
#include "games/th14_conflicts.h"
#include "games/th15_conflicts.h"
#include "games/th18_conflicts.h"
#include "games/th20_conflicts.h"
struct GameIdentity {
    unsigned id, image_size;    /* the smallest accepted SizeOfImage; see identify_image */
    const char *name, *legacy_ini, *replay_magic;
    const char* executables[2]; /* English first, then Japanese. */
    const struct GameSignature* signatures;
    size_t signature_count;
    const struct ConflictSite* conflicts;    /* may be NULL: no sites known for that game yet */
    size_t conflict_count;
};
/* Named slots, so a profile says which game it is rather than counting rows. Inserting a game
   at the front of the table used to silently repoint every profile after it at its neighbour's
   identity -- the same trap as a positional initialiser, and just as quiet. */
enum { GI_TH08, GI_TH10, GI_TH11, GI_TH12, GI_TH13, GI_TH14, GI_TH15, GI_TH18, GI_TH20 };
static const struct GameIdentity game_identities[] = {
    [GI_TH08] = {8,0x14dc000,"TH08 v1.00d",NULL,"T8RP",{"th08e.exe","th08.exe"},th08_signatures,sizeof th08_signatures/sizeof *th08_signatures,NULL,0},
    /* TH10 follows the same naming as the later games: th10.exe is the Japanese original and
       th10e.exe the English one (an earlier note here claimed a th10j.exe; that was a local
       rename, not a convention). The replay magic is unused while the simulation is
       undescribed, so it is left at the obvious guess rather than asserted. */
    [GI_TH10] = {10,0x9c000,"TH10 v1.00a","th10_hfr.ini","t10r",{"th10e.exe","th10.exe"},th10_signatures,sizeof th10_signatures/sizeof *th10_signatures,th10_conflicts,sizeof th10_conflicts/sizeof *th10_conflicts},
    [GI_TH11] = {11,0xcd000,"TH11 v1.00a","th11_hfr.ini","t11r",{"th11e.exe","th11.exe"},th11_signatures,sizeof th11_signatures/sizeof *th11_signatures,th11_conflicts,sizeof th11_conflicts/sizeof *th11_conflicts},
    [GI_TH12] = {12,0xd9000,"TH12 v1.00b","th12_hfr.ini","t12r",{"th12e.exe","th12.exe"},th12_signatures,sizeof th12_signatures/sizeof *th12_signatures,th12_conflicts,sizeof th12_conflicts/sizeof *th12_conflicts},
    [GI_TH13] = {13,0xe9000,"TH13 v1.00c",NULL,"t13r",{"th13e.exe","th13.exe"},th13_signatures,sizeof th13_signatures/sizeof *th13_signatures,th13_conflicts,sizeof th13_conflicts/sizeof *th13_conflicts},
    /* TH14's replay magic is not asserted while the simulation is undescribed: nothing reads
       it until replays are extended, and a wrong four bytes there would be a silent one. */
    [GI_TH14] = {14,0x101000,"TH14 v1.00b",NULL,"t13r",{"th14e.exe","th14.exe"},th14_signatures,sizeof th14_signatures/sizeof *th14_signatures,th14_conflicts,th14_conflict_count},
    [GI_TH15] = {15,0x125000,"TH15 v1.00b",NULL,"t15r",{"th15e.exe","th15.exe"},th15_signatures,sizeof th15_signatures/sizeof *th15_signatures,th15_conflicts,th15_conflict_count},
    [GI_TH18] = {18,0x174000,"TH18 v1.00a",NULL,"t18r",{"th18e.exe","th18.exe"},th18_signatures,sizeof th18_signatures/sizeof *th18_signatures,th18_conflicts,th18_conflict_count},
    [GI_TH20] = {20,0x1f9000,"TH20 v1.00c",NULL,"t20r",{"th20e.exe","th20.exe"},th20_signatures,sizeof th20_signatures/sizeof *th20_signatures,th20_conflicts,th20_conflict_count},
};
#define GAME_COUNT (sizeof game_identities / sizeof *game_identities)
/* Where the image actually is. Every address in a profile is written for the preferred base,
   0x400000, which is where TH08-TH15 always load; TH20's executable is marked relocatable and
   Windows moves it. `g_image_base` is the real base of a *loaded* image (the runtime sets it
   before identifying); a file mapped by the launcher or the harness leaves it alone. */
#define HFR_PREFERRED_BASE ((uintptr_t)0x400000)
static uintptr_t g_image_base = HFR_PREFERRED_BASE;
#define HFR_VA(a) ((uintptr_t)(a) + (g_image_base - HFR_PREFERRED_BASE))
static const IMAGE_NT_HEADERS32* image_header(const uint8_t* image, size_t size) {
    if (size < sizeof(IMAGE_DOS_HEADER)) return NULL;
    const IMAGE_DOS_HEADER* dos=(const void*)image;
    if (dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 ||
        (size_t)dos->e_lfanew>size || size-(size_t)dos->e_lfanew<sizeof(IMAGE_NT_HEADERS32)) return NULL;
    const IMAGE_NT_HEADERS32* nt=(const void*)(image+dos->e_lfanew);
    if (nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
        nt->FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER32) ||
        nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        (nt->OptionalHeader.ImageBase!=HFR_PREFERRED_BASE && nt->OptionalHeader.ImageBase!=g_image_base)) return NULL;   /* the loader rewrites it when it moves the image */
    return nt;
}
/* `bytes` are what the file holds at preferred address `addr`. The loader adds the base delta
   to every dword the relocation table names, so add it here to the ones inside the range. A
   fixup that straddles the end of the range cannot be represented; the range is then reported
   as unusable (-1) and whoever froze that signature has to pick a different length. */
static int reloc_adjust(const uint8_t* image, size_t size, uintptr_t addr, size_t n, uint8_t* bytes, uint32_t delta) {
    if (!delta) return 0;
    const IMAGE_NT_HEADERS32* nt=image_header(image,size);
    if (!nt) return -1;
    const IMAGE_DATA_DIRECTORY* dir=&nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (!dir->VirtualAddress || dir->VirtualAddress>size || dir->Size>size-dir->VirtualAddress) return -1;
    uint32_t lo=(uint32_t)(addr-HFR_PREFERRED_BASE), hi=lo+(uint32_t)n, done=0;
    const uint8_t* p=image+dir->VirtualAddress,* end=p+dir->Size;
    while (p+8<=end) {
        uint32_t page,block; memcpy(&page,p,4); memcpy(&block,p+4,4);
        if (block<8 || block>(size_t)(end-p)) break;
        if (page+0x1000>lo && page<hi) {
            for (const uint8_t* e=p+8;e+2<=p+block;e+=2) {
                uint16_t w; memcpy(&w,e,2);
                if ((w>>12)!=IMAGE_REL_BASED_HIGHLOW) continue;
                uint32_t rva=page+(w&0xfff);
                if (rva+4<=lo || rva>=hi) continue;
                if (rva<lo || rva+4>hi) return -1;
                uint32_t v; memcpy(&v,bytes+(rva-lo),4); v+=delta; memcpy(bytes+(rva-lo),&v,4); ++done;
            }
        }
        p+=block;
    }
    return (int)done;
}
static const struct GameIdentity* identify_image(const uint8_t* image, size_t size) {
    const IMAGE_NT_HEADERS32* nt=image_header(image,size);
    if (!nt || nt->OptionalHeader.SizeOfImage>size) return NULL;
    for (size_t g=0;g<GAME_COUNT;++g) {
        const struct GameIdentity* game=&game_identities[g];
        /* Larger than the build these signatures were taken from is still that build. Both
           the English TH13 and every Steam release append a section of their own -- a
           translation loader, a DRM stub -- which grows SizeOfImage without moving or
           altering one byte of the game's code. Smaller is never right; larger is settled by
           the signatures below, which is where identification actually rests. */
        if (nt->OptionalHeader.SizeOfImage<game->image_size) continue;
        size_t i=0;
        for (;i<game->signature_count;++i) {
            const struct GameSignature* s=&game->signatures[i];
            size_t off=s->addr-0x400000;
            if (off>size || s->size>size-off) break;
            uint8_t want[sizeof s->bytes]; memcpy(want,s->bytes,s->size);
            if (reloc_adjust(image,size,s->addr,s->size,want,(uint32_t)(g_image_base-HFR_PREFERRED_BASE))<0) break;
            if (memcmp(image+off,want,s->size)) break;
        }
        if (i==game->signature_count) return game;
    }
    return NULL;
}
/* Whether this image is a game inside a DRM wrapper. A Steam release of these games is the
   same executable with an extra section holding a stub, the entry point moved into it, and
   the game's own .text encrypted: the stub checks ownership at start-up and decrypts the code
   in memory before jumping to where the entry point used to be. Nothing on disk can identify
   such a file -- every frozen signature reads as ciphertext -- and nothing should pretend to;
   what this answers is the different question of whether that is *why* identification failed,
   so the launcher can say so and the runtime can arrange to look again later.

   Steam names its section `.bind`. A stub under some other name is still caught, because the
   entry point of a wrapped image is by construction outside the game's own code section. */
static int wrapped_executable(const uint8_t* image, size_t size) {
    const IMAGE_NT_HEADERS32* nt=image_header(image,size);
    if (!nt) return 0;
    size_t headers=(size_t)((const uint8_t*)IMAGE_FIRST_SECTION(nt)-image);
    unsigned count=nt->FileHeader.NumberOfSections;
    if (count>96 || headers>size || (size-headers)/sizeof(IMAGE_SECTION_HEADER)<count) return 0;
    const IMAGE_SECTION_HEADER* s=IMAGE_FIRST_SECTION(nt);
    uint32_t entry=nt->OptionalHeader.AddressOfEntryPoint;
    for (unsigned i=0;i<count;++i) {
        if (!memcmp(s[i].Name,".bind",6)) return 1;
        if (entry>=s[i].VirtualAddress && entry-s[i].VirtualAddress<s[i].Misc.VirtualSize &&
            memcmp(s[i].Name,".text",6)) return 1;
    }
    return 0;
}
/* Which of the game's frame-loop sites no longer holds its stock bytes, or -1 if all do.
   Call only on an image that has already been identified. */
static int conflict_scan(const struct GameIdentity* game, const uint8_t* image, size_t size) {
    if (!game || !game->conflicts) return -1;
    for (size_t i = 0; i < game->conflict_count; ++i) {
        const struct ConflictSite* c = &game->conflicts[i];
        size_t off = c->addr - g_image_base;   /* a relocated identity carries relocated addresses */
        if (off > size || c->size > size - off) continue;
        if (memcmp(image + off, c->bytes, c->size)) return (int)i;
    }
    return -1;
}

/* Map an on-disk PE as inert bytes; never LoadLibrary an executable here. */
static uint8_t* map_game_file(const uint8_t* file, size_t size, size_t* image_size) {
    const IMAGE_NT_HEADERS32* nt=image_header(file,size);
    /* Early games keep large object pools in BSS (TH08: about 21 MiB). */
    if (!nt || nt->OptionalHeader.SizeOfImage>64*1024*1024 ||
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
