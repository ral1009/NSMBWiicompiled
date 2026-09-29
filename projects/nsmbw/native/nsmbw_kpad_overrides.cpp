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
//   = -> +            - -> -                C -> shake (see below)
//   Left -> WPAD UP   Right -> WPAD DOWN    Up -> WPAD RIGHT    Down -> WPAD LEFT
// Gamepads go through the same PAD slots (remappable in the settings overlay, which labels them
// with these Wii names for NSMBW); B maps to WPAD B, R is a second shake, and the left stick
// drives the d-pad. HOME and the pointer are not bound yet.
//
// Shake: NSMBW's spin jump / propeller spin comes from the accelerometer, not a button. The
// detector is dGameKeyCore_c's helper at 0x800B62A0 (read from the translated body): it copies
// KPADStatus.acc from EGG::CoreController (+0x24..0x2C) each frame and sets mIsShaking when
// |acc.y - prev.y| >= 0.28 (SDA2 float at 0x8042C8A4) for 4 consecutive frames with z not the
// dominant delta, then holds a 5-frame cooldown (the player adds its own 10 frames). So instead
// of poking mIsShaking, a C press starts an 8-tick burst of synthetic acc.y values through the
// same field, and the game's own detector, cooldowns and demo/replay handling do the rest. The
// burst cycles 1.0, 0.5, 0.0, -0.5: any two samples up to three ticks apart differ by >= 0.5,
// so it still fires when a slow frame makes the game read only every 2nd or 3rd VI tick.
//
// Tilt: the tilt lifts, remote wire, remote door etc. read dGameKeyCore_c::getAccVerticalAngleX
// (0x800B5CA0), which returns the s16 at dGameKeyCore_c+0x8E. func_800B61F0 fills that each
// frame. For a remote with no extension (dGameKeyCore_c+8 == 0, which is what this override
// reports) it is 0x4000 * acc_vertical.y, where acc_vertical is KPADStatus+0x54 (Vec2), copied
// in by 0x800B5CB0 from CoreController+0x6C. (With an extension it instead eases toward
// 0x4000 * -acc.z.) 0x4000 is the SDA2 float 16384.0 at 0x8042C8A0, next to the shake
// threshold: 90 degrees in the game's angle units. The remote-tilt door (d_a_remo_door.cpp)
// opens at >= 0x2000.
// KPAD computes acc_vertical itself (0x801EB170): the smoothed unit vector
// (sqrt(acc.x^2 + acc.y^2), -acc.z) / |acc|. This override replaces the whole KPAD read, so that
// never runs here and the field was (0, 0) - the game saw a level remote. For a remote lying
// face-up (KPAD's reset rests acc at (0, -1, 0), 0x801EAB00) and tilted by theta, the vector is
// (cos theta, -sin theta); acc.z is eased toward the target (its sign: see Sign below) and the
// vector is written from it as (sqrt(1 - acc.z^2), -acc.z), so the raw acc and acc_vertical agree.
// Sign: tilt right = -acc.z = a POSITIVE game angle. Set by observation, not derivation: the
// developer tried it on a tilt platform (2026-09-28) with the opposite sign and A tilted it right,
// D left. The first version reasoned from KPAD's converter (0x801EB2A0: acc.z = +raw Y) plus the
// Wii Remote convention that raw Y is positive with the pointer end raised; that last step, taken
// from documentation rather than this game's code, is the part that was wrong for this grip.
// acc.z moves toward its target by at most kTiltStepPerTick, like a real remote turning: the shake
// detector ignores frames where z changes more than y, so an instant 0 -> 1 jump in z could
// swallow a shake made in the same frames.
//
// The keyboard is read through aurora's PAD keyboard-binding layer (dolphin/pad.h) with the
// GC PAD bits used only as an intermediate; SDL_PumpEvents is what actually refreshes SDL's
// key state and nothing else in the NSMBW runtime calls it after boot (see the tick pump).
#include "hle_stubs.h"
#include <aurora/env.hpp>
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include <dolphin/pad.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void SDL_PumpEvents();
extern "C" void NsmbwSkipBootScreensApply();
extern "C" uint32_t NsmbwControlsReadWpad(); // runtime/src/product/nsmbw_controls.cpp
extern "C" float NsmbwControlsReadTilt();     // same file; -1 (left) .. +1 (right)
extern "C" const bool* SDL_GetKeyboardState(int* numkeys);
// Set by nsmbw_tick_read_pump.cpp's self-test (NSMBW_AUTO_PRESS_SELFTEST): a synthetic A press
// is delivered as one frame of "held" through the same path as a real key.
extern "C" uint32_t g_nsmbwSelfTestPressBits;
uint32_t g_nsmbwSelfTestPressBits = 0;

