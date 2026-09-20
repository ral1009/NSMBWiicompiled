// Diagnostic (not a fix) continuing the ROOT_DISABLE_EXECUTE investigation past the previous
// nsmbw_wiistrap_create_diag.cpp result: that diagnostic proved dWiiStrap_c is NOT the child stuck
// in CREATING (it loaded on its very first call). A live run of that build then showed the fader
// cycle correctly through the whole boot sequence (WiiStrap fade in/out, Controller Info fade
// in/out, dScRestartCrsin_c::startTitle firing - confirmed via dScCrsin_c::m_isDispOff flipping to
// 1, one of only three real writers of that flag) and then sit at mStatus=OPAQUE with
// m_isAutoFadeIn=1 for 900+ consecutive dFader_c::calc() frames with zero further
// dFader_c::startFadeIn calls logged. dScene_c::preExecute() only reaches its own auto-fade-in
// check *after* the ROOT_DISABLE_EXECUTE gate - so this shape (OPAQUE forever, autoFadeIn=1, no
// startFadeIn) is the exact signature of the ROOT_DISABLE_EXECUTE stall recurring on whatever new
// scene dScRestartCrsin_c::startTitle creates (a fresh dScene_c subclass instance gets its own
// fresh ROOT_DISABLE_EXECUTE per dScene_c's constructor - this is not the same stall instance as
// the WiiStrap one already ruled out).
//
// This hooks fBase_c::checkChildProcessCreateState() (0x80162B60, confirmed real address from
// projects/nsmbw/function_map.txt) to find out, directly, which scene is asking and which child it
// is blocked on - rather than guessing from source which scene/child that might be.
//
// Disassembling both 0x80162B60 and its helper 0x80162AF0
// (fBase_c::getChildProcessCreateState() const) confirms the exact original prompt's claim: the
// walk reads `curr->mpOwner` from the tree node at +0x10, then tests `mpOwner`'s own
// LIFECYCLE_e state byte at **+0xA** against 0 (CREATING) - `checkChildProcessCreateState()`
// itself is just `return getChildProcessCreateState() != nullptr;` compiled to a branch-free
// neg/or/srwi bool idiom. Both facts are read off the real disassembly, not guessed.
//
// This override delegates the entire tree walk to the real, untouched, separately-addressed
// getChildProcessCreateState() (0x80162AF0) via InvokeIndirectCpu - it is not reimplemented here,
// so no fBase_c/fTrNdBa_c layout beyond the two offsets above (both verified above) is assumed.
// The override is otherwise value-identical to the original (same nonzero-pointer-to-bool
// conversion), so this changes no guest-visible behavior.
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"

#include <cstdio>

namespace {
constexpr uint32_t kGetChildProcessCreateStateAddr = 0x80162AF0u;
constexpr uint32_t kLifecycleStateOffset = 0xAu; // verified via disassembly, see header comment

uint32_t g_lastLoggedThis = 0;
uint32_t g_lastLoggedChild = 0;
uint64_t g_sameStateStreak = 0;
}

extern "C" uint32_t CheckChildProcessCreateState_Diag_80162B60(uint32_t thisPtr)
{
    auto& cpu = GetPersistentCpuContext();
    cpu.gpr[3] = thisPtr;
    InvokeIndirectCpu(kGetChildProcessCreateStateAddr, &cpu);
    const uint32_t stuckChildPtr = cpu.gpr[3];

    const bool changed = (thisPtr != g_lastLoggedThis) || (stuckChildPtr != g_lastLoggedChild);
    if (changed) {
        g_sameStateStreak = 0;
    }
    ++g_sameStateStreak;

    // Log every transition immediately, plus a heartbeat every 120 calls (~2s at 60fps) while a
    // single (scene, stuck child) pair persists, so a long stall is still visible without spam.
    if (changed || (g_sameStateStreak % 120) == 0) {
        if (stuckChildPtr != 0) {
            const uint8_t lifecycleState = Memory::Read8(stuckChildPtr + kLifecycleStateOffset);
            std::fprintf(stderr,
                "[nsmbw][diag] checkChildProcessCreateState: scene=0x%08X BLOCKED on child=0x%08X "
                "lifecycleState=%u (0=CREATING) streak=%llu\n",
                thisPtr, stuckChildPtr, lifecycleState,
                static_cast<unsigned long long>(g_sameStateStreak));
        } else {
            std::fprintf(stderr,
                "[nsmbw][diag] checkChildProcessCreateState: scene=0x%08X CLEAR (no child creating)\n",
                thisPtr);
        }
        std::fflush(stderr);
    }

    g_lastLoggedThis = thisPtr;
    g_lastLoggedChild = stuckChildPtr;

    return stuckChildPtr != 0 ? 1u : 0u;
}

PPC_NATIVE_OVERRIDE(80162B60, CheckChildProcessCreateState_Diag_80162B60, uint32_t, (uint32_t thisPtr), (thisPtr));
