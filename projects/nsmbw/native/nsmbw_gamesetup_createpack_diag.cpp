// Diagnostic (not a fix): continuing the "title screen never appears" investigation past
// nsmbw_create_next_scene_diag.cpp's finding that the boot sequence reaches GAME_SETUP (profile
// 0xA) and then never calls createNextScene() with a new target again - it just idles forever
// with nextScene=INVALID(0x2EE).
//
// Read against NSMBW-Decomp's fully-decompiled source/dol/framework/f_base.cpp: an fBase_c stays
// in LIFECYCLE_e::CREATING until its own (virtual, per-subclass) create() returns something other
// than NOT_READY. fBase_c::runCreate() (0x80162A70, confirmed real address) drives this every
// frame for every object still in the create list: it calls createPack() -> commonPack(&create,
// &preCreate, &postCreate); postCreate() only moves the object onto the execute/draw lists (and
// flips mLifecycleState to ACTIVE) when create() returned SUCCEEDED. If create() returns NOT_READY
// forever, runCreate() just re-queues the same object via
// fManager_c::m_createManage.addLastLineNode() every frame, forever - the exact shape of our
// stall, just for GAME_SETUP's own object instead of one of its children.
//
// GAME_SETUP's own concrete class (whatever it's actually named) is NOT decompiled - only the
// fProf::fBaseProfile_c declaration exists (include/game/bases/d_profile.hpp), no .cpp - so its
// create() can't be read as source. But fBase_c::createPack() (0x80161F80, confirmed real address,
// fully decompiled and NOT virtual itself) is the single, generic entry point EVERY fBase_c
// subclass's create cycle goes through, and it directly returns create()'s result (via
// commonPack's `result = (this->*doFunc)()`, where doFunc==&fBase_c::create is a *virtual*
// member-function pointer - so this correctly dispatches to GAME_SETUP's own override even though
// createPack() itself never mentions that subclass). Hooking this one well-understood function and
// filtering by `this` (via nsmbw_create_next_scene_diag.cpp's g_nsmbwGameSetupScenePtr, captured
// fresh each run since heap addresses aren't stable across runs) reveals create()'s real return
// value every frame without needing GAME_SETUP's own address at all.
//
// IMPORTANT: this does NOT delegate to createPack() itself (0x80161F80, the address being
// overridden) - an earlier version of this file did exactly that and hung the game almost
// immediately at boot, because InvokeIndirectCpu(0x80161F80, ...) from inside the override
// installed AT 0x80161F80 just calls straight back into this same override, forever (infinite
// self-recursion, not a real stack overflow crash since each level tail-calls rather than
// growing - it just never returns). The fix: reimplement createPack()'s own one-line body
// instead, `return commonPack(&fBase_c::create, &fBase_c::preCreate, &fBase_c::postCreate)`,
// calling the REAL commonPack (0x80161E00, a *different*, non-overridden address - confirmed via
// disassembly of createPack() itself) directly, so nothing here ever re-enters this address.
//
// commonPack's calling convention (also read off createPack()'s disassembly, since it just loads
// 9 constant words from ROM and passes 3 pointers to them): r3=this, r4/r5/r6 = pointers to the
// preCreate/create/postCreate MetroWerks pointer-to-member-function triples respectively - each
// triple is 3 words {func-or-vtable-offset, vtable-index-or-(-1), this-adjustment}, consumed by
// the compiler's shared __ptmf_scall trampoline (0x802DCEEC) to do the actual virtual dispatch.
// These 9 words are fixed ROM constants embedded in createPack()'s own code (not per-instance
// data), computed once from its `lis r28,-0x7fcd; addi r28,r28,-0x6968` load: preCreate triple at
// 0x80329698, create triple at 0x803296A4, postCreate triple at 0x803296B0 - safe to hardcode
// since they're literal immediates in the original binary, not runtime-computed.
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"

#include <cstdio>

extern "C" uint32_t g_nsmbwGameSetupScenePtr;

namespace {
constexpr uint32_t kCommonPackAddr = 0x80161E00u;
constexpr uint32_t kPreCreateTriple = 0x80329698u;
constexpr uint32_t kCreateTriple = 0x803296A4u;
constexpr uint32_t kPostCreateTriple = 0x803296B0u;
}

extern "C" uint32_t GameSetupCreatePack_Diag_80161F80(uint32_t thisPtr)
{
    auto& cpu = GetPersistentCpuContext();
    cpu.gpr[3] = thisPtr;
    cpu.gpr[4] = kPreCreateTriple;
    cpu.gpr[5] = kCreateTriple;
    cpu.gpr[6] = kPostCreateTriple;
    InvokeIndirectCpu(kCommonPackAddr, &cpu);
    const uint32_t result = cpu.gpr[3];

    if (thisPtr == g_nsmbwGameSetupScenePtr && g_nsmbwGameSetupScenePtr != 0) {
        static uint64_t callCount = 0;
        ++callCount;
        // 0=NOT_READY(retry), 1=SUCCEEDED, otherwise an error code - matches fBase_c::commonPack's
        // MAIN_STATE_e mapping (result==NOT_READY -> WAITING, ==SUCCEEDED -> SUCCESS, else ERROR).
        if (callCount <= 20 || (callCount % 60) == 0) {
            std::fprintf(stderr,
                "[nsmbw][diag] GAME_SETUP createPack() call #%llu this=0x%08X -> result=%u "
                "(0=NOT_READY 1=SUCCEEDED other=ERROR)\n",
                static_cast<unsigned long long>(callCount), thisPtr, result);
            std::fflush(stderr);
        }
    }

    return result;
}

PPC_NATIVE_OVERRIDE(80161F80, GameSetupCreatePack_Diag_80161F80, uint32_t, (uint32_t thisPtr), (thisPtr));
