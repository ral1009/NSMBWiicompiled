// Restores the four REL images and runs main.dol's static constructors plus each REL's
// _prolog(), at the one point in NSMBW's startup where both survive.
//
// Two separate ordering problems, both fixed here:
//
// 1. Constructors used to run before the entry point. __start (0x80004050) calls __init_data
//    (0x80004250), which memsets guest BSS at 0x80004600, so every BSS word a constructor or
//    _prolog had written was zeroed before main ran. Established with a watchpoint on
//    fProfListMg_c::m_data_p (guest 0x8042A698): d_profileNP's _prolog wrote 0x8076A828, then
//    func_80004600 zeroed it, and fBase_make virtual-called through the null list.
//
// 2. The REL images themselves are wiped by OSInit. InitializeDataSections() copies each
//    relocated REL blob into guest RAM before the entry point, but those addresses
//    (0x807685A0 and up) lie inside MEM1's arena, and OSInit (0x801AA940) clears it. Measured
//    directly at profileList (guest 0x8076A828, d_profileNP + 0x2288): it holds the correct
//    relocated pointer 0x804296E8 at __start, still holds it when __init_data returns, and
//    reads 0 the moment OSInit returns. That left fProfListMg_c::m_data_p pointing at an array
//    of null profile pointers, so dScene_c::createNextScene could never build the BOOT scene
//    and the game looped forever drawing nothing.
//
//    On real hardware this cannot happen: the RELs are loaded from disc into heap the game has
//    already allocated, long after OSInit. This runtime pre-loads them at fixed addresses, so
//    the images have to be re-applied once OSInit is done with the arena.
//
// 0x80004040 is the hook for both. __start calls it exactly once, at 0x80004198 - after
// __init_data has cleared BSS AND after the OS bring-up calls at 0x8000416C/0x80004170, and
// immediately before __start calls main at 0x800041B4. It is also the only caller (checked
// against every bl in the DOL), and the function itself is a two-instruction leaf,
// `lbz r3, -0x4F68(r13); blr`, that calls nothing - so reproducing it here is exact and, unlike
// hooking 0x801B8AB0, it does not orphan a callee that the translator would then prune.
//
// Nothing the guest does is skipped or faked; only the ordering is corrected to match the real
// console, where __init_data and OSInit both run before any module is linked.
#include "hle_stubs.h"
#include "memory.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

#include <cstring>

extern "C" void Nsmbw_RunGuestConstructorsOnce(CpuContext* ctx);

// The relocated REL blobs InitializeDataSections() embeds; addresses and sizes match
// generated_nsmbw/data_sections_init.cpp.
extern "C" const uint8_t kData_d_basesNP[];
extern "C" const uint8_t kData_d_en_bossNP[];
extern "C" const uint8_t kData_d_enemiesNP[];
extern "C" const uint8_t kData_d_profileNP[];

namespace {

struct RelImage {
    uint32_t address;
    size_t size;
    const uint8_t* data;
};

void RestoreRelImages()
{
    static const RelImage kImages[] = {
        {0x8076D680u, 2313500u, kData_d_basesNP},
        {0x80B1C920u, 463140u, kData_d_en_bossNP},
        {0x809A2CA0u, 1546520u, kData_d_enemiesNP},
        {0x807684C0u, 14992u, kData_d_profileNP},
    };
    for (const RelImage& image : kImages) {
        if (!Memory::Contains(image.address, image.size)) {
            continue;
        }
        if (void* dest = Memory::GetPointer(image.address, image.size)) {
            std::memcpy(dest, image.data, image.size);
        }
    }
}

} // namespace

extern "C" void NsmbwGuestCtorHook_80004040(CpuContext* ctx)
{
    // Guest constructors run arbitrary code; keep the two bases __start still depends on.
    const uint32_t savedSda = ctx->gpr[13];
    const uint32_t savedSda2 = ctx->gpr[2];

    static bool restored = false;
    if (!restored) {
        restored = true;
        RestoreRelImages();
    }
    Nsmbw_RunGuestConstructorsOnce(ctx);

    ctx->gpr[13] = savedSda;
    ctx->gpr[2] = savedSda2;

    ctx->gpr[3] = Memory::Read8(ctx->gpr[13] - 0x4F68u);
}

PPC_NATIVE_OVERRIDE_VOID(80004040, NsmbwGuestCtorHook_80004040, (CpuContext* ctx), (ctx));
