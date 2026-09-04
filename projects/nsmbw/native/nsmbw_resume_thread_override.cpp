// Native override for NSMBW's own compiled OSResumeThread (0x801B5C40's sibling, 0x801B59A0).
// Fifth instance this session of the same bug class as SelectThread / OSCreateThread /
// OSSuspendThread: the override was bound to 0x801AA58C, which disassembles to the middle of an
// unrelated function (`mr r3,r30; lis r4,4; bl ...`), not a function entry at all.
//
// The real address was confirmed, not guessed, two ways: NSMBW's own main (0x800CA080) calls it
// directly at 0x800CA114 as the "resume the game thread I just created" step, and 0x801B59A0's
// body matches this override's own field usage one-for-one - it decrements the suspend count at
// kThreadSuspendOffset (0x2CC), clamps it at 0, and switches on kThreadStateOffset (0x2C8).
//
// This was the reason boot ended with "Entry point returned": main creates the game thread
// (entryFunc 0x800CA050, priority 16), resumes it, drops its own priority to 31, then suspends
// itself - the standard "hand off to the game thread forever" idiom. With resume bound to a dead
// address, the created thread never became runnable in this runtime's scheduler, so main's
// self-suspend had nothing to switch to, fell through, and returned out of the entry point.
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

extern "C" void OSResumeThread_HLE_801b59a0(CpuContext* ctx);

PPC_NATIVE_OVERRIDE_VOID(801B59A0, OSResumeThread_HLE_801b59a0, (CpuContext* ctx), (ctx));
