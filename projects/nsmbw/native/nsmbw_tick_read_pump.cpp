// Native override for 0x801BE010, a one-line leaf that just returns the guest global at
// 0x8042AB4C (kViRetraceCountAddr - r13-0x4E34, matching this project's sda_base) - i.e. it's
// NSMBW's "read the current VI retrace tick" accessor.
//
// Found while chasing func_801AF710's boot stall, right after the disk-ID-check fix
// (nsmbw_disk_id_check_sync.cpp) cleared the previous blocker: func_801AF710 calls this twice and
// spins (`bl 0x801be010; ...; bl 0x801be010; subf r0,r24,r3; cmpwi r0,1; blt back`) until the
// delta between two reads is >=1 - a "wait for at least one VI retrace tick to pass" idiom. Same
// root cause as the SelectThread bug this session already fixed: the retrace count only actually
// advances from inside SelectThread's own idle loop (which polls VI_HLE_PollRetrace), and this is
// a tight spin with no yield point, so it never reaches that idle loop no matter how long it runs.
//
// Fix: pump one VI_HLE_PollRetrace() call every time this specific accessor is read, so time
// visibly advances for whoever's polling it - the general fix for this whole class of "wait for
// the tick to move" loop (not particular to func_801AF710; anything else reading ticks through
// this same accessor benefits too), consistent with the SelectThread fix's own approach of running
// VI's real retrace-advance logic rather than inventing a fake counter increment.
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"

extern "C" void nsmbw_tick_read_pump_801be010(CpuContext* ctx)
{
    VI_HLE_PollRetrace(ctx);
    ctx->gpr[3] = ::Memory::Read32(0x8042AB4Cu);
}

PPC_NATIVE_OVERRIDE_VOID(801BE010, nsmbw_tick_read_pump_801be010, (CpuContext* ctx), (ctx));
