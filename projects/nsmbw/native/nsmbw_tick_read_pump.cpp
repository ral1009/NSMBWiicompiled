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
#include <dolphin/pad.h>
#include <cstring>
#include <cstdlib>

// nsmbw_native has no SDL3 include path wired up, so these are the SDL3 scancode values
// directly (stable USB-HID-based values, unchanged across SDL2/3): X=27, Z=29, Up=82,
// Down=81, Left=80, Right=79, Return=40.
namespace {
constexpr int32_t kScancodeX = 27;
constexpr int32_t kScancodeZ = 29;
constexpr int32_t kScancodeUp = 82;
constexpr int32_t kScancodeDown = 81;
constexpr int32_t kScancodeLeft = 80;
constexpr int32_t kScancodeRight = 79;
constexpr int32_t kScancodeReturn = 40;

// Real WPAD.h digital button bits (NSMBW-Decomp include/lib/revolution/WPAD/WPAD.h) - the
// format EGG::CoreController::downTrigger's mask parameter and internal state use. D-pad
// bits happen to already match dolphin/PAD's own D-pad bits (1/2/4/8), so those don't need
// remapping; only the face buttons differ (PAD's 0x0100-range vs WPAD's 0x0100..0x0800 range).
constexpr uint32_t kWpadButtonA = 1u << 11; // 0x800
constexpr uint32_t kWpadButtonB = 1u << 10; // 0x400
constexpr uint32_t kWpadButton1 = 1u << 9;  // 0x200
constexpr uint32_t kWpadButton2 = 1u << 8;  // 0x100
} // namespace

// aurora_update() (which pumps SDL's event queue, the only thing that refreshes
// SDL_GetKeyboardState's snapshot) is called exactly once in the whole NSMBW runtime -
// during nsmbw_product.cpp's initial 120-frame "clearing" loop, before the guest entry
// point even starts. Nothing calls it again afterward, so real keyboard state was frozen
// from that point on for the rest of the run, regardless of binding or focus.
// SDL_PumpEvents is declared here directly (matching its real, stable extern "C" ABI)
// since nsmbw_native has no SDL3 include path wired up.
extern "C" void SDL_PumpEvents();
// Set by nsmbw_create_next_scene_diag.cpp on every successful scene transition.
extern "C" uint32_t g_nsmbwCurrentSceneProfile;

