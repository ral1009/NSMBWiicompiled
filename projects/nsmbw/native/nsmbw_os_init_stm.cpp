// Native override for NSMBW's own compiled __OSInitSTM (0x801B6960). Confirmed against the real
// translated body: it saves the "/dev/stm/immediate" and "/dev/stm/eventhook" name strings,
// checks an "already initialized" flag at r13-20356, then calls func_80224DB0 (an "open a named
// resource, block until ready" wrapper - allocates a request object, hashes the name, posts it to
// a fixed-size work queue at guest address 0x803DA180, then calls OSSleepThread on the request's
// own queue field) once per device name, storing each result at r13-20352 ("/dev/stm/immediate")
// and r13-20348 ("/dev/stm/eventhook"), aborting with 0 if either comes back negative. On success
// it creates a background "STM watcher" thread (func_80225640, an OSCreateThread-shaped call) and
// sets r13-20356=1 before returning 1.
//
// The work-queue mechanism func_80224DB0 posts to has no consumer in this runtime: real hardware
// relies on a dedicated IOS/IO thread pulling requests off that same queue and calling
// OSWakeupThread once the real IOS_Open completes. This runtime only ever executes one cooperative
// CpuContext, so that thread never runs, the sleep never ends, and every guest device open through
// this path hangs (not crashes) - matching runtime/src/hle/ipc.cpp's own header comment predicting
// exactly this failure mode for "a guest blocked waiting for an IOS reply bit."
//
// This isn't guessed: MKW already has the identical, working fix for the identical SDK call at its
// own link address (__OSInitSTM_HLE_801ab848 in runtime/src/hle/os/os_init.cpp) - skip the real
// device-open-and-wait entirely, write fake non-zero handles and the initialized flag directly,
// and never create the watcher thread. That's safe there for the same reason it's safe here: the
// callback pointers a real power/reset button press would invoke are left unset, but this runtime
// never fires the STM hardware interrupt that would call them, so nothing ever reads them.
// NSMBW's own compiled binary places __OSInitSTM at a different address than MKW's build (they're
// linked separately even though they share the same SDK source), and MKW's override doesn't apply
// here for the same reason IOS_OpenAsync's MKW-only guard exists in runtime/src/hle/ios.cpp: an
// unconditional registration at MKW's address would either be dead (if NSMBW has nothing there) or
// hijack a real, unrelated NSMBW function - so this lives here, at NSMBW's own address, instead of
// being added to the shared override.
#include "hle_stubs.h"
#include "memory.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

extern "C" uint32_t OSInitSTM_NSMBW_801B6960(CpuContext* ctx)
{
    const uint32_t r13 = ctx->gpr[13];

    // Fake non-zero handles so callers' zero/negative-checks pass - same sentinel shape as MKW's
    // own override (real fd values are small positive integers; any non-negative value works).
    Memory::Write32(r13 - 20352u, 0x00535401u); // "ST\x01" - /dev/stm/immediate
    Memory::Write32(r13 - 20348u, 0x00535402u); // "ST\x02" - /dev/stm/eventhook
    Memory::Write32(r13 - 20356u, 1u);          // STM_Initialized

    return 1;
}

PPC_NATIVE_OVERRIDE(801B6960, OSInitSTM_NSMBW_801B6960, uint32_t, (CpuContext* ctx), (ctx));
