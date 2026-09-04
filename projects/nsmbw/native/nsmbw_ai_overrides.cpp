// NSMBW address binding for the AI HLE in runtime/src/hle/audio/audio.cpp, which registers
// AIInit at Mario Kart Wii's 0x801240B0. Same situation as the IOS and WPAD families.
//
// 0x8019F330 = AIInit(u32 callbackStackSwitch). Evidence, all from its own disassembly:
//   - guards on an init flag at r13-0x51E8 and returns immediately when it is already 1
//   - reads the bus clock from 0x800000F8 and precomputes the timing windows it later
//     compares against
//   - stores its r3 argument to r13-0x51B4 and zeroes r13-0x51B0
//   - OSSetInterruptHandler(5, 0x8019A4B0) then __OSUnmaskInterrupts(0x04000000)
//   - sets the init flag at r13-0x51E8 to 1 last
//
// Without this, boot hangs at 0x8019F644 - an inline sample-rate calibration inside AIInit
// (0x8019F5D0) that does `do { s = AISCNT & 0x7FFFFFFF; } while (s == prev);` against the AI
// sample counter at 0xCD006C08, times the interval with OSGetTime, and repeats until the
// measurement lands in one of two windows. runtime/src/hle/ai.cpp models the AI register block
// as flat storage, so AISCNT never advances and that inner loop never exits.
//
// Lives here, not in the shared runtime tree, because the translator's override-skip detection
// only scans this project's native_registration_root (projects/nsmbw/native).
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

#include <cstdint>

// Named for MKW's address because that is where the shared implementation is registered; the
// body itself is title-independent apart from the four guest globals audio.cpp selects per
// product.
extern "C" void AIInit_801240b0(uint32_t callbackStackSwitch);

PPC_NATIVE_OVERRIDE_VOID(8019F330, AIInit_801240b0, (uint32_t callbackStackSwitch), (callbackStackSwitch));
