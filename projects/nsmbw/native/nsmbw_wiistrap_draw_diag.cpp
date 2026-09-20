// Diagnostic (not a fix), continuing the WiiStrap invisible-content investigation.
// nsmbw_wiistrap_execute_diag.cpp bracketed dWiiStrap_c::execute() and found the watch address
// (a specific pane's vertex corner-color word, confirmed constant at (255,255,255,0) - alpha
// zero - for the entire run) goes from an unrelated pointer value to literally 0x00000000
// (all four bytes, not just alpha) immediately after LytBase_c::calc() returns - i.e. calc()
// resets/invalidates this slot. Since the actual draw-time value has RGB=(255,255,255) (not 0),
// something after calc() must fill in RGB while leaving alpha untouched at the reset value.
// dWiiStrap_c::draw() is the next, and last, step before this data reaches the GPU:
//   int dWiiStrap_c::draw() {
//       if (mHasLoadedLayout && mVisible) { mLayout.entry(); }
//       return SUCCEEDED;
//   }
// Disassembly of draw() (0x8010F4E0, against original/wiimj2d.dol) confirms:
//   lbz r0,0x208(r3); cmpwi r0,0; beq skip      ; mHasLoadedLayout
//   lbz r0,0x209(r3); cmpwi r0,0; beq skip      ; mVisible
//   addi r3,r3,0x70; bl 0x80006ea0              ; LytBase_c::entry(&mLayout) - real, direct call
// (mLayout at this+0x70, same offset already confirmed in the execute() override.) This
// reimplements draw() exactly, bracketing the one real sub-call with a watch-address read
// before and after, to see whether entry() is what fills in RGB while leaving alpha at 0.
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
constexpr uint32_t kEntryAddr = 0x80006EA0u;

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
        std::fprintf(stderr, "[nsmbw][diag] draw() watch %s addr=0x%08X value=0x%08X\n", when, addr, val);
    } catch (const Memory::AccessViolation& e) {
        std::fprintf(stderr, "[nsmbw][diag] draw() watch %s addr=0x%08X: CAUGHT AccessViolation reason=%.*s\n",
            when, addr, static_cast<int>(e.reason().size()), e.reason().data());
    }
    std::fflush(stderr);
}
} // namespace

extern "C" uint32_t WiiStrapDraw_Diag_8010F4E0(uint32_t thisPtr)
{
    static uint64_t callCount = 0;
    ++callCount;

    const uint8_t hasLoadedLayout = Memory::Read8(thisPtr + kHasLoadedLayoutOffset);
    const uint8_t visible = Memory::Read8(thisPtr + kVisibleOffset);
    const bool shouldLog = callCount <= 5;

    if (hasLoadedLayout != 0 && visible != 0) {
        if (shouldLog) LogWatch("BEFORE entry()");

        auto& cpu = GetPersistentCpuContext();
        cpu.gpr[3] = thisPtr + kMLytOffset;
        InvokeIndirectCpu(kEntryAddr, &cpu);

        if (shouldLog) LogWatch("AFTER entry()");
    }

    if (shouldLog) {
        std::fprintf(stderr, "[nsmbw][diag] dWiiStrap_c::draw() call #%llu this=0x%08X ran=%d\n",
            static_cast<unsigned long long>(callCount), thisPtr, (hasLoadedLayout != 0 && visible != 0) ? 1 : 0);
        std::fflush(stderr);
    }

    return 1; // SUCCEEDED
}

PPC_NATIVE_OVERRIDE(8010F4E0, WiiStrapDraw_Diag_8010F4E0, uint32_t, (uint32_t thisPtr), (thisPtr));
