// Native override for NSMBW's own compiled OSCreateThread (0x801B5270, confirmed by name via
// NSMBW-Decomp's syms.txt: OSCreateThread=0x801B5270).
//
// This runtime already has a correct OSCreateThread HLE (OSCreateThread_HLE_801b5270, in
// runtime/src/hle/os/os_thread.cpp) - it was just bound to the wrong guest address, 0x801A9E84,
// the same bug class as SelectThread (see nsmbw_select_thread_override.cpp): disassembling
// 0x801A9E84 straight from main.dol lands on unrelated mfmsr/mtmsr/mtspr SPR-wrapper code, not a
// function that could plausibly be OSCreateThread. The real address was confirmed, not guessed,
// by matching 0x801B5270's field writes one-for-one against this override's own field offsets:
// `sth r7,0x2c8(r3)` (kThreadStateOffset, r7=1=kThreadStateReady), `stw r8,0x2d0(r3)` /
// `stw r8,0x2d4(r3)` (kThreadPriorityOffset/kThreadBasePriorityOffset), `stw r7,0x2cc(r3)`
// (kThreadSuspendOffset), `stw r29,0x2d8(r3)` (kThreadExitValueOffset, r29=-1 default exit code).
//
// Found while chasing the SCInit poll-flag blocker (0x8042AD30 stuck at 1): the EGG task the SC
// read enqueues is meant to be picked up and completed by a worker thread, but with
// OSCreateThread silently landing on the wrong address, any attempt to create that worker (or any
// other guest thread) would have executed unrelated SPR instructions instead - explaining why
// this process only ever has one OS thread (confirmed live: kThreadListTailAddr ==
// kOSRunningContextAddr, a single shared value) despite the game's own code trying to create more.
//
// Lives here, not in the shared runtime/src/hle/os/os_thread.cpp, for the same reason
// SelectThread's override does (see nsmbw_select_thread_override.cpp): the translator's
// override-skip detection only scans this project's native_registration_root
// (projects/nsmbw/native), so a registration added directly in the shared runtime tree isn't
// picked up by translate-recursive and produces a duplicate func_801B5270 symbol at link time
// against the shard's own auto-translated copy.
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

extern "C" void OSCreateThread_HLE_801b5270(CpuContext* ctx);

PPC_NATIVE_OVERRIDE_VOID(801B5270, OSCreateThread_HLE_801b5270, (CpuContext* ctx), (ctx));
