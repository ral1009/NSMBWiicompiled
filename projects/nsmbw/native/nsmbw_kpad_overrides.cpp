// Keyboard -> Wii Remote input for NSMBW, delivered through KPADRead.
//
// Why here and not at the EGG::CoreController level (the previous approach, a native override
// of downTrigger that OR'd "just pressed" bits into the controller object): the game's own
// controller class samples KPAD every frame - func_802BD0D0 (EGG::CoreController::beginFrame,
// virtual, no static callers) does `KPADReadEx(mNum, this+0x18, 16, &err)`, so this+0x18 is
// KPADStatus[0] (hold @+0x18, trig @+0x1C, release @+0x20 - which is exactly the field the old
// downTrigger hack poked). Everything the game derives from input - held state for walking,
// press edges for menus, release edges, the sideways-remote d-pad mapping, button repeat - is
// computed by the game from that one struct. Feeding real samples here makes all of it work
// through the game's own code; poking a single edge field could never give held state.
//
// Addresses (translated bodies in generated_nsmbw/build_shards):
//   0x801ED4E0  KPADRead(chan, samples, num)            = `r6=0; r7=0; b 0x801ED500`
//   0x801ED4F0  KPADReadEx(chan, samples, num, s32*err) = `r7=1;       b 0x801ED500`
//   0x801ED500  the shared internal: (chan, samples, num, err, isEx). Overridden here so both
//               entry points are covered. Returns the number of samples written.
// KPADStatus layout: NSMBW-Decomp include/lib/revolution/KPAD/KPAD.h (0x84 bytes: hold/trig/
// release at +0/+4/+8, acc at +0xC, dev_type u8 at +0x5C, wpad_err s8 at +0x5D).
// WPAD button bits: include/lib/revolution/WPAD/WPAD.h.
//
// Key map (the remote is held sideways in NSMBW, so the game reads d-pad UP as "left" etc.;
// the arrow keys are rotated here so they mean what they say on screen):
//   Z -> 2 (jump)     X -> 1 (run/dash)     Enter -> A (menus, dialogs)
//   Left -> WPAD UP   Right -> WPAD DOWN    Up -> WPAD RIGHT    Down -> WPAD LEFT
// B, +, -, HOME and the pointer are not bound yet.
//
// The keyboard is read through aurora's PAD keyboard-binding layer (dolphin/pad.h) with the
// GC PAD bits used only as an intermediate; SDL_PumpEvents is what actually refreshes SDL's
// key state and nothing else in the NSMBW runtime calls it after boot (see the tick pump).
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include <dolphin/pad.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void SDL_PumpEvents();
extern "C" const bool* SDL_GetKeyboardState(int* numkeys);
// Set by nsmbw_tick_read_pump.cpp's self-test (NSMBW_AUTO_PRESS_SELFTEST): a synthetic A press
// is delivered as one frame of "held" through the same path as a real key.
extern "C" uint32_t g_nsmbwSelfTestPressBits;
uint32_t g_nsmbwSelfTestPressBits = 0;

