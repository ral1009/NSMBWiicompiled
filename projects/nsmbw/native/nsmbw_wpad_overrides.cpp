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

#include <cstdint>

extern "C" int32_t WPADInit_HLE();
extern "C" int32_t WUDGetStatus_HLE();

PPC_NATIVE_OVERRIDE(801DFB90, WPADInit_HLE, int32_t, (), ());
PPC_NATIVE_OVERRIDE(801F3670, WUDGetStatus_HLE, int32_t, (), ());
