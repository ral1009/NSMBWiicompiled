// Diagnostic (not a fix): the user-visible symptom is a screen with everything black except a
// solid blue rectangle that stays PIXEL-IDENTICAL for ~2 real seconds before the boot sequence
// moves on. A pane-identity survey (gx_texture.cpp's NSMBW_LOG_PANE_IDENTITY, gated to scene
// profile 0x5/STAGE via g_nsmbwCurrentSceneProfile) found the pane behind it is "W_circle_00",
// which belongs to dWipeCircle_c (NSMBW-Decomp source/dol/bases/d_wipe_circle.cpp) - the standard
// circular-wipe scene transition ("Seen when entering a pipe, for example," per its own header
// doc comment). A wipe transition that's supposed to animate a shrinking/growing circle but
// instead renders as one static rectangle for 2 seconds is consistent with its animation frame
// counter simply not advancing.
//
// CRASH POSTMORTEM (2026-09-06): the first version of this file hooked calc__13dWipeCircle_cFv
// (0x8001B580) and, to run the "real" calc(), called InvokeIndirectCpu(0x8001B580, ...) - i.e.
// back into its OWN overridden address. That doesn't work the way it does for MKW-style
// diagnostics elsewhere in this project: PPC_NATIVE_OVERRIDE makes the shard emitter EXCLUDE the
// translated PPC implementation for that address entirely (confirmed - the emitted function count
// drops by exactly one while this file is active), so there is no "original" left to call back
// into. InvokeIndirectCpu(0x8001B580, ...) just re-entered THIS SAME override, recursively,
// forever. The log proved it directly: 27,551 back-to-back "ENTER call #N" lines with zero other
// diagnostic output interleaved (no heartbeat, no SelectThread, nothing - meaning nothing else in
// the game ran between those calls), then silence. Windows reports that as STATUS_STACK_OVERFLOW,
// not EXCEPTION_ACCESS_VIOLATION, which is why nsmbw_product.cpp's vectored exception handler
// (which only checks for the latter) never printed anything - a silent, message-less death that
// looked identical to a native segfault until the call pattern was inspected.
// nsmbw_create_next_scene_diag.cpp and nsmbw_commonpack_diag.cpp never hit this because they only
// ever call INTO a *different* address than the one they override (dBase_c::createRoot; a
// resolved PTMF target) - never back into themselves. The fix below follows that same rule:
// calc()'s own logic is small enough to reimplement directly (matching
// NSMBW-Decomp/source/dol/bases/d_wipe_circle.cpp:70-87 exactly), invoking only the real,
// non-overridden addresses of its parts.
//
// Field/address offsets (all confirmed from real PPC disassembly, not inferred from the decomp
// headers' declaration order, since Metrowerks vtable placement isn't always at offset 0 - see
// the fBase_c/DynamicModuleControlBase precedent already documented elsewhere in this project):
//
//   dWipeCircle_c::calc (0x8001B580) disassembly shows:
//     lbz  r0, 0x1c0(r3)          ; mIsCreated
//     lwz  r0, 0x1bc(r3)          ; mAction  (IDLE == 3, ACTION_COUNT = IDLE per the header enum)
//     mulli r0, r0, 0xc; ...; bl 0x802dceec   ; (this->*Proc_tbl[mAction])() via __ptmf_scall,
//                                              ; but Proc_tbl is only 3 entries (OPEN_SETUP=0,
//                                              ; ANIME_END_CHECK=1, CLOSE_SETUP=2) and each is a
//                                              ; non-virtual member fn - resolved below by mAction
//                                              ; value directly against their real addresses
//                                              ; instead of replicating PTMF decode for a table
//                                              ; that's this simple.
//     addi r3, r31, 0x14          ; &mLyt == this+0x14, passed to mLyt.AnimePlay()/mLyt.calc()
//
//   LytBase_c::AnimePlay (0x800C9650), operating on that same mLyt (this+0x14 above) as `this`:
//     lwz  r3, 0x184(r28)         ; mpAnimGroup array base pointer
//     lwz  r3, 0x188(r28)         ; mpEnabledAnims array base pointer
//     lwz  r0, 0x190(r28)         ; mAnimGroupCount
//     (isAnime(-1), 0x800C9700, additionally confirms mLastStartedAnimNum at +0x194)
//     stride between mpAnimGroup[] elements is 0x28 (confirmed via addi r30,r30,0x28 in the loop)
//
//   Within one m2d::AnmGroup_c element (0x28 bytes), confirmed via AnimeStartBaseSetup
//   (0x800C9360: lwzux r3,r31,r0 loads the element's first field - AnmGroupBase_c::mpFrameCtrl -
//   as a POINTER, then that pointer is passed into FrameCtrl_c::setFrame), and via
//   FrameCtrl_c::setFrame/isStop/play (0x80163910/0x80163930/0x80163800, all operating on
//   *that* mpFrameCtrl pointer, i.e. the embedded m2d::AnmGroup_c::mFrameCtrl at element+0x10):
//     element + 0x00 = mpFrameCtrl (points to element+0x10, self-referential per AnmGroup_c's
//                      ctor "AnmGroupBase_c(&mFrameCtrl)")
//     element + 0x14 = mFrameCtrl.mEndFrame   (float)
//     element + 0x18 = mFrameCtrl.mCurrFrame  (float)   <- the thing we actually want to watch
//     element + 0x1C = mFrameCtrl.mPrevFrame  (float)
//     element + 0x20 = mFrameCtrl.mRate       (float)
//     element + 0x24 = mFrameCtrl.mFlags      (u8: bit0=NO_LOOP, bit1=REVERSE)
//   This size (0x28 total: 0x10 AnmGroupBase_c fields + 0x18 FrameCtrl_c fields, vtable included)
//   exactly matches the confirmed inter-element stride above - internally consistent, not guessed.
//
//   LytBase_c::calc() is virtual (dWipeCircle_c::calc disassembly dispatches it via
//   `lwz r12,0x1c(r31)` - vtable ptr stored at mLyt+0x8, i.e. dWipeCircle_c+0x1c - then
//   `lwz r12,0x10(r12); mtctr r12; bctrl`, i.e. vtable slot +0x10). Resolved the same way here
//   (read the vtable pointer, read slot +0x10, invoke whatever real address that names) rather
//   than hardcoding a guessed concrete address for it.
//
// ANIM_COUNT for dWipeCircle_c is 2 (inWindow=0, outWindow=1), so mAnimGroupCount should be 2
// once createLayout() has run. Logging both elements' mCurrFrame across consecutive calc() calls
// while mAction != IDLE directly answers "is the wipe animation started, and is its frame
// counter advancing." Remove once resolved.
#include "hle_stubs.h"
#include <aurora/env.hpp>
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"

