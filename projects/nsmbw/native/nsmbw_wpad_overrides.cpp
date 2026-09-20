// NSMBW address bindings for the WPAD/WUD HLE already implemented in
// runtime/src/hle/input/wpad.cpp, which registers at Mario Kart Wii's addresses
// (WPAD::Init 0x801BF5C4, WUDGetStatus 0x801CDB84, ...). Same situation as the IOS
// family in nsmbw_ios_ipc_overrides.cpp: NSMBW's own copies of the same SDK functions
// are at different addresses, so nothing intercepted them here.
//
// 0x801DFB90 = WPAD::Init. Evidence: it is called exactly once, from 0x801EDC88, behind
// an "already initialized" guard, and its return value is ignored there. It calls
// 0x801F32D0 (which brackets its body with the debug strings "BTA_Init() is started" /
// "BTA_Init() is done", i.e. WUDInit) and, only if that succeeds, 0x801DF930 (the WPAD-side
// setup: sets bit 0x100 in the GPIO register at 0xCD0000C0 and fills a per-channel table
// with 0xFF). It sits immediately before the block of one-instruction WPAD->WUD forwarding
// thunks at 0x801DFC00-0x801DFC70, matching MKW's layout around 0x801BF5C4.
//
// 0x801F3670 = WUDGetStatus. Evidence: it reads a signed byte at 0x803C3420 + 0x708 under
// OSDisableInterrupts/OSRestoreInterrupts and sign-extends it into the return value.
// 0x803C3420 is _wcb: the neighbouring 0x801F33F0 stores its two arguments to +0x6F4 and
// +0x6F8, and NSMBW-Decomp's WUDInternal.h gives WUDCB as allocFunc at 0x6F4, freeFunc at
// 0x6F8, and libStatus at 0x708.
//
// Without these, the boot hangs in 0x801F9F50 (reached via 0x801EDC30 -> 0x801DFB90 ->
// 0x801F32D0), spinning on a BTE stack-state byte that only the real Bluetooth hardware
// advances - the same device this runtime's IOS HLE reports as unknown
// ("/dev/usb/oh1/57e/305").
//
// Lives here, not in the shared runtime tree, because the translator's override-skip
// detection only scans this project's native_registration_root (projects/nsmbw/native).
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include "hle/controller_status_contract.h"

#include <cstdint>

extern "C" int32_t WPADInit_HLE();
extern "C" int32_t WUDGetStatus_HLE();
extern "C" int32_t WPADStartSimpleSync_HLE();
extern "C" int32_t WPADStopSimpleSync_HLE();
extern "C" uint32_t WPADSetSyncDeviceCallback_HLE(uint32_t callback);

