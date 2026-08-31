// Native override for NSMBW's DI_CONFIG probe leaf (0x801AB2F0). Confirmed against the real
// translated body (build/nsmbw/functions/func_801AB2F0.cpp): three guest instructions -
// `lis/lwz` DI_CONFIG (0xCD006024), `rlwinm` masking the loaded word down to its low byte into
// r3, `blr`. It does not touch CR itself; the two callers (func_801B9090 and func_801BA7B0's EXI
// channel-probe helpers) each do their own `cmpwi r3, 0xFF` against the returned byte after the
// call returns - see runtime/src/hle/di.cpp's header comment for why 0 (this runtime's DI_CONFIG
// default) and a real board-revision byte both compare not-equal to that 0xFF sentinel, so this
// override's return value is correct on real hardware and here alike.
//
// A function-level override is required, not just DI_CONFIG's checked-path registration in
// hle/di.cpp: translated *loads* of MMIO ranges use the raw, unchecked flat-memory path for speed
// (see pi.cpp's comment), so a fault there can't recover a value from a caught hardware
// exception - the same reason hle/vi.cpp overrides MKW's VI functions at the guest-function level
// instead of the memory level. Registering PPC_NATIVE_OVERRIDE_VOID also removes 0x801AB2F0 from
// LeafFunctionInliner's candidates (LeafInliningPolicy.BlockedTargets) - it's a 3-instruction
// straight line, small enough that the translator was already inlining it directly into both call
// sites, and a splice bypasses whatever override is registered for the callee.
#include "hle_stubs.h"
#include "memory.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

namespace {
constexpr uint32_t kDiConfigAddr = 0xCD006024u;
} // namespace

extern "C" void DIConfigProbe_NSMBW_801AB2F0(CpuContext* ctx)
{
    const uint32_t value = Memory::Read32(kDiConfigAddr);
    ctx->gpr[0] = value;
    ctx->gpr[3] = value & 0xFFu;
}

PPC_NATIVE_OVERRIDE_VOID(801AB2F0, DIConfigProbe_NSMBW_801AB2F0, (CpuContext* ctx), (ctx));
