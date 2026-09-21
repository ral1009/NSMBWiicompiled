// Native override for 0x801BE010, a one-line leaf that just returns the guest global at
// 0x8042AB4C (kViRetraceCountAddr - r13-0x4E34, matching this project's sda_base) - i.e. it's
// NSMBW's "read the current VI retrace tick" accessor.
//
// Found while chasing func_801AF710's boot stall, right after the disk-ID-check fix
// (nsmbw_disk_id_check_sync.cpp) cleared the previous blocker: func_801AF710 calls this twice and
// spins (`bl 0x801be010; ...; bl 0x801be010; subf r0,r24,r3; cmpwi r0,1; blt back`) until the
// delta between two reads is >=1 - a "wait for at least one VI retrace tick to pass" idiom. Same
// root cause as the SelectThread bug this session already fixed: the retrace count only actually
// advances from inside SelectThread's own idle loop (which polls VI_HLE_PollRetrace), and this is
// a tight spin with no yield point, so it never reaches that idle loop no matter how long it runs.
//
// Fix: pump one VI_HLE_PollRetrace() call every time this specific accessor is read, so time
// visibly advances for whoever's polling it - the general fix for this whole class of "wait for
// the tick to move" loop (not particular to func_801AF710; anything else reading ticks through
// this same accessor benefits too), consistent with the SelectThread fix's own approach of running
// VI's real retrace-advance logic rather than inventing a fake counter increment.
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include <cstdio>
#include <cstdlib>

// Keyboard input used to be polled here and injected as "just pressed" edges into
// EGG::CoreController::downTrigger. Since 2026-09-20 real samples go through KPADRead instead
// (nsmbw_kpad_overrides.cpp), which also gives held/release state; this file only pumps VI,
// prints the boot-time fader diagnostics, and drives the opt-in self-test presses.
extern "C" uint32_t g_nsmbwCurrentSceneProfile;
extern "C" uint32_t g_nsmbwSelfTestPressBits;
// Published for aurora-side diagnostics (see aurora-main/lib/internal.hpp).
extern "C" uint32_t g_nsmbwCurrentViTick;
uint32_t g_nsmbwCurrentViTick = 0;