namespace {
// SDL3 scancodes (USB HID usage ids; nsmbw_native has no SDL include path).
constexpr int32_t kScancodeX = 27;
constexpr int32_t kScancodeZ = 29;
constexpr int32_t kScancodeReturn = 40;
constexpr int32_t kScancodeRight = 79;
constexpr int32_t kScancodeLeft = 80;
constexpr int32_t kScancodeDown = 81;
constexpr int32_t kScancodeUp = 82;

constexpr uint32_t kWpadLeft = 1u << 0;
constexpr uint32_t kWpadRight = 1u << 1;
constexpr uint32_t kWpadDown = 1u << 2;
constexpr uint32_t kWpadUp = 1u << 3;
constexpr uint32_t kWpad2 = 1u << 8;
constexpr uint32_t kWpad1 = 1u << 9;
constexpr uint32_t kWpadA = 1u << 11;

constexpr uint32_t kKpadStatusSize = 0x84u;
constexpr uint32_t kViRetraceCountAddr = 0x8042AB4Cu; // same accessor the tick pump serves

bool g_padInited = false;
uint32_t g_frameHold = 0, g_frameTrig = 0, g_frameRelease = 0;
uint32_t g_prevHold = 0;
uint32_t g_lastSampledTick = 0xFFFFFFFFu;

void InitKeyboardOnce() {
    if (g_padInited) return;
    PADInit();
    // PADRead's first call lazily loads keyboard_bindings.dat and would overwrite anything set
    // before it, so force that load first (learned the hard way in the tick pump).
    PADStatus warmup[PAD_CHANMAX]{};
    PADRead(warmup);
    PADSetKeyboardActive(0, TRUE);
    // GC PAD bits are only an intermediate; TranslatePadToWpad below is the real mapping.
    PADSetKeyButtonBinding(0, {kScancodeZ, PAD_BUTTON_Y});      // -> WPAD 2
    PADSetKeyButtonBinding(0, {kScancodeX, PAD_BUTTON_X});      // -> WPAD 1
    PADSetKeyButtonBinding(0, {kScancodeReturn, PAD_BUTTON_A}); // -> WPAD A
    PADSetKeyButtonBinding(0, {kScancodeUp, PAD_BUTTON_UP});
    PADSetKeyButtonBinding(0, {kScancodeDown, PAD_BUTTON_DOWN});
    PADSetKeyButtonBinding(0, {kScancodeLeft, PAD_BUTTON_LEFT});
    PADSetKeyButtonBinding(0, {kScancodeRight, PAD_BUTTON_RIGHT});
    g_padInited = true;
}

uint32_t TranslatePadToWpad(uint32_t pad) {
    uint32_t w = 0;
    if (pad & PAD_BUTTON_Y) w |= kWpad2;
    if (pad & PAD_BUTTON_X) w |= kWpad1;
    if (pad & PAD_BUTTON_A) w |= kWpadA;
    // Sideways remote: the game maps physical d-pad UP to screen-left, DOWN to screen-right,
    // RIGHT to up and LEFT to down.
    if (pad & PAD_BUTTON_LEFT) w |= kWpadUp;
    if (pad & PAD_BUTTON_RIGHT) w |= kWpadDown;
    if (pad & PAD_BUTTON_UP) w |= kWpadRight;
    if (pad & PAD_BUTTON_DOWN) w |= kWpadLeft;
    return w;
}

// One keyboard snapshot per VI frame: several KPADRead calls in the same frame must see the
// same hold/trig/release, or a press edge would be consumed by whichever caller read first.
void SampleKeyboardForFrame() {
    uint32_t tick = 0;
    Memory::TryRead32(kViRetraceCountAddr, tick);
    if (tick == g_lastSampledTick) return;
    g_lastSampledTick = tick;

    InitKeyboardOnce();
    SDL_PumpEvents();
    PADStatus statuses[PAD_CHANMAX]{};
    PADRead(statuses);

    uint32_t hold = TranslatePadToWpad(statuses[0].button);
    hold |= g_nsmbwSelfTestPressBits;
    g_nsmbwSelfTestPressBits = 0;

    g_frameHold = hold;
    g_frameTrig = hold & ~g_prevHold;
    g_frameRelease = g_prevHold & ~hold;
    g_prevHold = hold;

    if (g_frameTrig != 0 || g_frameRelease != 0) {
        static const bool log = std::getenv("NSMBW_LOG_INPUT") != nullptr;
        if (log) {
            std::fprintf(stderr, "[nsmbw][kpad] tick=%u hold=0x%04X trig=0x%04X release=0x%04X\n",
                         tick, g_frameHold, g_frameTrig, g_frameRelease);
            int numKeys = 0;
            const bool* kb = SDL_GetKeyboardState(&numKeys);
            std::fprintf(stderr, "[nsmbw][kpad]   pad=0x%04X scancodes down:", statuses[0].button);
            for (int i = 0; kb && i < numKeys; ++i) if (kb[i]) std::fprintf(stderr, " %d", i);
            std::fprintf(stderr, "\n");
            std::fflush(stderr);
        }
    }
}

void WriteZeroedStatus(uint32_t addr, int8_t wpadErr) {
    for (uint32_t off = 0; off < kKpadStatusSize; off += 4u) Memory::Write32(addr + off, 0);
    Memory::Write8(addr + 0x5Du, static_cast<uint8_t>(wpadErr));
}
} // namespace

extern "C" int32_t NsmbwKpadRead_801ED500(uint32_t chan, uint32_t samples, uint32_t num, uint32_t errPtr, uint32_t isEx)
{
    (void)isEx;
    try {
        if (chan != 0 || samples == 0 || num == 0) {
            // Only channel 0 is a connected remote (nsmbw_wpad_overrides.cpp seeds it).
            if (samples != 0 && num != 0) WriteZeroedStatus(samples, -1 /* WPAD_ERR_NO_CONTROLLER */);
            if (errPtr != 0) Memory::Write32(errPtr, static_cast<uint32_t>(-1));
            return 0;
        }
        SampleKeyboardForFrame();
        WriteZeroedStatus(samples, 0 /* WPAD_ERR_OK */);
        Memory::Write32(samples + 0x0u, g_frameHold);
        Memory::Write32(samples + 0x4u, g_frameTrig);
        Memory::Write32(samples + 0x8u, g_frameRelease);
        // dev_type 0 = core remote, no extension; dpd_valid_fg 0 = no pointer data.
        if (errPtr != 0) Memory::Write32(errPtr, 0);
        return 1;
    } catch (const Memory::AccessViolation&) {
        return 0;
    }
}
PPC_NATIVE_OVERRIDE(801ED500, NsmbwKpadRead_801ED500, int32_t,
                    (uint32_t chan, uint32_t samples, uint32_t num, uint32_t errPtr, uint32_t isEx),
                    (chan, samples, num, errPtr, isEx));
