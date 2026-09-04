// Native override for NSMBW's own compiled SelectThread (0x801B4FE0), the function
// nsmbw_vi_wait_for_retrace.cpp already documented as "real, ordinary translated guest code, not a
// native override" - this file makes it one.
//
// This runtime already has a SelectThread HLE (SelectThread_801b4fe0, in
// runtime/src/hle/os/os_scheduler.cpp) with the correct idle loop that polls
// VI/Audio/Alarm - but it used to be registered at 0x801A9C08, which NSMBW's own compiled bytes
// show is not SelectThread at all: disassembling that address straight from main.dol lands on an
// unrelated cluster of tiny mfmsr/mtmsr/mtspr SPR-wrapper leaf functions, so the override never
// fired. Guest code ran its own literal SelectThread body at 0x801B4FE0 instead, whose final
// "nothing runnable" path is the exact same interrupt-enable/poll-flag/interrupt-disable spin
// idiom VIWaitForRetrace's wait loop (via func_801B4FE0) gets stuck in - proven live by attaching
// to the stuck process twice, 24s apart, and finding the native RIP inside a 2-instruction branch
// loop both times.
//
// The real address was found by matching this guest function's field accesses one-for-one against
// SelectThread_801b4fe0's own logic (run queue linking, cntlzw priority select, READY/RUNNING
// state, the switch-thread callback indirect call) - not guessed. That same disassembly is where
// os_internal.h's kThreadQueueArrayAddr / kSwitchThreadCallbackPtrAddr / kSchedulerReschedCounterAddr
// / kSchedulerPendingFlagAddr / kSchedulerIdleFlagAddr were re-derived from, since they were
// wrong for NSMBW for the same reason (this header is shared with the mkwii project).
//
// Lives here, not in the shared runtime/src/hle/os/os_scheduler.cpp, for the same reason
// VIWaitForRetrace's override does (see nsmbw_vi_wait_for_retrace.cpp): the translator's
// override-skip detection only scans this project's native_registration_root
// (projects/nsmbw/native), so a registration added directly in the shared runtime tree isn't
// picked up by translate-recursive and produces a duplicate func_801B4FE0 symbol at link time
// against the shard's own auto-translated copy.
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

extern "C" void SelectThread_801b4fe0(CpuContext* ctx);

PPC_NATIVE_OVERRIDE_VOID(801B4FE0, SelectThread_801b4fe0, (CpuContext* ctx), (ctx));