namespace {
constexpr uint32_t kWpadButtonA = 1u << 11;
// Sideways-remote "screen right" is the remote's DOWN bit (see nsmbw_kpad_overrides.cpp).
constexpr uint32_t kWpadScreenRight = 1u << 2;
constexpr uint32_t kWpadButton2 = 1u << 8;

// DIAGNOSTIC (temporary): NSMBW_WATCH_WMOBJ. The world map's teardown (func_808DC2D0, after the
// wipe-out into a level) deletes the 56-byte object at *(0x8099FDFC) through its vtable and
// crashes because the vtable word reads 0, although its constructor (0x80103BD0) wrote
// 0x80321BD0 there. This samples that word once per VI tick and reports the tick where it
// changes, plus the scene, to bracket what overwrote it.
void NsmbwWatchWmObject() {
    static const bool enabled = std::getenv("NSMBW_WATCH_WMOBJ") != nullptr;
    if (!enabled) return;
    static uint32_t lastObj = 0, lastVtable = 0xFFFFFFFFu;
    uint32_t obj = 0, vtable = 0xFFFFFFFFu;
    Memory::TryRead32(0x8099FDFCu, obj);
    if (obj != 0) Memory::TryRead32(obj, vtable);
    if (obj != lastObj || vtable != lastVtable) {
        uint32_t tick = 0;
        Memory::TryRead32(0x8042AB4Cu, tick);
        std::fprintf(stderr, "[nsmbw][wmobj] tick=%u scene=%u obj=0x%08X vtable=0x%08X (was obj=0x%08X vtable=0x%08X)\n",
                     tick, g_nsmbwCurrentSceneProfile, obj, vtable, lastObj, lastVtable);
        std::fflush(stderr);
        lastObj = obj; lastVtable = vtable;
    }
}

void NsmbwDiagWatch() {
    NsmbwWatchWmObject();

    static int heartbeatCount = 0;
    if ((++heartbeatCount % 30) == 0) {

        // dScCrsin_c::m_isDispOff (0x8042A490) - DISPROVEN as the black-screen cause:
        // exhaustively scanned main.dol + all 4 RELs (fully relocated) for every
        // r13-relative load AND every lis+{addi,ori} absolute-address construction of
        // this exact address - zero reads found anywhere, only the 3 known writers
        // (dScRestartCrsin_c::startTitle x1, reStartPeachCastle x1, and a third
        // dInfo_c::startGame-calling helper at 0x800BB8D0). It's write-only in this
        // build regardless of what its doc comment says. Kept here only as a settled
        // negative result, not a live lead.
        std::printf("[nsmbw][diag] dScCrsin_c::m_isDispOff=%u (write-only, not the cause)\n",
            Memory::Read8(0x8042A490u));

        // Real lead: dFader_c::calc() (0x800B0B00, disassembled offline) reads its
        // static current-fader pointer from r13-0x5260 = 0x8042A720, then dispatches
        // to that object's vtable slot 7 (offset 0x1C). mFaderBase_c::getStatus()
        // (0x8016DE50: `lwz r3,4(r3); blr`) confirms mStatus lives at object+4.
        // EStatus: OPAQUE=0 (screen fully blacked out), HIDDEN=1, FADE_IN=2, FADE_OUT=3
        // (NSMBW-Decomp include/game/mLib/m_fader_base.hpp). startTitle calls
        // dFader_c::setFader(FADER_FADE) right before the m_isDispOff writes above -
        // if the fader is stuck at OPAQUE, that alone produces a pure black screen
        // regardless of what's actually being rendered underneath.
        uint32_t faderPtr = 0;
        Memory::TryRead32(0x8042A720u, faderPtr);
        uint32_t faderStatus = 0xFFFFFFFFu;
        if (faderPtr != 0) {
            Memory::TryRead32(faderPtr + 4u, faderStatus);
        }
        std::printf("[nsmbw][diag] dFader_c::mFader=0x%08X mStatus=%u (0=OPAQUE 1=HIDDEN 2=FADE_IN 3=FADE_OUT)\n",
            faderPtr, faderStatus);

        // dScene_c::preExecute() (0x800E1CD0, disassembled offline, matches
        // NSMBW-Decomp source/dol/bases/d_scene.cpp line-for-line) reads m_isAutoFadeIn
        // via `lbz r0, -0x6F34(r13)` = address 0x80428A4C. This is the ONE global that
        // decides whether the "fader OPAQUE" auto-fade-in branch can even be taken -
        // if it's false, dScCrsin_c must be manually calling startFadeIn itself
        // (matching the doc'd "unless m_isAutoFadeIn is false, in which case the fade-in
        // must be started manually" design) and something in that manual path is what's
        // actually stuck. If it's true, preExecute's auto-fade-in check should already
        // be firing every frame while OPAQUE - meaning the REAL block is upstream of it,
        // in the `isProcControlFlag(ROOT_DISABLE_EXECUTE)` early-return this same
        // function has (skips the fade-in check entirely while the scene's child
        // actors/archives haven't finished being created) - a full pipeline hang, not a
        // fader-specific one.
        std::printf("[nsmbw][diag] dScene_c::m_isAutoFadeIn=%u\n", Memory::Read8(0x80428A4Cu));
    }

    // SELF-TEST ONLY, opt-in via NSMBW_AUTO_PRESS_SELFTEST: hand KPADRead one frame of "A held"
    // once a second for the first NSMBW_AUTO_PRESS_TICKS (default 1200) ticks, so an unattended
    // run gets past every button-gated wait (strap / controller-info skip, the save-data-created
    // dialog, the title) without a physical key. It goes through the same KPAD sample path as a
    // real key, so it does not test a separate code path. NSMBW_AUTO_PRESS_STOP_SCENE=<profile>
    // stops the presses once that scene is active so a run can park on it.
    if (std::getenv("NSMBW_AUTO_PRESS_SELFTEST") != nullptr) {
        static const long stopScene = [] {
            const char* v = std::getenv("NSMBW_AUTO_PRESS_STOP_SCENE");
            return v ? static_cast<long>(std::strtoul(v, nullptr, 10)) : -1L;
        }();
        static const int pressWindowTicks = [] {
            const char* v = std::getenv("NSMBW_AUTO_PRESS_TICKS");
            return v ? static_cast<int>(std::strtol(v, nullptr, 10)) : 1200;
        }();
        static int diagAutoPressTick = 0;
        ++diagAutoPressTick;
        const bool parked = stopScene >= 0 && g_nsmbwCurrentSceneProfile == static_cast<uint32_t>(stopScene);
        // On the world map a resumed save starts Mario on the start node, one step left of 1-1,
        // so tap screen-right early in each map visit and hold the periodic A presses off for the
        // first NSMBW_AUTO_PRESS_MAP_RIGHT_TICKS (default 90) ticks; A alone never leaves the node.
        static const int mapRightTicks = [] {
            const char* v = std::getenv("NSMBW_AUTO_PRESS_MAP_RIGHT_TICKS");
            return v ? static_cast<int>(std::strtol(v, nullptr, 10)) : 90;
        }();
        static int mapTicks = 0;
        const bool onMap = g_nsmbwCurrentSceneProfile == 3u;
        mapTicks = onMap ? mapTicks + 1 : 0;
        // A short tap (ticks 30..33 of the map visit): holding the direction for longer put the
        // map into its free-look mode ("Back to Mario" overlay, lvl8_t150.png) instead of moving.
        const bool tapRight = onMap && mapTicks >= 30 && mapTicks < 34;
        const bool walkingRight = onMap && mapTicks <= mapRightTicks;
        if (tapRight) {
            g_nsmbwSelfTestPressBits |= kWpadScreenRight;
        } else {
            g_nsmbwSelfTestPressBits &= ~kWpadScreenRight;
        }
        // On the map A opens free-look ("Back to Mario" overlay); 2 is what enters a level.
        const uint32_t pressBit = onMap ? kWpadButton2 : kWpadButtonA;
        if (parked || walkingRight) {
            g_nsmbwSelfTestPressBits &= ~(kWpadButtonA | kWpadButton2);
        } else if (diagAutoPressTick <= pressWindowTicks && (diagAutoPressTick % 60) == 0) {
            g_nsmbwSelfTestPressBits |= pressBit;
            std::fprintf(stderr, "[nsmbw][diag] self-test: synthesized %s press (tick=%d)\n", onMap ? "2" : "A", diagAutoPressTick);
            std::fflush(stderr);
        }
    }
}
} // namespace

