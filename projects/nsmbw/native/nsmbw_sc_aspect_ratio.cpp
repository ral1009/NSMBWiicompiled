// NSMBW address binding for SCGetAspectRatio_HLE (runtime/src/hle/sc.cpp), which the shared
// runtime registers only at Mario Kart Wii's 0x801B1BE4.
//
// 0x801DD310 = SCGetAspectRatio() (function_map.txt). Its translated body calls the SC item
// lookup at 0x801DC9F0 with item id 1 (IPL.AR) into a stack byte and returns 1 only when the
// lookup succeeds and the byte is 1; it writes no guest globals, so replacing it leaves no stale
// bookkeeping behind.
//
// Without this the game read the emulated SYSCONF, got 4:3, and letterboxed its 16:9 scene inside
// the 640x480 frame, which the presenter then pillarboxed to 4:3 in a 16:9 window: a 2000x1125
// window showed the picture in a 1500x873 box (measured 2026-09-26, PrintWindow capture at 4x).
// With this, Config.toml's `widescreen` decides, as it already does for MKW.
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

#include <cstdint>

extern "C" uint32_t SCGetAspectRatio_HLE();

PPC_NATIVE_OVERRIDE(801DD310, SCGetAspectRatio_HLE, uint32_t, (), ());
