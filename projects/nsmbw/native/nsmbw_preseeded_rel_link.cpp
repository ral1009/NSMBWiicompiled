// Fix (not a diagnostic) for the black screen traced through nsmbw_create_next_scene_diag.cpp:
// dScene_c::createNextScene() could never create the RESTART_CRSIN scene because
// fProfListMg_c::m_data_p's profileList[] table (d_profileNP.rel) was null past its very first
// entry - profileList[0] (g_profile_BOOT, in main.dol) was intact, but profileList[1..8]
// (AUTO_SELECT/SELECT/WORLD_MAP/WORLD_9_DEMO/STAGE/RESTART_CRSIN/CRSIN/MOVIE, all living in other
// RELs) all read as 0x00000000 live.
//
// Traced the actual mechanism, not guessed:
// - generated_nsmbw/data_sections_init_blobs/d_profileNP.bin (the translator's OWN embedded
//   blob, inspected directly, before any runtime copy) already has the CORRECT non-zero
//   relocated pointers at this exact offset (profileList[6]=0x8098D3D0, landing inside
//   d_basesNP's [0x8076D680, 0x809BD75C) range - RESTART_CRSIN's real class lives in
//   d_basesNP.rel). So the translator's static data-init generation is not the bug.
// - nsmbw_guest_ctor_hook.cpp's RestoreRelImages() correctly copies that same correct blob into
//   guest RAM once, early in boot - also not the bug.
// - The corruption happens later: dSys_c::execute()'s system-level myDylinkInitPhase
//   (source/dol/bases/d_system.cpp) drives dDyl::InitAsync() -> DynamicModuleCallback::
//   InitCallback(), which calls the REAL s_ProfileDMC.link() (source/dol/framework/d_dylink.cpp) -
//   genuine, live guest code, not stubbed out. DynamicModuleControlBase::link() (0x80160080,
//   confirmed via projects/nsmbw/function_map.txt, disassembled directly) unconditionally calls
//   do_load() then do_link() through the object's own vtable whenever mUsageCount==0.
//   DynamicModuleControl::do_load() (source/dol/cLib/c_dylink.cpp) requests
//   "/rels/<name>NP.rel" from disc via mDvd_toMainRam_c::create() and blocks on waitDone(); then
//   do_link() calls the real OSLink()/OSLinkFixed() and finally re-runs the module's _prolog()
//   (which, for d_profileNP, is exactly `fProfListMg_c::m_data_p = &profileList;` -
//   source/d_profileNP/d_profile.cpp:757).
// - The four modules this project pre-seeds (d_bases, d_enemies, d_en_boss, d_profile) exist on
//   the actual disc ONLY as LZ-compressed files (NSMBW-Files/files/rels/*NP.rel.LZ - confirmed by
//   directory listing; nothing else in files/rels/), and this runtime's DVD/archive layer
//   (runtime/src/hle/storage/dvd.cpp) has no ".LZ" resolution or decompression anywhere. So the
//   real do_load()/do_link() path for exactly these four modules cannot succeed the way it does
//   on real hardware, and running it anyway re-touches (and, empirically, zeroes) memory
//   RestoreRelImages() had already set up correctly - which is why only the untouched first word
//   of profileList[] (predating this call) survived.
//
// Fix: these four modules' data is already correct and resident (RestoreRelImages() already did
// the equivalent of load+link for them, ahead of when the guest itself gets a chance to). So for
// exactly these four - identified by their real mModuleName string, read the same way
// DynamicModuleControl::getModuleName() (0x801602C0: `lwz r3,0x20(r3); blr`) does - this override
// skips the real do_load()/do_link() call and reports success directly, replicating link()'s own
// bookkeeping (mUsageCount/mLinkCount, at +0x0/+0x2 per the disassembly below) so later
// link()/unlink() ref-counting for the same object stays correct. Every other caller (nothing else
// in this game uses DynamicModuleControlBase for anything other than these four RELs, per
// dDyl::DynamicNameTable being otherwise empty) falls through to a faithful reimplementation of
// the original logic, dispatched through the object's own vtable exactly as the original does -
// not reimplemented behavior, just relocated.
//
// DynamicModuleControlBase::link() (0x80160080) disassembles to exactly
// source/dol/cLib/c_dylink.cpp:36-51:
//   lhz r0,0(r3); cmpwi r0,0; bne skip_load_link      ; if (mUsageCount == 0)
//   lwz r12,0xc(r3); lwz r12,0x1c(r12); mtctr r12; bctrl   ; do_load() [vtable+0x1C]
//   lwz r12,0xc(r31); lwz r12,0x28(r12); mtctr r12; bctrl  ; do_link() [vtable+0x28]
//   cmpwi r3,0; bne inc_link_count; li r3,0; b end     ; if (!do_link()) return false
//   lhz r3,2(r31); cmplwi r3,0xffff; bge skip_load_link
//   addi r0,r3,1; sth r0,2(r31)                        ; mLinkCount++ (capped at 0xffff)
//   skip_load_link: lhz r3,0(r31); cmplwi r3,0xffff; bge return_true
//   addi r0,r3,1; sth r0,0(r31)                        ; mUsageCount++ (capped at 0xffff)
//   return_true: li r3,1
// confirming mUsageCount@+0x0, mLinkCount@+0x2, and the object's OWN vtable pointer at +0xC (not
// +0x0 - this class's data members precede its vtable pointer in this compiler's layout, verified
// by getModuleName()'s offset +0x20 landing exactly where DynamicModuleControl's declared field
// order puts mModuleName once DynamicModuleControlBase's 0xC bytes of data + 4-byte vtable
// pointer are accounted for).
//
// load_async() gap (found after the .link() fix alone still corrupted d_profileNP's vtables):
// myDylinkInitPhase_0a/0b/0c (source/dol/bases/d_s_boot.cpp) call `s_DBasesDMC.load_async()`
// BEFORE `.link()`, and DynamicModuleControlBase::load_async() (0x80160160, disassembled below)
// tail-calls do_load_async() [vtable+0x20] whenever mUsageCount==0 - a second, independent path
// into real DVD-loading code that the .link()-only fix never touched. DynamicModuleControl::
// do_load_async() (source/dol/cLib/c_dylink.cpp) allocates an mDvd_callback_c/heap buffer for a
// real (and here, unservable - see the .LZ note above) disc load. That allocation goes through
// sDylinkHeap, which has no idea RestoreRelImages() already parked correct data at fixed addresses
// via a raw memcpy outside any heap's bookkeeping - so the allocator can and does hand out
// overlapping memory, stomping the very data this fix exists to protect. Confirmed this is a
// runtime-only issue, not a translator/relocation bug: `trace-rel-relocations` against the exact
// vtable slots that came back corrupted (e.g. d_profileNP's dYesNoWindow_c vtable+0x24) computed
// the correct cross-module target, and generated_nsmbw/data_sections_init_blobs/d_profileNP.bin
// already holds that exact correct value at that exact offset - the bytes are only ever wrong
// after this second load path runs.
//   lhz r0,0(r3); cmpwi r0,0; bne return_true          ; if (mUsageCount == 0)
//   lwz r12,0xc(r3); lwz r12,0x20(r12); mtctr r12; bctr ; tail-call do_load_async() [vtable+0x20]
//   return_true: li r3,1; blr
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"