extern "C" void NsmbwDumpScnObjs();

extern "C" void nsmbw_tick_read_pump_801be010(CpuContext* ctx)
{
    VI_HLE_PollRetrace(ctx);
    NsmbwDiagWatch();
    NsmbwDumpScnObjs();
    ctx->gpr[3] = ::Memory::Read32(0x8042AB4Cu);
    g_nsmbwCurrentViTick = ctx->gpr[3];
}

PPC_NATIVE_OVERRIDE_VOID(801BE010, nsmbw_tick_read_pump_801be010, (CpuContext* ctx), (ctx));

// dFader_c::mFader (0x8042A720, r13-0x5260) is confirmed live-stuck at mStatus=0
// (OPAQUE - "the screen is completely blacked out", NSMBW-Decomp
// include/game/mLib/m_fader_base.hpp) for the entire run following the WPAD-input fix.
// dFader_c::startFadeIn (0x800B0A20) is the real function that should transition it out
// of OPAQUE - disassembled offline, its full logic is:
//   bit 5 (MSB=0 numbering, value 0x04000000) of the word at r13-0x5720 (0x8042A260)
//   selects a forced 1-frame duration vs the caller's requested duration, then
//   unconditionally calls setFrame(fader, duration) [0x8016DE20] followed by the
//   fader's own vtable slot 5 (offset 0x14) = mFaderBase_c::fadeIn().
// It has 10 real call sites across the translated code (confirmed via grep), so it is
// NOT dead code overall - the question is whether any of those sites actually execute
// for THIS specific stuck fader. Reimplementing exactly (not guessing) so this can be
// observed live without altering behavior for any caller.
extern "C" uint32_t StartFadeIn_800B0A20_Diag(uint32_t duration)
{
    std::fprintf(stderr, "[nsmbw][diag] dFader_c::startFadeIn(duration=%u) called\n", duration);
    std::fflush(stderr);

    uint32_t skipFlagWord = 0;
    Memory::TryRead32(0x8042A260u, skipFlagWord);
    const bool forceInstant = (skipFlagWord & 0x04000000u) != 0;

    uint32_t faderPtr = 0;
    Memory::TryRead32(0x8042A720u, faderPtr);
    if (faderPtr == 0) {
        std::fprintf(stderr, "[nsmbw][diag] dFader_c::startFadeIn: mFader is null, nothing to do\n");
        std::fflush(stderr);
        return 0;
    }

    auto& cpu = GetPersistentCpuContext();
    cpu.gpr[3] = faderPtr;
    cpu.gpr[4] = forceInstant ? 1u : duration;
    InvokeIndirectCpu(0x8016DE20u, &cpu); // mFaderBase_c::setFrame(duration)

    uint32_t vtable = 0;
    Memory::TryRead32(faderPtr, vtable);
    uint32_t fadeInTarget = 0;
    if (vtable != 0) {
        Memory::TryRead32(vtable + 0x14u, fadeInTarget);
    }
    uint32_t result = 0;
    if (fadeInTarget != 0) {
        cpu.gpr[3] = faderPtr;
        InvokeIndirectCpu(fadeInTarget, &cpu);
        result = cpu.gpr[3];
    }
    std::fprintf(stderr, "[nsmbw][diag] dFader_c::startFadeIn: fader=0x%08X forceInstant=%d fadeIn()->%u\n",
        faderPtr, forceInstant ? 1 : 0, result);
    std::fflush(stderr);
    return result;
}