// Real (un-overridden) WPAD entry points read the raw WPADCB struct directly instead of
// going through WPADGetInfoAsync's callback contract - confirmed by disassembling
// WPADProbe (0x801E1080), which pulls status(+0x8BC)/devType(+0x8C1)/handshakeFinished
// (+0x8DC) straight out of _wpdcb[chan]. Since WPAD::Init is entirely replaced by
// WPADInit_HLE above, nothing ever runs the real linking code that makes _wpdcb[chan]
// point at real storage - confirmed via a runtime diagnostic showing _wpdcb[0] reads as
// null the whole time. Found the real addresses by disassembling WPAD::Init's own
// call chain (0x801DFB90 -> 0x801DF930, the "WPAD-side setup" WPAD::Init calls after
// WUDInit succeeds - never reached since WPADInit_HLE replaces 0x801DFB90 outright):
// _wpdcb table base = 0x8039E630 (matches WPADProbe's own computed address exactly),
// _wpd[] backing storage base = 0x8039F660, stride 0x9C0 per channel
// (_wpdcb[i] = &_wpd[i], _wpd[i] = _wpd[0] + i*0x9C0 - read directly off 0x801DF930's
// linking loop). Seeding channel 0 here as an already-synced Wii Remote (status=0
// WPAD_ERR_NONE, handshakeFinished=true, devType=0 Core, not busy) so every real WPAD
// entry point that reads this struct directly - not just the ones explicitly
// overridden - sees a consistent, already-connected controller from boot, instead of
// needing the game's own Bluetooth pairing state machine (dConnect_c) to run at all.
namespace {
constexpr uint32_t kWpadCbTableAddr = 0x8039E630u;
constexpr uint32_t kWpadCbStorageAddr = 0x8039F660u;
constexpr uint32_t kWpadCbStride = 0x9C0u;

void SeedWpadCbChannel0()
{
    const uint32_t wpdcb0 = kWpadCbStorageAddr; // channel 0, no stride offset needed
    Memory::Write32(kWpadCbTableAddr, wpdcb0);  // _wpdcb[0] = &_wpd[0]

    Memory::Write32(wpdcb0 + 0x8BC, 0);         // status = WPAD_ERR_NONE
    Memory::Write8(wpdcb0 + 0x8C1, 0);          // devType = 0 (Core, no extension)
    Memory::Write32(wpdcb0 + 0x8D8, 1);         // used = TRUE
    Memory::Write32(wpdcb0 + 0x8DC, 1);         // handshakeFinished = TRUE
    Memory::Write8(wpdcb0 + 0x98C, 0);          // getInfoBusy = FALSE
}

// mPad::beginPad (0x8016F360) only calls readOne (-> WPADGetInfoAsync) for a channel when
// mPad's own player-count global (r13-0x5228, absolute 0x8042A758) is nonzero - confirmed
// live: WPADGetInfoAsync_NSMBW_HLE's diagnostic never fired even after the WPADCB seeding
// above made WPADProbe report channel 0 as connected, because this is a second, separate
// gate mPad checks on its own. Exhaustively searched every writer of this global
// (mPad::setPlayerCount, 0x8016F780, is the only one - checked via a full scan of the
// translator's generated output for direct stores to r13-0x5228): every call site either
// passes 0 or 10 (both disable polling) or restores a previously-saved value: nothing on a
// fresh boot ever sets it to a real player count, so mPad's per-channel polling is
// permanently dead code without this. Writing 1 directly is equivalent to calling the
// real setPlayerCount(1) - its own logic (0x8016F780) only takes a different path when the
// argument is 0.
void SeedMPadPlayerCount()
{
    Memory::Write32(0x8042A758u, 1);
}
} // namespace

extern "C" int32_t WPADInit_NSMBW_HLE()
{
    const int32_t result = WPADInit_HLE();
    SeedWpadCbChannel0();
    SeedMPadPlayerCount();
    return result;
}

PPC_NATIVE_OVERRIDE(801DFB90, WPADInit_NSMBW_HLE, int32_t, (), ());
PPC_NATIVE_OVERRIDE(801F3670, WUDGetStatus_HLE, int32_t, (), ());

// 0x801DFC00/0x801DFC10/0x801DFC40 = WPADStartSimpleSync / WPADStopSimpleSync /
// WPADSetSyncDeviceCallback. All three are one-instruction thunks in the same forwarding
// block as WPAD::Init above (0x801DFC00-0x801DFC70). Traced via dRemoconMng_c: its
// constructor registers its own callback (0x800DC000) through 0x801DFC40 -> 0x801F37D0,
// which does the same "swap value at a fixed struct offset, return the old one" the
// shared WPADSetSyncDeviceCallback_HLE already implements. 0x801DFC00 forwards into
// 0x801F3980 -> 0x801F3830 with hardcoded args (1,-1,0,1), matching WPADStartSimpleSync's
// real internal call shape. 0x801DFC10 is called from dGameCom::SelectCursorSetup
// (0x800B4540) guarded by a small manager object's flag_0x279/flag_0x27a bytes, and
// dRemoconMng_c's callback (0x800DC000) checks for result==1 before tail-calling
// 0x800B45A0, which sets that same object's flag_0x27b - the "a Wii Remote just synced"
// signal. The shared WPADStopSimpleSync_HLE already fires the registered callback with
// result=1, matching what 0x800DC000 checks for.
// TEMPORARY diagnostic wrappers: confirm these three actually get called during a normal
// boot (not just that binding them doesn't crash). Remove once answered - the real
// bindings are the *_HLE calls each of these forwards to.
extern "C" int32_t WPADStartSimpleSync_NSMBW_Diag()
{
    static int callCount = 0;
    if (callCount < 10) {
        ++callCount;
        std::fprintf(stderr, "[nsmbw][diag] WPADStartSimpleSync called (#%d)\n", callCount);
    }
    return WPADStartSimpleSync_HLE();
}
extern "C" int32_t WPADStopSimpleSync_NSMBW_Diag()
{
    static int callCount = 0;
    if (callCount < 10) {
        ++callCount;
        std::fprintf(stderr, "[nsmbw][diag] WPADStopSimpleSync called (#%d)\n", callCount);
    }
    return WPADStopSimpleSync_HLE();
}
extern "C" uint32_t WPADSetSyncDeviceCallback_NSMBW_Diag(uint32_t callback)
{
    static int callCount = 0;
    if (callCount < 10) {
        ++callCount;
        std::fprintf(stderr, "[nsmbw][diag] WPADSetSyncDeviceCallback called (#%d) callback=0x%08X\n",
            callCount, callback);
    }
    return WPADSetSyncDeviceCallback_HLE(callback);
}

