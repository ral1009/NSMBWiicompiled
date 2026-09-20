// Diagnostic (not a fix), continuing the WiiStrap invisible-content investigation: a specific
// pane's vertex corner-color word was isolated to a fixed heap address that reads
// (255,255,255,0) - alpha zero - for the entire run, confirmed via nsmbw_wiistrap_create_diag.cpp
// to NOT yet hold that value immediately after createLayout() returns (it holds an unrelated
// pointer at that point) - so the corruption happens later, during the per-frame
// execute()/calc() cycle, not during resource loading.
//
// dWiiStrap_c::execute() (0x8010F480, disassembled offline against original/wiimj2d.dol) is
// exactly the decompiled logic from NSMBW-Decomp source/dol/bases/d_WiiStrap.cpp:
//   int dWiiStrap_c::execute() {
//       if (mHasLoadedLayout && mVisible) {
//           mLayout.AnimePlay();
//           mLayout.calc();
//       }
//       return SUCCEEDED;
//   }
// Disassembly confirms:
//   lbz r0,0x208(r3); cmpwi r0,0; beq skip      ; mHasLoadedLayout
//   lbz r0,0x209(r3); cmpwi r0,0; beq skip      ; mVisible
//   addi r3,r3,0x70; bl 0x800c9650              ; LytBase_c::AnimePlay(&mLayout) - real, direct
//   lwz r12,0x78(r31); addi r3,r31,0x70         ; mLayout's own vtable ptr, at mLayout+0x8
//   lwz r12,0x10(r12); mtctr r12; bctrl         ; mLayout.calc() - virtual, vtable slot 0x10
// (mLayout lives at this+0x70; its own vtable pointer is at mLayout+0x8 = this+0x78 - same
// offsets already confirmed independently in nsmbw_wipecircle_calc_diag.cpp for a different
// LytBase_c instance, good cross-check that these offsets are a stable, real ABI fact and not
// per-instance coincidence.)
//
// This overrides execute() (not AnimePlay/calc individually) so the watch-address read can be
// bracketed cleanly around each real sub-call without needing a second override on calc()'s
// (shared, heavily-reused) vtable target - reimplementing the whole tiny function exactly, then
// calling only the two real, distinct, non-self addresses, following the same rule already
// established in nsmbw_wipecircle_calc_diag.cpp for why this doesn't self-recurse.
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"

#include <cstdio>
#include <cstdlib>