#include <cstdio>

namespace {
constexpr uint32_t kMLytOffset = 0x14u;          // dWipeCircle_c::mLyt
constexpr uint32_t kMActionOffset = 0x1BCu;      // dWipeCircle_c::mAction
constexpr uint32_t kMIsCreatedOffset = 0x1C0u;   // dWipeCircle_c::mIsCreated
constexpr uint32_t kMLytVtableOffset = 0x8u;     // LytBase_c's own vtable ptr, relative to mLyt
constexpr uint32_t kLytCalcVtableSlot = 0x10u;   // LytBase_c::calc's slot in that vtable
constexpr uint32_t kMpAnimGroupOffset = 0x184u;  // LytBase_c::mpAnimGroup
constexpr uint32_t kMpEnabledAnimsOffset = 0x188u; // LytBase_c::mpEnabledAnims
constexpr uint32_t kMAnimGroupCountOffset = 0x190u; // LytBase_c::mAnimGroupCount
constexpr uint32_t kMLastStartedAnimNumOffset = 0x194u; // LytBase_c::mLastStartedAnimNum
constexpr uint32_t kAnimGroupStride = 0x28u;
constexpr uint32_t kCurrFrameOffset = 0x18u;
constexpr uint32_t kEndFrameOffset = 0x14u;
constexpr uint32_t kRateOffset = 0x20u;
constexpr uint32_t kActionIdle = 3u;

// Real, non-overridden addresses - all confirmed in projects/nsmbw/function_map.txt. None of
// these is the address this file overrides (0x8001B580), so invoking them via InvokeIndirectCpu
// cannot recurse back into this override the way calling 0x8001B580 itself did.
constexpr uint32_t kOpenSetupAddr = 0x8001B8D0u;      // dWipeCircle_c::OpenSetup (mAction==0)
constexpr uint32_t kAnimeEndCheckAddr = 0x8001B920u;  // dWipeCircle_c::AnimeEndCheck (mAction==1)
constexpr uint32_t kCloseSetupAddr = 0x8001B9D0u;     // dWipeCircle_c::CloseSetup (mAction==2)
constexpr uint32_t kAnimePlayAddr = 0x800C9650u;      // LytBase_c::AnimePlay

void InvokeWithThis(uint32_t targetAddr, uint32_t thisPtr) {
    auto& cpu = GetPersistentCpuContext();
    cpu.gpr[3] = thisPtr;
    InvokeIndirectCpu(targetAddr, &cpu);
}

void LogWipeCircleState(const char* when, uint32_t self) {
    try {
        const uint32_t mLytBase = self + kMLytOffset;
        const uint32_t action = Memory::Read32(self + kMActionOffset);
        const uint8_t isCreated = Memory::Read8(self + kMIsCreatedOffset);
        if (!isCreated) {
            std::fprintf(stderr, "[nsmbw][wipecircle] %s this=0x%08X mIsCreated=0 (layout not built yet)\n",
                         when, self);
            std::fflush(stderr);
            return;
        }

        const uint32_t animGroupBase = Memory::Read32(mLytBase + kMpAnimGroupOffset);
        const uint32_t enabledAnimsBase = Memory::Read32(mLytBase + kMpEnabledAnimsOffset);
        const uint32_t animGroupCount = Memory::Read32(mLytBase + kMAnimGroupCountOffset);
        const uint32_t lastStarted = Memory::Read32(mLytBase + kMLastStartedAnimNumOffset);

        std::fprintf(stderr,
            "[nsmbw][wipecircle] %s this=0x%08X mAction=%u mAnimGroupCount=%u lastStartedAnimNum=%u\n",
            when, self, action, animGroupCount, lastStarted);
        std::fflush(stderr);

        for (uint32_t i = 0; i < animGroupCount && i < 4; ++i) {
            const uint32_t elem = animGroupBase + i * kAnimGroupStride;
            const uint8_t enabled = Memory::Read8(enabledAnimsBase + i);
            const float currFrame = Memory::ReadFloat32(elem + kCurrFrameOffset);
            const float endFrame = Memory::ReadFloat32(elem + kEndFrameOffset);
            const float rate = Memory::ReadFloat32(elem + kRateOffset);
            std::fprintf(stderr,
                "[nsmbw][wipecircle]   anim[%u] elem=0x%08X enabled=%u currFrame=%f endFrame=%f rate=%f\n",
                i, elem, enabled, currFrame, endFrame, rate);
            std::fflush(stderr);
        }
    } catch (const Memory::AccessViolation& e) {
        std::fprintf(stderr,
            "[nsmbw][wipecircle] %s CAUGHT Memory::AccessViolation at 0x%08X len=%zu reason=%.*s\n",
            when, e.address(), e.length(), static_cast<int>(e.reason().size()), e.reason().data());
        std::fflush(stderr);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[nsmbw][wipecircle] %s CAUGHT std::exception: %s\n", when, e.what());
        std::fflush(stderr);
    }
}

}  // namespace

