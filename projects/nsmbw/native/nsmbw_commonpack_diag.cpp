// Diagnostic (not a fix) for the "missing indirect jump target 0x819E0228" fatal crash: identifies
// exactly which object/profile/virtual-slot the bad pointer-to-member-function dispatch belongs
// to, and makes the crash survivable so the run can keep going and reveal more.
//
// The crash happens inside fBase_c::commonPack() (0x80161E00, confirmed real address from
// projects/nsmbw/function_map.txt) via __ptmf_scall (0x802DCEEC, a CodeWarrior compiler-runtime
// helper for calling through a pointer-to-member-function, NOT game code) - `lr` at crash time
// pointed at commonPack's return address for its *first* of three such calls, i.e. `(this->*
// preFunc)()` (source/dol/framework/f_base.cpp:69).
//
// __ptmf_scall's own disassembly gives the exact 3-word PTMF layout this compiler uses (verified,
// not assumed):
//   word0 = this-adjustment (added to the object pointer first - multiple-inheritance support)
//   word1 = vtable slot offset if >= 0 (virtual call), or a sentinel < 0 meaning "direct call"
//   word2 = direct function pointer (if word1 < 0), else the object offset to read the vtable
//           pointer from
// fBase_c::fBase_c() (0x80161C10) confirms fBase_c's own vtable pointer lives at object+0x60 (via
// `stw <vtable literal>, 0x60(r3)`) - not at +0x0, matching the same "data before vtable pointer"
// layout this compiler already showed for DynamicModuleControlBase (see
// nsmbw_preseeded_rel_link.cpp) - and mProfName at object+0x8 (`sth r0,8(r3)` copying
// m_tmpCtProfName), matching source/dol/framework/f_base.cpp:15-18's declaration order
// (mUniqueID@0x0, mParam@0x4, mProfName@0x8).
//
// This override reimplements commonPack's exact logic (source/dol/framework/f_base.cpp:66-86), but
// resolves each of the three PTMF calls itself first and checks the result against
// TranslatedFunctionRegistry before jumping - if resolution lands on an address that isn't a
// registered function, it dumps full diagnostics (this pointer, mProfName, vtable pointer, the raw
// PTMF words, the bad resolved address) instead of crashing, and substitutes a harmless return
// value so the frame that would have crashed can unwind normally and the run keeps going.
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"

#include <cstdio>

namespace {
constexpr uint32_t kProfNameOffset = 0x8u;
constexpr uint32_t kFBaseVtableOffset = 0x60u;

// Mirrors __ptmf_scall (0x802DCEEC) exactly, but stops short of jumping so the caller can check
// validity first.
struct PtmfResolution {
    uint32_t adjustedThis;
    int32_t word0;
    int32_t word1;
    uint32_t word2;
    uint32_t target;
};

PtmfResolution ResolvePtmf(uint32_t thisPtr, uint32_t ptmfAddr)
{
    PtmfResolution r{};
    r.word0 = static_cast<int32_t>(Memory::Read32(ptmfAddr + 0));
    r.word1 = static_cast<int32_t>(Memory::Read32(ptmfAddr + 4));
    r.word2 = Memory::Read32(ptmfAddr + 8);
    r.adjustedThis = thisPtr + static_cast<uint32_t>(r.word0);

    if (r.word1 < 0) {
        r.target = r.word2;
    } else {
        uint32_t vtable = 0;
        try {
            vtable = Memory::Read32(r.adjustedThis + r.word2);
            r.target = Memory::Read32(vtable + static_cast<uint32_t>(r.word1));
        } catch (const Memory::AccessViolation&) {
            r.target = 0;
        }
    }
    return r;
}

// Calls through a validated PTMF resolution, or logs and returns a safe default if the resolved
// target isn't a registered translated function. `label` and `stateArg` (only used for postFunc,
// -1 otherwise) are purely for the diagnostic message.
uint32_t SafeCallPtmf(uint32_t thisPtr, uint32_t ptmfAddr, const char* label, int32_t stateArg, bool& ok)
{
    const PtmfResolution res = ResolvePtmf(thisPtr, ptmfAddr);
    ok = TranslatedFunctionRegistry::FindByAddressPtr(res.target) != nullptr;

    if (!ok) {
        uint16_t profName = 0xFFFFu;
        uint32_t vtablePtr = 0;
        try {
            profName = Memory::Read16(thisPtr + kProfNameOffset);
            vtablePtr = Memory::Read32(thisPtr + kFBaseVtableOffset);
        } catch (const Memory::AccessViolation&) {
        }
        std::fprintf(stderr,
            "[nsmbw][diag] commonPack: BAD PTMF for %s (this=0x%08X mProfName=0x%04X "
            "vtablePtr@+0x60=0x%08X ptmf={adjust=%d slot=%d word2=0x%08X} adjustedThis=0x%08X "
            "resolvedTarget=0x%08X NOT REGISTERED - substituting safe return)\n",
            label, thisPtr, profName, vtablePtr, res.word0, res.word1, res.word2,
            res.adjustedThis, res.target);
        std::fflush(stderr);
        return 0;
    }

    auto& cpu = GetPersistentCpuContext();
    cpu.gpr[3] = res.adjustedThis;
    if (stateArg >= 0) {
        cpu.gpr[4] = static_cast<uint32_t>(stateArg);
    }
    InvokeIndirectCpu(res.target, &cpu);
    return cpu.gpr[3];
}
}

extern "C" uint32_t CommonPack_Diag_80161E00(uint32_t thisPtr, uint32_t doFuncPtmf,
                                             uint32_t preFuncPtmf, uint32_t postFuncPtmf)
{
    bool ok = true;
    int32_t result = static_cast<int32_t>(SafeCallPtmf(thisPtr, preFuncPtmf, "preFunc", -1, ok));

    int32_t state; // MAIN_STATE_e: CANCELED=0, ERROR=1, SUCCESS=2, WAITING=3
    if (result) {
        result = static_cast<int32_t>(SafeCallPtmf(thisPtr, doFuncPtmf, "doFunc", -1, ok));
        if (result == 0) {       // NOT_READY
            state = 3;            // WAITING
        } else if (result == 1) { // SUCCEEDED
            state = 2;             // SUCCESS
        } else {
            state = 1;              // ERROR
        }
    } else {
        state = 0; // CANCELED
    }

    SafeCallPtmf(thisPtr, postFuncPtmf, "postFunc", state, ok);
    return static_cast<uint32_t>(result);
}

PPC_NATIVE_OVERRIDE(80161E00, CommonPack_Diag_80161E00, uint32_t,
                    (uint32_t thisPtr, uint32_t doFuncPtmf, uint32_t preFuncPtmf, uint32_t postFuncPtmf),
                    (thisPtr, doFuncPtmf, preFuncPtmf, postFuncPtmf));