namespace {
// SDL3 scancodes (USB HID usage ids; nsmbw_native has no SDL include path).
constexpr int32_t kScancodeC = 6;
constexpr int32_t kScancodeX = 27;
constexpr int32_t kScancodeZ = 29;
constexpr int32_t kScancodeMinus = 45;
constexpr int32_t kScancodeEquals = 46;
constexpr int32_t kScancodeReturn = 40;
constexpr int32_t kScancodeRight = 79;
constexpr int32_t kScancodeLeft = 80;
constexpr int32_t kScancodeDown = 81;
constexpr int32_t kScancodeUp = 82;

constexpr uint32_t kWpadLeft = 1u << 0;
constexpr uint32_t kWpadRight = 1u << 1;
constexpr uint32_t kWpadDown = 1u << 2;
constexpr uint32_t kWpadUp = 1u << 3;
constexpr uint32_t kWpadPlus = 1u << 4;
constexpr uint32_t kWpad2 = 1u << 8;
constexpr uint32_t kWpad1 = 1u << 9;
constexpr uint32_t kWpadB = 1u << 10;
constexpr uint32_t kWpadA = 1u << 11;
constexpr uint32_t kWpadMinus = 1u << 12;
// Host-only bit carried through the PAD intermediate; never reaches the guest's hold word.
constexpr uint32_t kHostShake = 1u << 31;

constexpr uint32_t kKpadStatusSize = 0x84u;
constexpr uint32_t kViRetraceCountAddr = 0x8042AB4Cu; // same accessor the tick pump serves

bool g_padInited = false;
uint32_t g_frameHold = 0, g_frameTrig = 0, g_frameRelease = 0;
uint32_t g_prevHold = 0;
uint32_t g_lastSampledTick = 0xFFFFFFFFu;
// Synthetic accelerometer burst (see the header comment). Ticks remaining, and acc.y this tick.
uint32_t g_shakeBurstTicks = 0;
float g_frameAccY = 0.0f;
constexpr uint32_t kShakeBurstTicks = 8;
constexpr float kShakeBurstPattern[4] = {1.0f, 0.5f, 0.0f, -0.5f};
// Tilt (see the header comment): acc.z this tick, eased toward the bound tilt amount. 0.25 per
// tick is a full tilt in 4 ticks, and below the shake burst's 0.5 steps in y.
float g_frameAccZ = 0.0f;
constexpr float kTiltStepPerTick = 0.25f;
constexpr uint32_t kGameKeyInstanceAddr = 0x8042A230u; // dGameKey_c::m_instance (syms.txt)

// NSMBW_LOG_TILT=1: tilt input, acc.z, and what the game made of it (remote 0's extension type
// at dGameKeyCore_c+8 - the no-extension path needs 0 - and its angle at +0x8E), on change.
void LogTilt(uint32_t tick, float target) {
    static const bool log = AURORA_ENV("NSMBW_LOG_TILT") != nullptr;
    if (!log) return;
    uint32_t gameKey = 0, core = 0, ext = 0xFFFFFFFFu;
    uint16_t angle = 0;
    if (Memory::TryRead32(kGameKeyInstanceAddr, gameKey) && gameKey != 0 &&
        Memory::TryRead32(gameKey + 4u, core) && core != 0) {
        Memory::TryRead32(core + 8u, ext);
        uint32_t word = 0;
        if (Memory::TryRead32(core + 0x8Cu, word)) angle = static_cast<uint16_t>(word & 0xFFFFu);
    }
    static float lastTarget = 2.0f, lastAccZ = 2.0f;
    static uint16_t lastAngle = 0xFFFFu;
    if (target == lastTarget && g_frameAccZ == lastAccZ && angle == lastAngle) return;
    lastTarget = target;
    lastAccZ = g_frameAccZ;
    lastAngle = angle;
    std::fprintf(stderr, "[nsmbw][tilt] tick=%u input=%+.2f acc.z=%+.2f ext=%u angle=%d (0x%04X)\n", tick,
                 target, g_frameAccZ, ext, static_cast<int16_t>(angle), angle);
}

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
    PADSetKeyButtonBinding(0, {kScancodeEquals, PAD_BUTTON_START}); // -> WPAD +
    PADSetKeyButtonBinding(0, {kScancodeMinus, PAD_TRIGGER_Z});    // -> WPAD -
    PADSetKeyButtonBinding(0, {kScancodeC, PAD_TRIGGER_L});        // -> shake burst
    PADSetKeyButtonBinding(0, {kScancodeUp, PAD_BUTTON_UP});
    PADSetKeyButtonBinding(0, {kScancodeDown, PAD_BUTTON_DOWN});
    PADSetKeyButtonBinding(0, {kScancodeLeft, PAD_BUTTON_LEFT});
    PADSetKeyButtonBinding(0, {kScancodeRight, PAD_BUTTON_RIGHT});
    g_padInited = true;
}

