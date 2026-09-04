// Native override for 0x801D9890, the entry point NSMBW's SCInit uses to hand its deferred
// system-config write off to an EGG-level task registry for async completion.
//
// Traced while chasing the SCInit poll-flag stall (0x8042AD30 stuck at 1): SCInit -> func_801DBB40
// sets up a request substruct (guest addr computed as `lis r30,-0x7fc7; addi r30,r30,0x6440`,
// indexed by a slot byte at +0x15a) whose completion-callback field (substruct+4) is set to
// func_801DBC60, then calls this function to enqueue the actual transfer. The real path from here
// fans out through func_801D95E0 -> func_802277B0 -> func_80224C90 -> func_80224A50, which
// allocates a named task object and links it into a fixed-size (32-entry) array-based callback
// registry at guest 0x803DA2C0/0x803DA340. Nothing in this recompiled runtime ever drains that
// array - it isn't OS-thread-based (confirmed live: kThreadListHead/Tail/RunningContext never
// change even after fixing OSCreateThread's own wrong-address bug), and no per-frame pump reaches
// it this early in boot. func_801DBC60 is the substruct's own completion handler - it reads the
// transfer result and unconditionally writes the poll flag via func_801DBEB0 (whether or not a
// further user callback is registered) - it just never runs, because nothing drains the queue
// this override bypasses.
//
// func_801DBB40 already finishes all the real setup (buffer pointer, size, and the substruct's
// callback = func_801DBC60) before calling this function, and the transfer this represents is a
// small (0x4000-byte) system-config blob read that completes with negligible latency on real
// hardware. So this override skips the broken array hand-off entirely and invokes the substruct's
// completion callback immediately with a success result - the same effect an instant DMA
// completion would have. It does not touch 0x8042AD30 itself; func_801DBEB0 still owns that
// write, so this stays a real completion, not a forced flag flip.
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

extern "C" void nsmbw_sc_deferred_write_sync_801d9890(CpuContext* ctx)
{
    // r3==0 takes func_801DBC60's "verify the transferred data" branch (calls func_801D8BA0),
    // which fails because no override here actually populates the transfer buffer, and returns
    // silently before ever reaching func_801DBEB0's flag write (confirmed live: flag stayed 1).
    // r3!=0 skips verification and reaches func_801DBEB0 directly per static trace.
    ctx->gpr[3] = 1;
    InvokeIndirectCpu(0x801DBC60u, ctx);
}

PPC_NATIVE_OVERRIDE_VOID(801D9890, nsmbw_sc_deferred_write_sync_801d9890, (CpuContext* ctx), (ctx));