PPC_NATIVE_OVERRIDE(800B0A20, StartFadeIn_800B0A20_Diag, uint32_t, (uint32_t duration), (duration));

// startFadeIn succeeded twice (fadeIn()->1) but mStatus stayed OPAQUE(0) afterward per
// the periodic dump above - contradiction. fadeIn() may only arm a pending transition
// that dFader_c::calc() (0x800B0B00, disassembled: `lwz r3,mFader; vtable[7](fader)`)
// is responsible for actually progressing every frame (typical for animated faders:
// fadeIn() flips an internal target, calc() advances mCurrFrame/mStatus toward it).
// If calc() is never being called at all, that would explain the stall regardless of
// fadeIn() reporting success. Reimplementing this trivial tail-call exactly so it can
// be observed without changing behavior for any caller.
extern "C" void Calc_800B0B00_Diag()
{
    static uint64_t callCount = 0;
    ++callCount;

    uint32_t faderPtr = 0;
    Memory::TryRead32(0x8042A720u, faderPtr);
    uint32_t statusBefore = 0xFFFFFFFFu;
    if (faderPtr != 0) {
        Memory::TryRead32(faderPtr + 4u, statusBefore);
    }

    if (faderPtr != 0) {
        uint32_t vtable = 0;
        Memory::TryRead32(faderPtr, vtable);
        uint32_t calcTarget = 0;
        if (vtable != 0) {
            Memory::TryRead32(vtable + 0x1Cu, calcTarget);
        }
        if (calcTarget != 0) {
            auto& cpu = GetPersistentCpuContext();
            cpu.gpr[3] = faderPtr;
            InvokeIndirectCpu(calcTarget, &cpu);
        }
    }

    if (callCount <= 20 || (callCount % 300) == 0) {
        uint32_t statusAfter = 0xFFFFFFFFu;
        if (faderPtr != 0) {
            Memory::TryRead32(faderPtr + 4u, statusAfter);
        }
        std::fprintf(stderr, "[nsmbw][diag] dFader_c::calc: call #%llu fader=0x%08X status %u -> %u\n",
            static_cast<unsigned long long>(callCount), faderPtr, statusBefore, statusAfter);
        std::fflush(stderr);
    }
}

PPC_NATIVE_OVERRIDE_VOID(800B0B00, Calc_800B0B00_Diag, (), ());