namespace {
bool g_padInited = false;
uint32_t g_lastButtons = 0;

// Accumulates real "just pressed" WPAD-format bits, one bit set per real key transition
// (PAD-format released->held edge this tick, translated to the matching WPAD bit).
// EGG::CoreController::downTrigger (see the override below) consumes bits out of this on
// each call rather than this file clearing it itself, so a real press is never missed
// even if this tick-pump hook and the game's own per-frame state-machine check don't land
// on exactly the same frame.
uint32_t g_wpadTriggerPending = 0;

void NsmbwDiagWatchWithRealInput() {
    if (!g_padInited) {
        PADInit();

        // PADRead's very first call lazily loads keyboard_bindings.dat from the user
        // config path and overwrites g_keyboardBindings wholesale if that file exists -
        // confirmed the hard way: an earlier saved file was silently clobbering the
        // bindings set below, because this diagnostic's own first PADRead call was the
        // one triggering that lazy load, AFTER the bindings were set. Forcing the load
        // here, before setting anything, so it can't stomp on this.
        PADStatus warmup[PAD_CHANMAX]{};
        PADRead(warmup);

        // Keyboard bindings are off by default (g_keyboardBindings[port].m_mappingsSet
        // starts false) - confirmed by reading aurora-main/lib/dolphin/pad/pad.cpp.
        // Nothing in NSMBW ever calls PADSetKeyboardActive since it never touches the
        // GC PAD system at all, so real key presses were silently discarded until now.
        PADSetKeyboardActive(0, TRUE);
        // g_defaultKeys (aurora-main/lib/dolphin/pad/pad.cpp) binds every button to
        // PAD_KEY_INVALID - there is no built-in default, it's meant to be configured
        // through a settings UI that saves to keyboard_bindings.dat. Binding a few keys
        // explicitly here for the test. Return is bound to WPAD "2" (via PAD_BUTTON_START)
        // since NSMBW's own boot flow already needs A and 2 specifically.
        PADSetKeyButtonBinding(0, {kScancodeX, PAD_BUTTON_A});
        PADSetKeyButtonBinding(0, {kScancodeZ, PAD_BUTTON_B});
        PADSetKeyButtonBinding(0, {kScancodeUp, PAD_BUTTON_UP});
        PADSetKeyButtonBinding(0, {kScancodeDown, PAD_BUTTON_DOWN});
        PADSetKeyButtonBinding(0, {kScancodeLeft, PAD_BUTTON_LEFT});
        PADSetKeyButtonBinding(0, {kScancodeRight, PAD_BUTTON_RIGHT});
        PADSetKeyButtonBinding(0, {kScancodeReturn, PAD_BUTTON_START});

        g_padInited = true;
    }

    SDL_PumpEvents();
    PADStatus statuses[PAD_CHANMAX]{};
    PADRead(statuses);

    static int heartbeatCount = 0;
    if ((++heartbeatCount % 30) == 0) {
        std::printf("[nsmbw][pad] heartbeat: err=%d button=0x%04X PADCount=%u\n",
            statuses[0].err, statuses[0].button, PADCount());

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

    const uint32_t buttons = statuses[0].button;
    const uint32_t newlyPressed = buttons & ~g_lastButtons;

    if (newlyPressed != 0) {
        uint32_t wpadBits = newlyPressed & (PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT);
        if (newlyPressed & PAD_BUTTON_A) wpadBits |= kWpadButtonA;
        if (newlyPressed & PAD_BUTTON_B) wpadBits |= kWpadButtonB;
        if (newlyPressed & PAD_BUTTON_START) wpadBits |= kWpadButton2;
        g_wpadTriggerPending |= wpadBits;
        std::fprintf(stderr, "[nsmbw][pad] real press: PAD=0x%04X -> WPAD trigger bits=0x%04X\n", newlyPressed, wpadBits);
        std::fflush(stderr);
    }

    // SELF-TEST ONLY, opt-in via NSMBW_AUTO_PRESS_SELFTEST: synthesize an "A just pressed"
    // WPAD trigger once a second for the first 20s of boot, so an unattended/automated run
    // can get itself past every real button-gated wait (WiiStrap/ControllerInfo skip, then
    // the save-data-created ButtonInputWait screen) without a physical key press. This goes
    // through the exact same g_wpadTriggerPending consumption path as a real press - it's
    // not a separate/parallel code path, so it doesn't change what's being tested.
    //
    // Was unconditional until it was caught racing ahead of a real person's own key presses:
    // one synthetic confirm every second is enough to auto-advance past a "press confirm"
    // screen (and cut short whatever fade/wipe transition is supposed to play after it)
    // before a human watching the screen ever gets a chance to press anything themselves.
    // Gated off by default now that real keyboard input (bound just above) is confirmed
    // working, so a live tester's own presses are the only thing driving input.
    if (std::getenv("NSMBW_AUTO_PRESS_SELFTEST") != nullptr) {
        // NSMBW_AUTO_PRESS_STOP_SCENE: stop synthesizing presses once this scene profile is
        // active, so an unattended run can park on a specific screen (e.g. "5" holds on STAGE, the
        // title screen) instead of confirming its way straight through it a second later.
        static const long stopScene = [] {
            const char* v = std::getenv("NSMBW_AUTO_PRESS_STOP_SCENE");
            return v ? static_cast<long>(std::strtoul(v, nullptr, 10)) : -1L;
        }();
        static int diagAutoPressTick = 0;
        ++diagAutoPressTick;
        const bool parked = stopScene >= 0 && g_nsmbwCurrentSceneProfile == static_cast<uint32_t>(stopScene);
        if (parked) {
            // A press synthesized during the previous scene stays latched until something consumes
            // it, and the target scene would consume it on its first frame and move on - drop the
            // synthetic bit so parking actually holds.
            g_wpadTriggerPending &= ~kWpadButtonA;
        }
        if (!parked && diagAutoPressTick <= 1200 && (diagAutoPressTick % 60) == 0) {
            g_wpadTriggerPending |= kWpadButtonA;
            std::fprintf(stderr, "[nsmbw][diag] self-test: synthesized A press (tick=%d)\n", diagAutoPressTick);
            std::fflush(stderr);
        }
    }

    g_lastButtons = buttons;
}
} // namespace

// DIAGNOSTIC (temporary): defined in nsmbw_g3d_scnobj_diag.cpp; per-tick scene-object matrix dump.
extern "C" void NsmbwDumpScnObjs();

extern "C" void nsmbw_tick_read_pump_801be010(CpuContext* ctx)
{
    VI_HLE_PollRetrace(ctx);
    NsmbwDiagWatchWithRealInput();
    NsmbwDumpScnObjs();
    ctx->gpr[3] = ::Memory::Read32(0x8042AB4Cu);
}

// The real, ground-truth EGG::CoreController::downTrigger(ulong) - NOT a vtable-position
// guess. Found by resolving dScBoot_c::executeState_ButtonInputWait's own address through
// generated_nsmbw/guest_symbol_table.cpp (the project's real function-address table,
// generated from the .dol/.rel map - the same one the crash reporter's DiagResolveSymbol
// uses) and disassembling straight from its real call site:
//   li r4, 0x900                    ; WPAD_BUTTON_A|WPAD_BUTTON_2
//   lwz r3, -0x5238(r13)            ; r3 = mPad::g_currentCore (0x8042A748)
//   lwz r12, 0(r3); lwz r12,0x20(r12) ; vtable slot 8 (offset 0x20), NOT slot 6 as first guessed
//   bctrl
// downTrigger itself (0x80017CE0) is trivial: `return (mask & *(this+0x1C)) != 0`. The real
// per-frame update of *(this+0x1C) goes through extension-type-dependent branches and
// analog-stick-to-dpad synthesis (EGG::CoreController's internal per-frame method, vtable
// slot 12) that were never fed real WPAD hardware samples in this recompilation - rebuilding
// that whole pipeline is the "full EGG architecture reconstruction" explicitly out of scope.
//
// SAFETY NOTE: 0x80017CE0 has no static (bl) caller anywhere in the translated code - every
// call reaches it indirectly through a vtable, and grepping can't rule out some OTHER class's
// unrelated method having been identical-code-folded onto this same compiled body (the same
// linker behavior that scattered CoreController's own down/up/downAll methods across
// unrelated-looking addresses - see the vtable dump this session's earlier investigation
// produced). So this only substitutes real captured input when `thisPtr` is the ONE object
// we've confirmed this address is legitimately called for (mPad::g_currentCore) - any other
// caller falls through to the exact original computation (`(mask & *(this+0x1C)) != 0`),
// so a coincidental other caller of this shared address keeps working exactly as before.
extern "C" uint32_t DownTrigger_80017CE0_HLE(uint32_t thisPtr, uint32_t mask)
{
    uint32_t field = 0;
    Memory::TryRead32(thisPtr + 0x1Cu, field);

    uint32_t currentCore = 0;
    Memory::TryRead32(0x8042A748u, currentCore);
    if (thisPtr == currentCore && currentCore != 0) {
        const uint32_t matched = g_wpadTriggerPending & mask;
        g_wpadTriggerPending &= ~matched;
        field |= matched; // additive: never destroys whatever the real field already had
        if (matched != 0) {
            std::fprintf(stderr, "[nsmbw][pad] downTrigger(mask=0x%08X) -> true (matched=0x%08X)\n", mask, matched);
            std::fflush(stderr);
        }
    }
    return ((mask & field) != 0) ? 1u : 0u;
}

PPC_NATIVE_OVERRIDE(80017CE0, DownTrigger_80017CE0_HLE, uint32_t, (uint32_t thisPtr, uint32_t mask), (thisPtr, mask));

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