#include <cstdio>
#include <string>

namespace {
constexpr uint32_t kUsageCountOffset = 0x0u;
constexpr uint32_t kLinkCountOffset = 0x2u;
constexpr uint32_t kModuleNameOffset = 0x20u; // DynamicModuleControl/dDynamicModuleControl only
constexpr uint32_t kVtableOffset = 0xCu;
constexpr uint32_t kDoLoadSlot = 0x1Cu;
constexpr uint32_t kDoLoadAsyncSlot = 0x20u;
constexpr uint32_t kDoLinkSlot = 0x28u;

std::string ReadModuleNameIfAny(uint32_t thisPtr)
{
    uint32_t namePtr = 0;
    try {
        namePtr = Memory::Read32(thisPtr + kModuleNameOffset);
    } catch (const Memory::AccessViolation&) {
        return {};
    }
    if (namePtr == 0) {
        return {};
    }
    std::string name;
    try {
        for (uint32_t p = namePtr; name.size() < 32; ++p) {
            const char c = static_cast<char>(Memory::Read8(p));
            if (c == '\0') break;
            name.push_back(c);
        }
    } catch (const Memory::AccessViolation&) {
        return {};
    }
    return name;
}

bool IsPreSeededModule(const std::string& name)
{
    return name == "d_bases" || name == "d_enemies" || name == "d_en_boss" || name == "d_profile";
}

void BumpU16(uint32_t addr)
{
    const uint16_t value = Memory::Read16(addr);
    if (value < 0xFFFFu) {
        Memory::Write16(addr, static_cast<uint16_t>(value + 1));
    }
}
}

