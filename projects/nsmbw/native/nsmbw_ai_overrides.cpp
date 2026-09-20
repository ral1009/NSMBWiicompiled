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

// The AI DMA family. Before 2026-09-20 only AIInit was bound, so the rest ran translated against
// runtime/src/hle/ai.cpp's flat register storage: AIRegisterDMACallback stored AX's callback at
// r13-0x51B0 (0x8042A7D0) but nothing host-side ever looked there, AIStartDMA set bit 15 of a
// register nobody serviced, and audio.cpp's per-block pump (which invokes the DMA callback and
// then AxDspHle::ServiceDeferredCallbacks) never had a callback to run. Net effect: AX's
// __AXNextFrame never executed, nw4r::snd never advanced, and any guest state waiting for a
// sound to *finish* waited forever - observed as dScGameSetup_c::StateID_VoiceEndWait being
// the last state GAME_SETUP ever enters after the player-count select (NSMBW_LOG_STATE_CHANGES,
// build_nsmbw/nsmbw_dl3.err.log).
//
// Identified from their translated bodies (AI register block 0xCC005000: +0x30/+0x32 DMA start
// hi/lo, +0x36 DMA length/control, +0x3A bytes left):
//   0x8019F1F0  AIRegisterDMACallback  swaps r13-0x51B0 under OSDisableInterrupts, returns old
//   0x8019F240  AIInitDMA(addr, len)   writes +0x30, +0x32, +0x36
//   0x8019F2C0  AIStartDMA             +0x36 |= 0x8000
//   0x8019F2E0  AIGetDMABytesLeft      (+0x3A & 0x7FFF) << 5
//   0x8019F2F0  AIGetDMAStartAddr      (+0x30 << 16 | +0x32) & 0x1FFFFFE0
//   0x8019F310  AIGetDMALength         (+0x36 & 0x7FFF) << 5
// audio.cpp already selects NSMBW's r13-0x51B0 / r13-0x51E4 globals per product, so the MKW
// bodies apply unchanged. AIGetDSPSampleRate is deliberately not bound: the function at the
// corresponding slot here (0x8019F320) only returns the AIInit flag at r13-0x51E8, i.e. it is
// AICheckInit, and the real sample-rate getter was not located.
extern "C" uint32_t AIRegisterDMACallback_80123f88(uint32_t callback);
extern "C" void AIInitDMA_80123fcc(uint32_t start_addr, uint32_t length);
extern "C" void AIStartDMA_80124048();
extern "C" uint32_t AIGetDMABytesLeft_8012405c();
extern "C" uint32_t AIGetDMAStartAddr_8012406c();
extern "C" uint32_t AIGetDMALength_80124084();

PPC_NATIVE_OVERRIDE(8019F1F0, AIRegisterDMACallback_80123f88, uint32_t, (uint32_t callback), (callback));
PPC_NATIVE_OVERRIDE_VOID(8019F240, AIInitDMA_80123fcc, (uint32_t start_addr, uint32_t length), (start_addr, length));
PPC_NATIVE_OVERRIDE_VOID(8019F2C0, AIStartDMA_80124048, (), ());
PPC_NATIVE_OVERRIDE(8019F2E0, AIGetDMABytesLeft_8012405c, uint32_t, (), ());
PPC_NATIVE_OVERRIDE(8019F2F0, AIGetDMAStartAddr_8012406c, uint32_t, (), ());
PPC_NATIVE_OVERRIDE(8019F310, AIGetDMALength_80124084, uint32_t, (), ());
