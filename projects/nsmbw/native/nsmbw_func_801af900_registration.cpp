// Stubs for 5 NSMBW functions this build cannot currently translate normally:
// func_801AF900, func_801AC980, func_801B8B20, func_801AD620, func_801AD9E0.
//
// History: a past session found each one forward-declares and calls
// func_801A9CE0_statefree_v0 (a "state-free ABI" fast-path variant of func_801A9CE0), guarded by
// `MkwStateFreeAbiEnabled(...) && KnownTranslatedCpuCall<0x801A9CE0u>::kAvailable && ...`. Because
// func_801A9CE0 is natively overridden (nsmbw_func_801a9ce0.cpp, fixing a translator
// mistranslation - an infinite self-branch), that guard is compile-time-false and the branch is
// dead code, but the translator's emit-nsmbw-build-shards pass still excludes every caller of an
// overridden function's state-free variant from its own output (NsmbwMultiModuleShardEmitter.cs's
// stateFreeMismatchedCallers treats "the callee's FunctionRecord is absent" the same as "signature
// mismatch"). All five are reached only via indirect calls (confirmed for func_801AF900 by an
// lldb backtrace; the others surfaced the same way, one at a time, as "missing_target" crashes
// during boot), so they need an explicit address->symbol binding regardless of how their bodies
// are supplied.
//
// That past session's fix hand-appended each function's real guest body (dead branch collapsed to
// its already-taken else path) directly into one specific generated shard file, then supplied the
// dispatch-table entry here via the dynamic registry (REGISTER_TRANSLATED_FUNCTION). That worked
// until this session had to fully regenerate every shard (to register a native override for
// VIWaitForRetrace elsewhere - see nsmbw_vi_wait_for_retrace.cpp): shard filenames are
// content-hashed, so the regeneration produced an entirely different file and the hand-appended
// bodies were gone, leaving REGISTER_TRANSLATED_FUNCTION here with no symbol to point at
// (`undefined symbol: func_801AF900` etc at link time).
//
// Recovering the real bodies requires re-running the translator with func_801A9CE0's override
// temporarily removed (so these five stop being classified as state-free-mismatched and translate
// normally) - tried once this session; the five still didn't translate for a still-undiagnosed
// reason (not the state-free ABI issue after all, or a second contributing cause), and chasing that
// further was out of scope for the boot investigation this stub unblocks. Until that's resolved,
// these are honest "not implemented" stubs (this project's existing pattern - see
// runtime/src/hle/gx/gx_fatal_stubs.cpp) rather than a guess: if the guest actually reaches one, it
// aborts loudly with the address instead of running with a missing or fabricated body.
#include "hle_stubs.h"
#include "runtime_log.h"
#include "memory.h"
#include "abi_bridge.h"

namespace {
[[noreturn]] void HaltMissingStateFreeBody(uint32_t addr) {
    RT_LOGF(RT_TAG_RUNTIME,
            "unimplemented NSMBW entry point: func_%08X.\n"
            "[nsmbw] This function's real translated body was lost when a full shard\n"
            "[nsmbw] regeneration replaced the hand-patched shard it used to live in (see\n"
            "[nsmbw] nsmbw_func_801af900_registration.cpp for the full history). It needs to be\n"
            "[nsmbw] recovered and given its own native-override body here.\n",
            addr);
    std::fflush(stderr);
    char message[512]{};
    std::snprintf(message, sizeof(message),
                  "func_%08X has no implementation bound to it (its real body was lost to a shard "
                  "regeneration), so the runtime stopped rather than run with missing guest code.",
                  addr);
    ShowRuntimeFatalPopup("the game called an unimplemented function", message);
    std::abort();
}
} // namespace

// func_801AF900 is confirmed reached during normal boot (proven live, not speculative): a large
// float-heavy color/curve table builder (cubic polynomial evaluation over several 0x20-byte
// tables allocated via 0x801AC060), called once during graphics init. Hand-porting its real body
// under time pressure isn't the smallest correct fix; a no-op is: whatever table it would have
// filled in stays zero-initialized, which is a cosmetic risk (a color ramp reads as black/default)
// but not a correctness blocker for reaching a frame at all. If a later frame looks visually wrong
// in a way that traces back to this table, that's the signal to actually recover the real body.
extern "C" void func_801AF900_noop_stub(CpuContext* ctx) { (void)ctx; }

#define NSMBW_MISSING_STATEFREE_BODY_STUB(addr) \
    extern "C" void func_##addr##_missing_stub(CpuContext* ctx) { (void)ctx; HaltMissingStateFreeBody(0x##addr); }

PPC_NATIVE_OVERRIDE_VOID(801AF900, func_801AF900_noop_stub, (CpuContext* ctx), (ctx));
NSMBW_MISSING_STATEFREE_BODY_STUB(801AC980) PPC_NATIVE_OVERRIDE_VOID(801AC980, func_801AC980_missing_stub, (CpuContext* ctx), (ctx));

// func_801B8B20 is confirmed reached during normal boot (proven live): a small function-pointer
// table walker - `mtctr r12; bctrl` over a NULL-terminated table at guest 0x802EDFC0, then an
// unconditional call to func_801A9CE0 (already correctly translated/overridden elsewhere - a real
// fix for a translator decode bug, see nsmbw_func_801a9ce0.cpp). Unlike func_801AF900's cosmetic
// color table, this one's table entries are arbitrary init callbacks, so a no-op risks skipping
// something that matters. Its control flow is trivial enough to reimplement faithfully instead of
// guessing whether skipping it is safe.
extern "C" void func_801A9CE0(CpuContext* ctx);
extern "C" void func_801B8B20_table_walker(CpuContext* ctx)
{
    for (uint32_t entry = 0x802EDFC0u;; entry += 4u) {
        const uint32_t fn = ::Memory::Read32(entry);
        if (fn == 0u) break;
        InvokeIndirectCpu(fn, ctx);
    }
    InvokeIndirectCpu(0x801A9CE0u, ctx);
}
PPC_NATIVE_OVERRIDE_VOID(801B8B20, func_801B8B20_table_walker, (CpuContext* ctx), (ctx));

NSMBW_MISSING_STATEFREE_BODY_STUB(801AD620) PPC_NATIVE_OVERRIDE_VOID(801AD620, func_801AD620_missing_stub, (CpuContext* ctx), (ctx));
NSMBW_MISSING_STATEFREE_BODY_STUB(801AD9E0) PPC_NATIVE_OVERRIDE_VOID(801AD9E0, func_801AD9E0_missing_stub, (CpuContext* ctx), (ctx));
