// Diagnostic (not a fix): hunting down the LASTACTOR child every dScene_c auto-creates
// (NSMBW-Decomp source/dol/bases/d_scene.cpp:57, dScene_c::postCreate ->
// fBase_c::createChild(fProf::LASTACTOR, this, 0, fBase_c::OTHER)) - the previous diagnostics
// (nsmbw_gamesetup_createpack_diag.cpp / nsmbw_gamesetup_executepack_diag.cpp) fully cleared
// GAME_SETUP's own create()/execute()/postExecute() of ever calling dScene_c::setNextScene(), so
// whatever decides "GAME_SETUP is done, advance" almost certainly lives in this LASTACTOR child
// instead - a separate, also-undecompiled object we have never located.
//
// LASTACTOR's numeric profile id (749 = 0x2ED) was computed by counting
// include/game/framework/f_profile_name.hpp's PROFILE_NAME_e entries from BOOT=0 - not written in
// the header directly. Consistency check: 749 is exactly one less than 0x2EE
// (dScene_c::m_nextScene's "INVALID" sentinel, confirmed via disassembly in
// nsmbw_create_next_scene_diag.cpp), i.e. LASTACTOR is the very last real profile before the
// sentinel - as expected for the framework's "run last" catch-all actor.
//
// Every child/root construction funnels through fBase_c::fBase_make() (0x80162BB0, confirmed real
// address, fully decompiled in source/dol/framework/f_base.cpp):
//   if ((*fProfListMg_c::m_data_p)[profName].mBaseProfile == nullptr) return nullptr;
//   setTmpCtData(profName, connectParent, param, groupType);
//   fBase_c *res = (fBase_c *) (*fProfListMg_c::m_data_p)[profName].mBaseProfile->mpClassInit();
//   setTmpCtData(0, nullptr, 0, 0);
//   if (res != nullptr) res->runCreate();
//   return res;
// This override REIMPLEMENTS that exact body (rather than delegating to 0x80162BB0, which would
// be this override's own address - see nsmbw_gamesetup_createpack_diag.cpp's header comment for
// why that specific mistake hangs the game), calling only other, distinct, already-confirmed real
// addresses: setTmpCtData (0x80162B90), runCreate (0x80162A70), and fProfListMg_c::m_data_p's
// known address (0x8042A698, from nsmbw_create_next_scene_diag.cpp). mpClassInit is read live from
// the profile's own ROM data, not hardcoded, since it differs per profile.
//
// connectParent is the PARENT's fTrNdBa_c connect-tree node, not the parent fBase_c itself - its
// owning object lives at node+0x10 (mpOwner, confirmed offset already used by
// nsmbw_check_child_create_diag.cpp's tree walk), which is how this identifies "this specific
// LASTACTOR belongs to GAME_SETUP" without needing fBase_c/fManager_c's full field layout.
#include "hle_stubs.h"
#include <aurora/env.hpp>
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"

#include <cstdio>

extern "C" uint32_t g_nsmbwGameSetupScenePtr;
extern "C" uint32_t g_nsmbwGameSetupLastActorPtr = 0;

namespace {
constexpr uint32_t kProfListMgDataPAddr = 0x8042A698u;
constexpr uint32_t kSetTmpCtDataAddr = 0x80162B90u;
constexpr uint32_t kRunCreateAddr = 0x80162A70u;
constexpr uint32_t kMpOwnerOffset = 0x10u;
constexpr uint32_t kLastActorProfile = 749u;
constexpr uint32_t kGameSetupProfile = 0xAu;
// fBase_c::mProfName offset, confirmed empirically: nsmbw_create_next_scene_diag.cpp's raw dump
// of a live GAME_SETUP object showed word8..11 = 0x000A0100 - byte[8:10] (big-endian u16) = 0x000A
// = 10 = GAME_SETUP's own profile id, exactly matching fBase_c.hpp's declared field order
// (mUniqueID:4 + mParam:4 immediately before mProfName). Reading this directly, rather than
// relying on some other diagnostic's own captured pointer, avoids a construction-order race - this
// LASTACTOR is created synchronously *during* its parent's own construction (dScene_c::postCreate,
// called from inside fBase_make's res->runCreate() below), i.e. before fBase_make even returns to
// whatever caller would otherwise record the parent's pointer.
constexpr uint32_t kProfNameOffset = 0x08u;

// Fixed PMF triples for fBase_c's virtual execute()/postExecute() slots - these describe *which
// vtable slot* to call, not a per-object address, so they're valid for any fBase_c-family object
// (already confirmed working for GAME_SETUP in nsmbw_gamesetup_executepack_diag.cpp; same
// constants reused here for LASTACTOR).
constexpr uint32_t kExecuteTriple = 0x803296ECu;
constexpr uint32_t kPostExecuteTriple = 0x803296F8u;

void ResolveAndDumpVirtual(const char* label, uint32_t objPtr, uint32_t triple) {
    try {
        const uint32_t word0 = Memory::Read32(triple);
        const uint32_t word1 = Memory::Read32(triple + 4);
        const uint32_t word2 = Memory::Read32(triple + 8);
        const uint32_t adjustedThis = objPtr + word2;
        if (static_cast<int32_t>(word1) < 0) {
            std::fprintf(stderr, "[nsmbw][diag] LASTACTOR %s is non-virtual: funcAddr=0x%08X\n", label, word0);
            return;
        }
        const uint32_t vtablePtr = Memory::Read32(adjustedThis + word0);
        const uint32_t funcAddr = Memory::Read32(vtablePtr + word1);
        std::fprintf(stderr,
            "[nsmbw][diag] LASTACTOR %s RESOLVED: vtablePtr=0x%08X funcAddr=0x%08X\n",
            label, vtablePtr, funcAddr);
        char hexdump[3 * 320 + 1] = {0};
        int hexoff = 0;
        for (int i = 0; i < 320; ++i) {
            const uint8_t b = Memory::Read8(funcAddr + static_cast<uint32_t>(i));
            hexoff += std::snprintf(hexdump + hexoff, sizeof(hexdump) - static_cast<size_t>(hexoff), "%02x", b);
        }
        std::fprintf(stderr, "[nsmbw][diag] LASTACTOR %s bytes @0x%08X: %s\n", label, funcAddr, hexdump);
        std::fflush(stderr);
    } catch (const Memory::AccessViolation& e) {
        std::fprintf(stderr, "[nsmbw][diag] LASTACTOR %s resolve CAUGHT AccessViolation reason=%.*s\n",
            label, static_cast<int>(e.reason().size()), e.reason().data());
        std::fflush(stderr);
    }
}
}