namespace {
constexpr uint32_t kHasLoadedLayoutOffset = 0x208;
constexpr uint32_t kVisibleOffset = 0x209;
constexpr uint32_t kMLytOffset = 0x70;
constexpr uint32_t kMLytVtableOffset = 0x8;
constexpr uint32_t kLytCalcVtableSlot = 0x10;
constexpr uint32_t kAnimePlayAddr = 0x800C9650u;

// Same LytBase_c::AnimePlay offsets already confirmed in nsmbw_wipecircle_calc_diag.cpp for a
// DIFFERENT LytBase_c instance (WipeCircle's mLyt) - reused here for WiiStrap's own mLayout, to
// compare which of WiiStrap's own anim groups are actually enabled/advancing versus stuck. This
// is the same "check other panes" comparison already done empirically via live vertex-color
// polling (one pane's resolved color smoothly fades, a sibling pane's never moves) - this looks
// at the animation-group state directly instead of inferring it from the color output.
constexpr uint32_t kMpAnimGroupOffset = 0x184u;
constexpr uint32_t kMpEnabledAnimsOffset = 0x188u;
constexpr uint32_t kMAnimGroupCountOffset = 0x190u;
constexpr uint32_t kMLastStartedAnimNumOffset = 0x194u;
constexpr uint32_t kAnimGroupStride = 0x28u;
constexpr uint32_t kCurrFrameOffset = 0x18u;
constexpr uint32_t kEndFrameOffset = 0x14u;
constexpr uint32_t kRateOffset = 0x20u;

void LogAnimGroups(const char* when, uint32_t mLytBase) {
    try {
        const uint32_t animGroupBase = Memory::Read32(mLytBase + kMpAnimGroupOffset);
        const uint32_t enabledAnimsBase = Memory::Read32(mLytBase + kMpEnabledAnimsOffset);
        const uint32_t animGroupCount = Memory::Read32(mLytBase + kMAnimGroupCountOffset);
        const uint32_t lastStarted = Memory::Read32(mLytBase + kMLastStartedAnimNumOffset);
        std::fprintf(stderr,
            "[nsmbw][diag] WiiStrap mLayout %s: mAnimGroupCount=%u lastStartedAnimNum=%u\n",
            when, animGroupCount, lastStarted);
        for (uint32_t i = 0; i < animGroupCount && i < 8; ++i) {
            const uint32_t elem = animGroupBase + i * kAnimGroupStride;
            const uint8_t enabled = Memory::Read8(enabledAnimsBase + i);
            const float currFrame = Memory::ReadFloat32(elem + kCurrFrameOffset);
            const float endFrame = Memory::ReadFloat32(elem + kEndFrameOffset);
            const float rate = Memory::ReadFloat32(elem + kRateOffset);
            std::fprintf(stderr,
                "[nsmbw][diag]   anim[%u] elem=0x%08X enabled=%u currFrame=%f endFrame=%f rate=%f\n",
                i, elem, enabled, currFrame, endFrame, rate);
        }
        std::fflush(stderr);
    } catch (const Memory::AccessViolation& e) {
        std::fprintf(stderr, "[nsmbw][diag] WiiStrap mLayout %s: CAUGHT AccessViolation reason=%.*s\n",
            when, static_cast<int>(e.reason().size()), e.reason().data());
        std::fflush(stderr);
    }
}

uint32_t WatchAddr() {
    static const uint32_t addr = [] {
        const char* env = std::getenv("NSMBW_WATCH_ADDR");
        return env ? static_cast<uint32_t>(std::strtoul(env, nullptr, 16)) : 0u;
    }();
    return addr;
}

void LogWatch(const char* when) {
    const uint32_t addr = WatchAddr();
    if (addr == 0) return;
    try {
        const uint32_t val = Memory::Read32(addr);
        std::fprintf(stderr, "[nsmbw][diag] execute() watch %s addr=0x%08X value=0x%08X\n", when, addr, val);
    } catch (const Memory::AccessViolation& e) {
        std::fprintf(stderr, "[nsmbw][diag] execute() watch %s addr=0x%08X: CAUGHT AccessViolation reason=%.*s\n",
            when, addr, static_cast<int>(e.reason().size()), e.reason().data());
    }
    std::fflush(stderr);
}
} // namespace

extern "C" uint32_t WiiStrapExecute_Diag_8010F480(uint32_t thisPtr)
{
    static uint64_t callCount = 0;
    ++callCount;

    const uint8_t hasLoadedLayout = Memory::Read8(thisPtr + kHasLoadedLayoutOffset);
    const uint8_t visible = Memory::Read8(thisPtr + kVisibleOffset);

    const bool shouldLog = callCount <= 15; // widened to see whether currFrame ever advances

    if (hasLoadedLayout != 0 && visible != 0) {
        if (shouldLog) LogWatch("BEFORE AnimePlay");

        auto& cpu = GetPersistentCpuContext();
        const uint32_t mLytBase = thisPtr + kMLytOffset;
        if (shouldLog) LogAnimGroups("BEFORE AnimePlay", mLytBase);
        cpu.gpr[3] = mLytBase;
        InvokeIndirectCpu(kAnimePlayAddr, &cpu);
        if (shouldLog) LogAnimGroups("AFTER AnimePlay", mLytBase);

        if (shouldLog) LogWatch("AFTER AnimePlay, BEFORE calc");

        const uint32_t vtablePtr = Memory::Read32(mLytBase + kMLytVtableOffset);
        const uint32_t calcAddr = Memory::Read32(vtablePtr + kLytCalcVtableSlot);
        cpu.gpr[3] = mLytBase;
        InvokeIndirectCpu(calcAddr, &cpu);

        if (shouldLog) LogWatch("AFTER calc");
    }

    if (shouldLog) {
        std::fprintf(stderr, "[nsmbw][diag] dWiiStrap_c::execute() call #%llu this=0x%08X ran=%d\n",
            static_cast<unsigned long long>(callCount), thisPtr, (hasLoadedLayout != 0 && visible != 0) ? 1 : 0);
        std::fflush(stderr);
    }

    return 1; // SUCCEEDED
}

PPC_NATIVE_OVERRIDE(8010F480, WiiStrapExecute_Diag_8010F480, uint32_t, (uint32_t thisPtr), (thisPtr));
