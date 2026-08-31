#include "hle_stubs.h"
#include "runtime_log.h"

// 0x00000060 -> unknown Wii OS/boot-provided low-memory call. Not part of any
// dumped file (main.dol or any REL) - called via a hardcoded absolute `bl`
// from a depth-3 main.dol function (0x801AB150) during early startup.
//
// TODO(unverified stub): real behavior is UNKNOWN. This no-op unblocks
// translation/compilation but has not been validated against real hardware
// or Dolphin. If the recompiled game misbehaves in a way traceable to early
// boot, start here. See docs/MASTER_PLAN.md Phase 6 and Phase 9.
extern "C" void NsmbwBootStub_00000060()
{
    RT_LOGF(RT_TAG_OS, "NsmbwBootStub_00000060: unimplemented low-memory boot call, ignoring\n");
}

PPC_NATIVE_OVERRIDE_VOID(00000060, NsmbwBootStub_00000060, (), ());