extern "C" uint32_t DynamicModuleControlBaseLink_80160080(uint32_t thisPtr)
{
    const uint16_t usageCount = Memory::Read16(thisPtr + kUsageCountOffset);

    if (usageCount == 0) {
        const std::string name = ReadModuleNameIfAny(thisPtr);
        if (IsPreSeededModule(name)) {
            std::fprintf(stderr,
                "[nsmbw] DynamicModuleControlBase::link(this=0x%08X, name=\"%s\"): pre-seeded by "
                "RestoreRelImages() at boot - skipping do_load()/do_link() (the real path re-requests "
                "an unrelocated .LZ-compressed REL this runtime's DVD layer can't serve) and "
                "reporting success\n",
                thisPtr, name.c_str());
            std::fflush(stderr);

            BumpU16(thisPtr + kLinkCountOffset);
            Memory::Write16(thisPtr + kUsageCountOffset, 1);
            return 1;
        }

        // Not one of ours - faithful reimplementation, dispatched through the real vtable exactly
        // as the original does.
        const uint32_t vtable = Memory::Read32(thisPtr + kVtableOffset);

        auto& cpu = GetPersistentCpuContext();
        cpu.gpr[3] = thisPtr;
        InvokeIndirectCpu(Memory::Read32(vtable + kDoLoadSlot), &cpu); // do_load()

        cpu.gpr[3] = thisPtr;
        InvokeIndirectCpu(Memory::Read32(vtable + kDoLinkSlot), &cpu); // do_link()
        if (cpu.gpr[3] == 0) {
            return 0;
        }

        BumpU16(thisPtr + kLinkCountOffset);
    }

    BumpU16(thisPtr + kUsageCountOffset);
    return 1;
}

PPC_NATIVE_OVERRIDE(80160080, DynamicModuleControlBaseLink_80160080, uint32_t, (uint32_t thisPtr), (thisPtr));

extern "C" uint32_t DynamicModuleControlBaseLoadAsync_80160160(uint32_t thisPtr)
{
    const uint16_t usageCount = Memory::Read16(thisPtr + kUsageCountOffset);
    if (usageCount != 0) {
        return 1;
    }

    const std::string name = ReadModuleNameIfAny(thisPtr);
    if (IsPreSeededModule(name)) {
        std::fprintf(stderr,
            "[nsmbw] DynamicModuleControlBase::load_async(this=0x%08X, name=\"%s\"): pre-seeded - "
            "skipping do_load_async() (same reason as link(), see header) and reporting done\n",
            thisPtr, name.c_str());
        std::fflush(stderr);
        return 1;
    }

    // Not one of ours - faithful reimplementation, dispatched through the real vtable.
    const uint32_t vtable = Memory::Read32(thisPtr + kVtableOffset);
    auto& cpu = GetPersistentCpuContext();
    cpu.gpr[3] = thisPtr;
    InvokeIndirectCpu(Memory::Read32(vtable + kDoLoadAsyncSlot), &cpu); // do_load_async()
    return cpu.gpr[3];
}

PPC_NATIVE_OVERRIDE(80160160, DynamicModuleControlBaseLoadAsync_80160160, uint32_t, (uint32_t thisPtr), (thisPtr));
