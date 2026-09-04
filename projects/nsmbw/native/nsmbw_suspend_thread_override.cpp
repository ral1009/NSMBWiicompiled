// Native override for NSMBW's own compiled OSSuspendThread (0x801B5C40). Same bug class as
// SelectThread and OSCreateThread this session: the override used to be bound to 0x801AA6A8,
// which is wrong for NSMBW. The real address was confirmed, not guessed, by matching this
// override's own field offsets (kThreadSuspendOffset=0x2CC, kThreadStateOffset=0x2C8,
// kThreadNextOffset=0x2E0, kThreadPrevOffset=0x2E4, kThreadQueueOffset=0x2DC,
// kThreadPriorityOffset=0x2D0) one-for-one against 0x801B5C40's disassembly.
//
// Found while chasing the boot sequence past func_801B8B20 (the last of the 5 recovered stub
// functions this session): a newly-created guest thread (via the now-correctly-bound
// OSCreateThread) called into 0x801B5C40 while it was still real, untranslated guest code (the
// override having been dead at the wrong address), and something downstream of that faulted on
// an unmapped read at 0xFFFFFFFC, unwinding all the way back to the entry point returning early
// instead of continuing into the game's real main loop.
//
// Lives here, not in the shared runtime/src/hle/os/os_thread.cpp, for the same reason
// SelectThread's and OSCreateThread's overrides do: the translator's override-skip detection only
// scans this project's native_registration_root (projects/nsmbw/native).
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

extern "C" void OSSuspendThread_HLE_801b5c40(CpuContext* ctx);

PPC_NATIVE_OVERRIDE_VOID(801B5C40, OSSuspendThread_HLE_801b5c40, (CpuContext* ctx), (ctx));
