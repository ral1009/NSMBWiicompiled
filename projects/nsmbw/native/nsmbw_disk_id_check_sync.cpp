// Native override for 0x801D2D60, called from func_801D0E30 right before a local busy-wait:
//   stw r29(=0), -0x6048(r13)      ; init completion flag
//   bl 0x801d2d60                  ; kick off (disk-ID / region check, exact scope unconfirmed)
//   lwz r0,-0x6048(r13); cmpwi r0,0; beq $-8   ; spin until flag != 0
//
// Same shape as the SCInit poll-flag stall this session already fixed (see
// nsmbw_sc_deferred_write_sync.cpp): an operation kicked off synchronously, followed by a wait for
// a completion flag that this recompiled runtime never sets because whatever async/callback path
// would set it isn't reached. Confirmed live: RIP sat oscillating inside func_801D0E30's wait loop
// across a 20+ second window (no progress) immediately after the SC fix cleared the previous
// stall.
//
// Fix: mark the completion flag its caller polls for. -0x6048(r13) is an SDA-relative guest
// global (r13 is the fixed small-data base, not a stack pointer), so this write is meaningful
// regardless of who's calling; there's no way to run 0x801D2D60's own real translated body from
// here (overriding this address removes it from translation), so this override does not attempt
// to preserve whatever bookkeeping the real function did (table-entry validation against
// r7+r9*32, per static trace) - if that bookkeeping turns out to matter downstream, the next
// blocker will show it.
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"

extern "C" void nsmbw_disk_id_check_sync_801d2d60(CpuContext* ctx)
{
    // func_801D0E30 switches on a result code at -0x4CC8(r13) once the flag below goes non-zero:
    //   1 -> compare the 0x40-byte buffer (0x80395E60) against the table at 0x803427D8, and on a
    //        full 52-byte match fall through to 0x801D1010, which returns cleanly.
    //   2 -> a second async round trip (0x801D2720), then the same code==1 requirement.
    //   anything else -> 0x801D1038 -> func_801D0940 -> func_801AF710, which is the *shutdown*
    //        path (it calls OSDisableScheduler at 0x801AF734 and never re-enables), i.e. the game
    //        deliberately resetting. Leaving this code unset is what made every earlier boot end
    //        with the scheduler disabled and "Entry point returned".
    // Reporting 1 with the buffer left as func_801D0E30 zeroed it takes the match path, because
    // the comparison table at 0x803427D8 is itself zero-filled in the DOL's .data. That's a real
    // in-game code path on real data, not a forced branch - but it is only "correct" as long as
    // that table stays zero; if something later populates it, this needs the actual 0x40 bytes
    // that 0x801D2D60's real body would have fetched.
    ::Memory::Write32(ctx->gpr[13] - 0x4CC8u, 1u);
    ::Memory::Write32(ctx->gpr[13] - 0x6048u, 1u);
}

PPC_NATIVE_OVERRIDE_VOID(801D2D60, nsmbw_disk_id_check_sync_801d2d60, (CpuContext* ctx), (ctx));

// Second async-with-callback op on the same boot path, hit once the 52-byte compare above
// mismatches and func_801D0E30 takes its retail branch at 0x801D0F94:
//   stw r0,-0x6044(r13)            ; clear flag2
//   bl 0x801d07e0                  ; r3 = 0x0123456A, r4 = callback func_801D0D60
//   lwz r0,-0x6044(r13); cmpwi 0; beq $-8   ; spin until the callback sets flag2
// func_801D07E0's real body stashes r3 at 0x80395D48, the callback at -0x4CE0(r13), then starts
// the operation via func_801D3530; the callback (func_801D0D60) does nothing but `flag2 = 1`.
// Nothing in this runtime ever completes that operation, so the spin is unbounded - confirmed
// live by disassembling the stuck native loop, which polls guest 0x8042993C (= r13-0x6044).
//
// This override keeps the two stores the real body makes and then invokes the caller's callback
// immediately, which is what an instant completion looks like. r3 is set to 1 because that is the
// result code func_801D0E30 requires on the callback paths that record one (func_801D0D50 stores
// r3 to -0x4CC8, and 1 is its "continue" value); func_801D0D60 ignores r3 entirely.
extern "C" void nsmbw_async_complete_sync_801d07e0(CpuContext* ctx)
{
    const uint32_t magic = ctx->gpr[3];
    const uint32_t callback = ctx->gpr[4];
    ::Memory::Write32(0x80395D40u + 8u, magic);
    ::Memory::Write32(ctx->gpr[13] - 0x4CE0u, callback);
    if (callback != 0u) {
        ctx->gpr[3] = 1u;
        InvokeIndirectCpu(callback, ctx);
    }
}
PPC_NATIVE_OVERRIDE_VOID(801D07E0, nsmbw_async_complete_sync_801d07e0, (CpuContext* ctx), (ctx));