// Reimplements dWipeCircle_c::calc() (NSMBW-Decomp source/dol/bases/d_wipe_circle.cpp:70-87)
// directly instead of delegating to the real function at this same address - see the file header
// for why delegating to self recurses forever. Every sub-call below targets a real, distinct,
// non-overridden address.
extern "C" int32_t WipeCircleCalc_Diag_8001B580(uint32_t self) {
    static uint64_t callCount = 0;
    ++callCount;

    uint32_t action = kActionIdle;
    bool isCreated = false;
    try {
        isCreated = Memory::Read8(self + kMIsCreatedOffset) != 0;
        if (isCreated) {
            action = Memory::Read32(self + kMActionOffset);
        }
    } catch (const Memory::AccessViolation& e) {
        std::fprintf(stderr,
            "[nsmbw][wipecircle] call #%llu CAUGHT AccessViolation reading self=0x%08X: addr=0x%08X reason=%.*s\n",
            static_cast<unsigned long long>(callCount), self, e.address(),
            static_cast<int>(e.reason().size()), e.reason().data());
        std::fflush(stderr);
        return 1;
    }

    if (!isCreated) {
        return 1;
    }

    const bool active = (action != kActionIdle);
    static uint64_t activeStreak = 0;
    if (active) {
        ++activeStreak;
    } else {
        activeStreak = 0;
    }
    // Log every call while a transition is actually in progress (bounded if things work; if
    // they don't, still throttle after a while to avoid runaway output from a stuck transition).
    // Opt-in (NSMBW_LOG_WIPE): six unbuffered stderr lines per frame of every wipe made scene
    // transitions visibly slower once the wipe itself worked (2026-09-23).
    static const bool logWipe = AURORA_ENV("NSMBW_LOG_WIPE") != nullptr;
    const bool shouldLog = logWipe && active && (activeStreak <= 200 || (activeStreak % 60) == 0);

    if (!active) {
        return 1;
    }

    if (shouldLog) {
        char label[32];
        std::snprintf(label, sizeof(label), "call #%llu BEFORE", static_cast<unsigned long long>(callCount));
        LogWipeCircleState(label, self);
    }

    // (this->*Proc_tbl[mAction])() - OPEN_SETUP=0, ANIME_END_CHECK=1, CLOSE_SETUP=2 (IDLE=3
    // already excluded by the `active` check above).
    switch (action) {
        case 0: InvokeWithThis(kOpenSetupAddr, self); break;
        case 1: InvokeWithThis(kAnimeEndCheckAddr, self); break;
        case 2: InvokeWithThis(kCloseSetupAddr, self); break;
        default:
            std::fprintf(stderr, "[nsmbw][wipecircle] call #%llu unexpected mAction=%u (not 0/1/2), skipping dispatch\n",
                         static_cast<unsigned long long>(callCount), action);
            std::fflush(stderr);
            break;
    }

    const uint32_t mLytBase = self + kMLytOffset;
    InvokeWithThis(kAnimePlayAddr, mLytBase);

    // mLyt.calc() - virtual; resolve through the real vtable instead of guessing its address.
    try {
        const uint32_t vtablePtr = Memory::Read32(mLytBase + kMLytVtableOffset);
        const uint32_t lytCalcAddr = Memory::Read32(vtablePtr + kLytCalcVtableSlot);
        InvokeWithThis(lytCalcAddr, mLytBase);
    } catch (const Memory::AccessViolation& e) {
        std::fprintf(stderr,
            "[nsmbw][wipecircle] call #%llu CAUGHT AccessViolation resolving mLyt.calc() vtable: addr=0x%08X reason=%.*s\n",
            static_cast<unsigned long long>(callCount), e.address(),
            static_cast<int>(e.reason().size()), e.reason().data());
        std::fflush(stderr);
    }

    if (shouldLog) {
        char label[32];
        std::snprintf(label, sizeof(label), "call #%llu AFTER ", static_cast<unsigned long long>(callCount));
        LogWipeCircleState(label, self);
    }

    return 1;
}

PPC_NATIVE_OVERRIDE(8001B580, WipeCircleCalc_Diag_8001B580, int32_t, (uint32_t self), (self));