PPC_NATIVE_OVERRIDE(801DFC00, WPADStartSimpleSync_NSMBW_Diag, int32_t, (), ());
PPC_NATIVE_OVERRIDE(801DFC10, WPADStopSimpleSync_NSMBW_Diag, int32_t, (), ());
PPC_NATIVE_OVERRIDE(801DFC40, WPADSetSyncDeviceCallback_NSMBW_Diag, uint32_t, (uint32_t callback), (callback));

// 0x801E1400 = WPADGetInfoAsync (chan, WPADInfo* infoOut, WPADCallback cb). Traced via
// disassembly: mPad::beginPad (0x8016F360) -> mPad::readOne (0x8016F710) -> here. r30
// inside this function is _wpdcb[chan] (WPADCB*): confirmed by exact offset match against
// WPADInternal.h's WPADCB.status (0x8BC), .handshakeFinished (0x8DC), .getInfoCB (0x988),
// .getInfoBusy (0x98C). On success it fills the caller's WPADInfo (WPAD.h: dpd/speaker/
// attach/lowBat/nearempty as 4-byte BOOL at 0x0/0x4/0x8/0xC/0x10, then battery/led/protocol/
// firmware as bytes at 0x14-0x17) and invokes the callback with (chan, result); mPad's
// callback (0x8016F6D0) then copies that WPADInfo into mPad's own per-channel table when
// result==0. This is connection/handshake status only, not per-frame button data -
// button bits are read through a separate, not-yet-traced path.
extern "C" int32_t WPADGetInfoAsync_NSMBW_HLE(uint32_t chan, uint32_t infoPtr, uint32_t callback)
{
    int32_t result;
    if (chan >= WpadContract::kChannelCount) {
        result = WpadContract::kErrorBadChannel;
    } else if (chan == 0) {
        if (infoPtr != 0) {
            Memory::Write32(infoPtr + 0x00, 0); // dpd
            Memory::Write32(infoPtr + 0x04, 0); // speaker
            Memory::Write32(infoPtr + 0x08, 1); // attach
            Memory::Write32(infoPtr + 0x0C, 0); // lowBat
            Memory::Write32(infoPtr + 0x10, 0); // nearempty
            Memory::Write8(infoPtr + 0x14, 4);  // battery
            Memory::Write8(infoPtr + 0x15, 1);  // led
            Memory::Write8(infoPtr + 0x16, 0);  // protocol
            Memory::Write8(infoPtr + 0x17, 0);  // firmware
        }
        result = 0;
    } else {
        result = WpadContract::kErrorNoController;
    }

    if (callback != 0 && TranslatedFunctionRegistry::FindByAddressPtr(callback)) {
        auto& cpu = GetPersistentCpuContext();
        cpu.gpr[3] = chan;
        cpu.gpr[4] = static_cast<uint32_t>(result);
        InvokeIndirectCpu(callback, &cpu);
    }

    // TEMPORARY diagnostic: is g_currentCore__4mPad (0x8042A748) / g_core__4mPad[]
    // (0x80377F88, 4 entries) ever non-null once channel 0 reports connected? Remove
    // once answered.
    if (chan == 0) {
        static int diagCallCount = 0;
        if (diagCallCount < 10) {
            ++diagCallCount;
            std::fprintf(stderr,
                "[nsmbw][diag] WPADGetInfoAsync chan0 call #%d: g_currentCore=0x%08X "
                "g_core[0..3]=0x%08X,0x%08X,0x%08X,0x%08X\n",
                diagCallCount,
                Memory::Read32(0x8042A748u),
                Memory::Read32(0x80377F88u), Memory::Read32(0x80377F8Cu),
                Memory::Read32(0x80377F90u), Memory::Read32(0x80377F94u));
        }
    }
    return result;
}
PPC_NATIVE_OVERRIDE(801E1400, WPADGetInfoAsync_NSMBW_HLE, int32_t,
         (uint32_t chan, uint32_t infoPtr, uint32_t callback), (chan, infoPtr, callback));
