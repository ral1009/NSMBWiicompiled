// Diagnostic (not a fix) for the ROOT_DISABLE_EXECUTE / checkChildProcessCreateState() stall:
// confirms or rules out dWiiStrap_c as the specific child stuck in fBase_c::LIFECYCLE_e::CREATING.
//
// dWiiStrap_c::create() (0x8010F170, confirmed against this project's own
// projects/nsmbw/function_map.txt, not the cross-version NSMBW-Maps address-map.txt - that file
// maps address DELTAS between game revisions, not real addresses for this build, and gave a
// nonsense disassembly when tried here first) is exactly the decompiled logic from
// NSMBW-Decomp source/dol/bases/d_WiiStrap.cpp:
//   int dWiiStrap_c::create() {
//       if (mHasLoadedLayout) return SUCCEEDED;
//       if (!createLayout()) return NOT_READY;
//       mHasLoadedLayout = true; mVisible = true; return SUCCEEDED;
//   }
// Disassembling 0x8010F170 directly confirms the two field offsets used below - mHasLoadedLayout
// at this+0x208, mVisible at this+0x209 - and that createLayout() is called at 0x8010F1D0:
//   lbz r0, 0x208(r3); cmpwi r0,0; beq ...        ; if (mHasLoadedLayout)
//   bl 0x8010f1d0                                  ; createLayout()
//   ...
//   stb r0, 0x208(r31); stb r0, 0x209(r31)         ; mHasLoadedLayout = mVisible = true
//
// fBase_c::runCreate() re-invokes create() every single frame while the base stays in
// LIFECYCLE_e::CREATING (f_base.cpp:450-463), so if archive loading for WiiStrap.arc never
// completes, this override sees create() called forever with mHasLoadedLayout staying 0 - that's
// the direct, verifiable signature of the theory from the DVD-loader disassembly trace (dDvd::
// loader_c::request -> mDvd_toMainRam_c::create -> enqueue -> OSWakeupThread on a DVD-service
// thread whose scheduling was never confirmed). If instead this fires only a handful of times and
// then stops (mHasLoadedLayout flips to 1), WiiStrap is NOT the stuck child and the real one is
// elsewhere in the scene's connect-tree (LASTACTOR, most likely, per dScene_c::postCreate).
//
// This is a faithful reimplementation of the exact disassembled logic (same shape as this
// project's existing StartFadeIn_800B0A20_Diag / Calc_800B0B00_Diag in nsmbw_tick_read_pump.cpp),
// not a behavior change - it calls the real createLayout() and sets the real fields exactly as
// the original does. Only the logging is new.
#include "hle_stubs.h"
#include <aurora/env.hpp>
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"

#include <cstdio>
#include <cstdlib>

namespace {
constexpr uint32_t kHasLoadedLayoutOffset = 0x208;
constexpr uint32_t kVisibleOffset = 0x209;
constexpr uint32_t kCreateLayoutAddr = 0x8010F1D0u;

// TEMPORARY (continuation of the WiiStrap invisible-content investigation): live tracing
// isolated one specific pane's vertex corner-color word to a fixed heap address that reads
// (255,255,255,0) - alpha zero - for the entire run, never changing, while sibling panes at
// nearby addresses read correct/opaque colors. The .brlyt file's own on-disk data for every pane
// checked (including this content's neighbors) stores that same color as fully-opaque
// (0xFFFFFFFF), so the corruption must happen during createLayout()'s resource parsing rather
// than being authored data or a later frame-driven animation. Checking the same address
// immediately after createLayout() returns - before any draw has run - narrows the write to
// somewhere inside createLayout() itself if it is already wrong at this point.
uint32_t g_watchAddr = 0;
}

extern "C" uint32_t WiiStrapCreate_Diag_8010F170(uint32_t thisPtr)
{
    static uint64_t callCount = 0;
    ++callCount;

    const uint8_t hadLoadedBefore = Memory::Read8(thisPtr + kHasLoadedLayoutOffset);

    uint32_t result;
    if (hadLoadedBefore != 0) {
        result = 1; // SUCCEEDED - already loaded, matches the original's early-out
    } else {
        auto& cpu = GetPersistentCpuContext();
        cpu.gpr[3] = thisPtr;
        InvokeIndirectCpu(kCreateLayoutAddr, &cpu);
        const uint32_t layoutResult = cpu.gpr[3];

        // Checked immediately after createLayout() returns, before any draw call has run -
        // if the watch address is already wrong here, the corruption happens during layout
        // parsing, not in the later per-frame draw/animation loop already traced.
        if (const char* watchEnv = AURORA_ENV("NSMBW_WATCH_ADDR")) {
            g_watchAddr = static_cast<uint32_t>(std::strtoul(watchEnv, nullptr, 16));
        }
        if (g_watchAddr != 0) {
            try {
                const uint32_t watchVal = Memory::Read32(g_watchAddr);
                std::fprintf(stderr,
                    "[nsmbw][diag] post-createLayout() watch addr=0x%08X value=0x%08X (checked right after "
                    "createLayout() returned, before any draw)\n",
                    g_watchAddr, watchVal);
                std::fflush(stderr);
            } catch (const Memory::AccessViolation& e) {
                std::fprintf(stderr,
                    "[nsmbw][diag] post-createLayout() watch addr=0x%08X: CAUGHT AccessViolation reason=%.*s\n",
                    g_watchAddr, static_cast<int>(e.reason().size()), e.reason().data());
                std::fflush(stderr);
            }
        }

        if (layoutResult == 0) {
            result = 0; // NOT_READY - archive/layout still loading
        } else {
            Memory::Write8(thisPtr + kHasLoadedLayoutOffset, 1);
            Memory::Write8(thisPtr + kVisibleOffset, 1);
            result = 1; // SUCCEEDED
        }
    }

    // First 10 calls unthrottled (catches the transition instantly if it's fast), then once a
    // second at 60fps, so a truly-stuck case doesn't spam the log for minutes of runtime.
    if (callCount <= 10 || (callCount % 60) == 0) {
        std::fprintf(stderr,
            "[nsmbw][diag] dWiiStrap_c::create() call #%llu this=0x%08X hadLoadedBefore=%u -> result=%u (%s)\n",
            static_cast<unsigned long long>(callCount), thisPtr, hadLoadedBefore, result,
            result ? "SUCCEEDED" : "NOT_READY");
        std::fflush(stderr);
    }

    return result;
}

PPC_NATIVE_OVERRIDE(8010F170, WiiStrapCreate_Diag_8010F170, uint32_t, (uint32_t thisPtr), (thisPtr));