extern "C" uint32_t FBaseMake_Diag_80162BB0(uint32_t profName, uint32_t connectParent, uint32_t param,
                                             uint32_t groupType)
{
    const uint32_t arrayBase = Memory::Read32(kProfListMgDataPAddr);
    const uint32_t mBaseProfilePtr = Memory::Read32(arrayBase + profName * 4u);
    if (mBaseProfilePtr == 0) {
        return 0;
    }

    auto& cpu = GetPersistentCpuContext();

    cpu.gpr[3] = profName;
    cpu.gpr[4] = connectParent;
    cpu.gpr[5] = param;
    cpu.gpr[6] = groupType;
    InvokeIndirectCpu(kSetTmpCtDataAddr, &cpu);

    const uint32_t mpClassInit = Memory::Read32(mBaseProfilePtr);
    InvokeIndirectCpu(mpClassInit, &cpu);
    const uint32_t res = cpu.gpr[3];

    cpu.gpr[3] = 0;
    cpu.gpr[4] = 0;
    cpu.gpr[5] = 0;
    cpu.gpr[6] = 0;
    InvokeIndirectCpu(kSetTmpCtDataAddr, &cpu);

    if (res != 0) {
        cpu.gpr[3] = res;
        InvokeIndirectCpu(kRunCreateAddr, &cpu);
    }

    // NSMBW_LOG_LIQUID: AC_BG_WATER/LAVA/POISON/SAND/CLOUD are profiles 596-600.
    static const bool logLiquid = AURORA_ENV("NSMBW_LOG_LIQUID") != nullptr;
    if (logLiquid && profName >= 596u && profName <= 600u) {
        std::fprintf(stderr, "[nsmbw][liquid] created profile %u -> actor 0x%08X (param=0x%08X)\n", profName, res, param);
    }

    if (profName == kLastActorProfile) {
        uint32_t ownerPtr = 0;
        uint16_t ownerProfile = 0xFFFFu;
        try {
            ownerPtr = Memory::Read32(connectParent + kMpOwnerOffset);
            ownerProfile = Memory::Read16(ownerPtr + kProfNameOffset);
        } catch (const Memory::AccessViolation&) {
            ownerPtr = 0xFFFFFFFFu;
        }
        const bool isGameSetupsOwn = (ownerProfile == kGameSetupProfile);
        std::fprintf(stderr,
            "[nsmbw][diag] LASTACTOR created: res=0x%08X connectParent=0x%08X ownerPtr=0x%08X "
            "ownerProfile=0x%X %s\n",
            res, connectParent, ownerPtr, ownerProfile, isGameSetupsOwn ? "<-- MATCH (GAME_SETUP's own)" : "");
        std::fflush(stderr);
        if (isGameSetupsOwn) {
            g_nsmbwGameSetupLastActorPtr = res;
            ResolveAndDumpVirtual("execute()", res, kExecuteTriple);
            ResolveAndDumpVirtual("postExecute()", res, kPostExecuteTriple);
            // Dump the whole vtable at once instead of resolving one PMF triple at a time -
            // execute()'s resolved body (0x8006C5C0, function_map.txt: preExecute__7dBase_cFv)
            // itself calls ANOTHER virtual through slot offset 0x4c, i.e. one more indirection
            // this project's own address map doesn't explain by name alone. Reading every slot up
            // front and cross-checking against function_map.txt is far faster than chasing that
            // one slot at a time.
            try {
                const uint32_t vtablePtr = Memory::Read32(res + 0x60u);
                std::fprintf(stderr, "[nsmbw][diag] LASTACTOR vtable @0x%08X:\n", vtablePtr);
                for (uint32_t off = 0; off < 0x100u; off += 4u) {
                    const uint32_t entry = Memory::Read32(vtablePtr + off);
                    std::fprintf(stderr, "[nsmbw][diag]   [+0x%02X] = 0x%08X\n", off, entry);
                }
                std::fflush(stderr);
            } catch (const Memory::AccessViolation& e) {
                std::fprintf(stderr, "[nsmbw][diag] LASTACTOR vtable dump CAUGHT AccessViolation reason=%.*s\n",
                    static_cast<int>(e.reason().size()), e.reason().data());
                std::fflush(stderr);
            }
        }
    }

    return res;
}

PPC_NATIVE_OVERRIDE(80162BB0, FBaseMake_Diag_80162BB0,
                     uint32_t, (uint32_t profName, uint32_t connectParent, uint32_t param, uint32_t groupType),
                     (profName, connectParent, param, groupType));
