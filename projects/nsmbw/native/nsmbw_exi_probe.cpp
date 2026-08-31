// Native override for NSMBW's own compiled EXIProbe body (0x801BA0C0). Confirmed against
// NSMBW-Decomp's include/lib/revolution/EXI/EXIBios.h: `BOOL EXIProbe(EXIChannel chan)` - a
// device-presence check with a plain BOOL contract, not a function this runtime needs to
// faithfully emulate the internal chip-select/command/ID-read handshake for.
//
// Unlike EXIInit (0x801B9CE0) and EXISelect (0x801B9700), which are small enough to port
// faithfully and were verified working, EXIProbe's real body spans this function plus at least
// two more (func_801B9090, func_801B8C60 - together 1496+ lines) implementing an actual bit-level
// EXI transaction protocol (chip-select assertion, command bytes, ID-register readback, repeated
// for several known device signatures). Attempting a full port of that chain produced a genuine
// HANG (not a crash) - the caller kept retrying/looping deeper into unexamined logic whose exit
// condition could not be confirmed safe without unbounded additional reverse-engineering. Per the
// project's "correctness over apparent progress" rule, that isn't acceptable to guess past.
//
// What IS verified, directly from the real translated body: this function has an EARLY, GENUINE
// "not found" exit for exactly this environment's honest state - r4==0 (device index 0) with the
// channel already known busy/idle takes `r3 = 0; goto loc_801BA450;` without ever touching the
// output pointer (loc_801BA138 in func_801BA0C0.cpp) or running the handshake at all. Since this
// runtime attaches no real device to any EXI channel (no memory card, no broadband adapter, no
// AD16), "not found" is the correct answer for every channel/device combination, not a guess -
// it's the same conclusion the real protocol would eventually reach for an empty bus, just
// without the possibility of getting stuck inside protocol logic this runtime doesn't model.
//
// The one real exception the translated body honors is a small result cache at a fixed low-memory
// address (r13-20248, populated by a prior successful probe) for the (channel==0, device==2)
// case - reproduced here so a second probe of an already-identified device still reports it
// consistently, matching loc_801BA100/801BA10C's own cache-hit behavior.
#include "hle_stubs.h"
#include "memory.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

extern "C" void EXIProbe_NSMBW_801BA0C0(CpuContext* ctx)
{
    const uint32_t channel = ctx->gpr[3];
    const uint32_t device = ctx->gpr[4];
    const uint32_t outPtr = ctx->gpr[5];
    const uint32_t r13 = ctx->gpr[13];

    if (channel == 0 && device == 2) {
        const uint32_t cached = Memory::Read32(static_cast<uint32_t>(r13 - 20248));
        if (cached != 0) {
            Memory::Write32(outPtr, cached);
            ctx->gpr[3] = 1;
            return;
        }
    }

    ctx->gpr[3] = 0;
}

PPC_NATIVE_OVERRIDE_VOID(801BA0C0, EXIProbe_NSMBW_801BA0C0, (CpuContext* ctx), (ctx));
