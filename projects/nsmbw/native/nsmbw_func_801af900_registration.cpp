// Dynamic dispatch-table registration for every NSMBW function excluded from the generated build
// solely because it calls func_801A9CE0's "state-free ABI" fast path.
//
// These functions are normal, correctly-translated guest code (no bug of their own) - but the
// translator's emit-nsmbw-build-shards pass drops their entries from the *generated* static
// indirect dispatch table (shards.cmake's nsmbw_dispatch/registration output) as a side effect of
// an unrelated fix: each one forward-declares and calls func_801A9CE0_statefree_v0 (a "state-free
// ABI" fast-path variant of func_801A9CE0), guarded by
// `MkwStateFreeAbiEnabled(...) && KnownTranslatedCpuCall<0x801A9CE0u>::kAvailable && ...`. Since
// func_801A9CE0 itself is natively overridden (see nsmbw_func_801a9ce0.cpp, fixing a translator
// mistranslation - an infinite self-branch), its FunctionRecord is excluded from the merged
// "activeFunctions" set the emitter scans, so nothing in the build ever defines
// func_801A9CE0_statefree_v0 anymore. The emitter's cross-module ABI-conflict check
// (NsmbwMultiModuleShardEmitter.cs's `stateFreeMismatchedCallers`) treats "absent" the same as
// "signature mismatch" and drops every caller whose forward declaration can't be satisfied - even
// though the guarded call is compile-time dead code in this build
// (KnownTranslatedCpuCall<0x801A9CE0u>::kAvailable resolves to the primary template's `false`
// default, confirmed by reading abi_bridge.h, precisely because of the native override).
// grep -rl "func_801A9CE0_statefree_v0(r3)" build/nsmbw/functions/ confirmed exactly 5 such callers
// (excluding func_801A9CE0.cpp itself, the definer): func_801AF900, func_801AC980, func_801B8B20,
// func_801AD620, func_801AD9E0 - a small, bounded, fully-enumerated set, not an open-ended one.
//
// The fix has two parts, applied identically to all five:
//   1. Each function's guest body (with the dead state-free branch collapsed to its already-taken
//      `else` fallback) is appended directly to
//      generated_nsmbw/build_shards/nsmbw_all/shard_5179f8d1c7b31ca1ca073eb6.cpp so the symbol
//      exists and links. (func_801AF900 additionally keeps the session's three (r13-20020)
//      tick-counter fixes, re-applied at the same time.)
//   2. This file supplies the missing *dispatch table* entry for each. All five are reached only
//      via indirect calls (confirmed for func_801AF900 by lldb backtrace; the others surfaced the
//      same way, one at a time, as "missing_target" crashes during boot), so the runtime needs to
//      resolve each address to its symbol at indirect-dispatch time - the static per-build table
//      (which normally provides that) is where the addresses were dropped, so this uses the
//      hand-written dynamic-registration escape hatch instead (TranslatedFunctionRegistry::Register,
//      via REGISTER_TRANSLATED_FUNCTION below). Per abi_bridge.h's InvokeIndirectCpu ->
//      FindRawByAddressPtrMiss, a dynamically registered record is consulted whenever the baked
//      static table doesn't have the address - exactly this case - so no shard/table regeneration
//      is needed for these five addresses.
#include "abi_bridge.h"

extern "C" void func_801AF900(CpuContext* ctx);
extern "C" void func_801AC980(CpuContext* ctx);
extern "C" void func_801B8B20(CpuContext* ctx);
extern "C" void func_801AD620(CpuContext* ctx);
extern "C" void func_801AD9E0(CpuContext* ctx);

REGISTER_TRANSLATED_FUNCTION(0x801AF900, func_801AF900);
REGISTER_TRANSLATED_FUNCTION(0x801AC980, func_801AC980);
REGISTER_TRANSLATED_FUNCTION(0x801B8B20, func_801B8B20);
REGISTER_TRANSLATED_FUNCTION(0x801AD620, func_801AD620);
REGISTER_TRANSLATED_FUNCTION(0x801AD9E0, func_801AD9E0);
