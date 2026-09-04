// Native override for NSMBW's own compiled VIWaitForRetrace (0x801BCE30). Confirmed against the
// real translated body (build/nsmbw/functions/func_801BCE30.cpp) and NSMBW-Decomp's syms.txt,
// which names this exact address VIWaitForRetrace: it snapshots the retrace counter at
// r13-20020 (0x4E34, matching this project's manifest sda_base of 0x8042f980 -> 0x8042ab4c),
// then loops calling func_801B5DD0 (insert the current thread into the retrace wait queue at
// r13-20056 / 0x4E58, then call the genuine translated __OSReschedule at 0x801B4FE0) until the
// counter changes.
//
// func_801B4FE0 is real, ordinary translated guest code, not a native override - it has no way to
// reach this runtime's VI_HLE_PollRetrace/idle-loop machinery (runtime/src/hle/os/os_scheduler.cpp),
// which only runs when SelectThread's own C++ implementation decides nothing is runnable. So this
// specific wait loop never advances the retrace counter it is polling, and spins forever: proven
// live by attaching to a stuck process and reading g_vi.retraceCount (stayed 0) and by setting a
// breakpoint on VI_HLE_PollRetrace, which never fired.
//
// This runtime already has a correct, working VIWaitForRetrace implementation
// (VIWaitForRetrace_HLE_801b99ec in runtime/src/hle/vi.cpp) - it is simply registered at the wrong
// address (0x801B99EC), which NSMBW's own compiled bytes show is a padding gap immediately before
// an unrelated function at 0x801B99F0, so nothing ever calls it there (the same class of
// link-address drift already found and fixed for OSCreateThread/OSResumeThread/OSSuspendThread:
// this runtime's OS/VI HLE hooks were laid out for a different build's addresses). Its return
// value is unconditionally discarded by every call site found in NSMBW's own code (func_802BC120,
// EGG::Video::initialize's caller, calls it twice back to back and never reads r3 from either
// call), so reusing it here - rather than writing a second implementation - changes no observable
// behavior at this call site.
//
// Lives here, not in the shared runtime/src/hle/vi.cpp, because the translator's override-skip
// detection ("regex-parses these macro names to skip that address at build time", per
// hle_stubs.h's PPC_NATIVE_OVERRIDE_VOID comment) only scans this project's configured
// native_registration_root (projects/nsmbw/native, per projects/nsmbw/nsmbw.yml) - a registration
// added directly to vi.cpp was not picked up by translate-recursive and produced a duplicate
// func_801BCE30 symbol at link time against the shard's own auto-translated copy.
#include "hle_stubs.h"
#include "memory.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

extern "C" void VIWaitForRetrace_HLE_801b99ec(CpuContext* ctx);

PPC_NATIVE_OVERRIDE_VOID(801BCE30, VIWaitForRetrace_HLE_801b99ec, (CpuContext* ctx), (ctx));

// NSMBW's retrace-callback setters. Without these the HLE's own g_vi.preRetraceCallback /
// postRetraceCallback stay 0, so AdvanceRetrace has nothing to invoke and the guest's
// post-retrace work never runs - which is what left EGG's endRender (0x802BB8C0) asleep on
// its own queue at self+0x60 forever, since the waker (0x802BBAA0) is only reached from that
// callback. Addresses read off the two setters themselves; see the kViPreRetraceCallback
// comment in runtime/src/hle/vi.cpp for the evidence and the ordering cross-check.
extern "C" void VISetPreRetraceCallback_HLE_801b90f4(CpuContext* ctx);
extern "C" void VISetPostRetraceCallback_HLE_801b9138(CpuContext* ctx);

PPC_NATIVE_OVERRIDE_VOID(801BC520, VISetPreRetraceCallback_HLE_801b90f4, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(801BC570, VISetPostRetraceCallback_HLE_801b9138, (CpuContext* ctx), (ctx));
