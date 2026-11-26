// NSMBW address binding for SCGetLanguage_HLE (runtime/src/hle/sc.cpp).
//
// 0x801DD460 = SCGetLanguage() (function_map.txt names only SCInit and SCGetAspectRatio nearby; the
// body was read from main.dol): it looks up SYSCONF item 11 (IPL.LNG) with the SC item lookup at
// 0x801DC9F0 into a stack byte, falls back to English (or Japanese when the console's area is
// Japan) if the lookup fails, and replaces any value above 9 by English. It writes no guest
// globals, so replacing it leaves no bookkeeping behind.
//
// Every language read goes through this one function: item 11 is looked up at 0x801DD474 only
// (a scan of main.dol for `bl 0x801DC9F0` with r4 = 11 finds no other place), and its callers
// (dGameCom::GetLanguageHBM 0x800B4630 -> AreaLanguageFolder 0x800B4670, and the SDK/nw4r code at
// 0x80107DBC and 0x801D0960..0x801D0CE8) call it directly.
//
// Needs the shard manifest regenerated (CLAUDE.md): a new PPC_NATIVE_OVERRIDE address, otherwise
// a `missing_target` crash at 0x801DD460.
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

#include <cstdint>

extern "C" uint32_t SCGetLanguage_HLE();

PPC_NATIVE_OVERRIDE(801DD460, SCGetLanguage_HLE, uint32_t, (), ());