// Left stick -> d-pad, per axis, so diagonals (crouch-walk, down+run) still work. The PAD layer
// reports the stick as s8 (+-127 at full tilt, +Y = up, pad.cpp:228); about 40 % tilt counts as pressed.
constexpr int kStickThreshold = 50;

uint32_t TranslatePadToWpad(const PADStatus& status) {
    uint32_t pad = status.button;
    if (status.stickX <= -kStickThreshold) pad |= PAD_BUTTON_LEFT;
    if (status.stickX >= kStickThreshold) pad |= PAD_BUTTON_RIGHT;
    if (status.stickY >= kStickThreshold) pad |= PAD_BUTTON_UP;
    if (status.stickY <= -kStickThreshold) pad |= PAD_BUTTON_DOWN;

    uint32_t w = 0;
    if (pad & PAD_BUTTON_Y) w |= kWpad2;
    if (pad & PAD_BUTTON_X) w |= kWpad1;
    if (pad & PAD_BUTTON_A) w |= kWpadA;
    if (pad & PAD_BUTTON_B) w |= kWpadB;
    if (pad & PAD_BUTTON_START) w |= kWpadPlus;
    if (pad & PAD_TRIGGER_Z) w |= kWpadMinus;
    if (pad & (PAD_TRIGGER_L | PAD_TRIGGER_R)) w |= kHostShake;
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
    NsmbwSkipBootScreensApply(); // once per frame; see nsmbw_skip_boot_screens.cpp
    SDL_PumpEvents();
    PADStatus statuses[PAD_CHANMAX]{};
    PADRead(statuses);

    // Action bindings per context (runtime/src/product/nsmbw_controls.cpp). PADRead above still
    // runs for the PAD layer's own bookkeeping; TranslatePadToWpad is kept only as documentation
    // of the old fixed mapping and for NSMBW_LEGACY_PAD_MAPPING=1.
    static const bool legacy = AURORA_ENV("NSMBW_LEGACY_PAD_MAPPING") != nullptr;
    uint32_t hold = legacy ? TranslatePadToWpad(statuses[0]) : NsmbwControlsReadWpad();
    hold |= g_nsmbwSelfTestPressBits;
    g_nsmbwSelfTestPressBits = 0;

    // A C press edge (not hold) starts one burst, so a tap is one spin like one flick of the
    // remote; the game's cooldowns decide how soon the next one can fire.
    if ((hold & kHostShake) && !(g_prevHold & kHostShake)) g_shakeBurstTicks = kShakeBurstTicks;
    if (g_shakeBurstTicks != 0) {
        g_frameAccY = kShakeBurstPattern[(kShakeBurstTicks - g_shakeBurstTicks) & 3u];
        --g_shakeBurstTicks;
    } else {
        g_frameAccY = 0.0f;
    }

    // acc.z target: tilt right (+input) is -acc.z - see "Sign" in the header comment.
    const float tiltTarget = legacy ? 0.0f : -NsmbwControlsReadTilt();
    if (g_frameAccZ < tiltTarget) {
        g_frameAccZ = g_frameAccZ + kTiltStepPerTick > tiltTarget ? tiltTarget : g_frameAccZ + kTiltStepPerTick;
    } else if (g_frameAccZ > tiltTarget) {
        g_frameAccZ = g_frameAccZ - kTiltStepPerTick < tiltTarget ? tiltTarget : g_frameAccZ - kTiltStepPerTick;
    }
    LogTilt(tick, -tiltTarget); // log the input (+ = right), not the acc.z target

    g_frameHold = hold;
    g_frameTrig = hold & ~g_prevHold;
    g_frameRelease = g_prevHold & ~hold;
    g_prevHold = hold;
    g_frameHold &= ~kHostShake;
    g_frameTrig &= ~kHostShake;
    g_frameRelease &= ~kHostShake;

    if (g_frameTrig != 0 || g_frameRelease != 0) {
        static const bool log = AURORA_ENV("NSMBW_LOG_INPUT") != nullptr;
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
        // acc (Vec, +0xC) in g: x, y, z. y is the shake burst, z the tilt; x stays 0. And
        // acc_vertical (Vec2, +0x54), which is what the game turns into the tilt angle. See header.
        auto writeFloat = [](uint32_t addr, float v) {
            uint32_t bits = 0;
            std::memcpy(&bits, &v, sizeof(bits));
            Memory::Write32(addr, bits);
        };
        writeFloat(samples + 0x10u, g_frameAccY);
        writeFloat(samples + 0x14u, g_frameAccZ);
        writeFloat(samples + 0x54u, std::sqrt(std::fmax(0.0f, 1.0f - g_frameAccZ * g_frameAccZ)));
        writeFloat(samples + 0x58u, -g_frameAccZ);
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
