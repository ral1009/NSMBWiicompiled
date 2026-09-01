// Native override for NSMBW's own compiled func_801A9CE0 (0x801A9CE0). This is a translator
// mistranslation, not a missing HLE behavior - localized with lldb (real debugger, not guesswork)
// after a deterministic native SIGSEGV: reading location 0x10000000B inside func_801A9D00 (a
// trivial "mtspr 952, r3; return" leaf immediately after this function in the address space),
// with rcx (the CpuContext* argument) holding 0xFFFFFFFF instead of a valid pointer. That garbage
// register state traces back one frame to an inlined call to func_801A9CE0_statefree_v0 - the
// "state-free ABI" fast-path variant of this exact function - which the C++ compiler had
// aggressively miscompiled.
//
// The root cause, confirmed by reading BOTH translated forms of func_801A9CE0 (the regular
// CpuContext-taking one and the state-free one - identical bodies, so this isn't specific to the
// optimization path): the translator emitted
//   loc_801A9CE4: { r3 = 0; goto loc_801A9CE4; }
// - an unconditional branch to itself, forever, with no side effect. A real, shipped game cannot
// have an actual infinite loop with no side effects on this path (it's called during ordinary
// boot, and MKW's own build reaches the equivalent SDK call successfully) - this is a decoding bug
// where the real PPC branch (almost certainly a `blr` return, given the very next translated
// function func_801A9D00 sits only 32 bytes later at 0x801A9D00) got misread as a branch to its
// own address. Because the loop has no observable side effects, it's undefined behavior in C++
// (a non-terminating loop with no I/O/volatile access) - the optimizer is entitled to assume it
// terminates, and visibly does something with that license: the code inlined into func_801AF900
// generates a bogus CpuContext* for a subsequent indirect call instead of predictably hanging.
//
// The fix keeps exactly the one operation the (otherwise buggy) translation got right - r3=0,
// matching this function's own RECOMP_STATE_FREE_ABI metadata (gpr_in=0x8, gpr_out=0x8: r3 in,
// r3 out, nothing else) - and simply returns instead of looping, removing the UB rather than
// guessing at unrelated behavior.
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

extern "C" uint32_t Func801A9CE0_NSMBW_801a9ce0(CpuContext* ctx)
{
    (void)ctx;
    return 0;
}

PPC_NATIVE_OVERRIDE(801A9CE0, Func801A9CE0_NSMBW_801a9ce0, uint32_t, (CpuContext* ctx), (ctx));
