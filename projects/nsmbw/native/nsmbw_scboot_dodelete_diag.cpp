// Diagnostic (not a fix), continuing past nsmbw_check_child_create_diag.cpp: that hook proved
// fBase_c::checkChildProcessCreateState() is called exactly once during the whole run (blocked on
// dWiiStrap_c for one frame, then clear) and never again - so the live OPAQUE-forever stall is NOT
// a second ROOT_DISABLE_EXECUTE occurrence on a new scene. New theory, from re-reading
// dScBoot_c::preExecute()/execute() (NSMBW-Decomp source/dol/bases/d_s_boot.cpp): once
// dScRestartCrsin_c::startTitle() sets dScene_c::m_nextScene, dScene_c::preExecute() (d_scene.cpp:
// 87-95) returns NOT_READY every frame and starts a fade-out, then calls deleteRequest() once the
// fader reaches OPAQUE - moving dScBoot_c from executePack() to deletePack(). From then on,
// dScBoot_c::doDelete() (not execute()) is the only thing still calling myBackGround_Phase - a
// 32-step sPhase_c phase list covering dylink loading of d_bases/d_enemies/d_en_boss, sound
// loading, resource sync, and the four "Wipe" transition layouts. If this list stalls, doDelete()
// returns NOT_READY forever, dScBoot_c never finishes deleting, dScRestartCrsin_c never gets
// created, and the fader sits at OPAQUE forever with nothing left to advance it - matching exactly
// what nsmbw_wiistrap_create_diag.cpp's live run showed (mStatus=0, m_isAutoFadeIn=1, zero further
// startFadeIn calls for 900+ frames).
//
// All addresses/offsets here are read directly off the real disassembly of
// dScBoot_c::doDelete() (0x8015C600) and dScBoot_c::execute() (0x8015C6E0), both confirmed real
// addresses from projects/nsmbw/function_map.txt:
//   addi r3, r13, -0x5348   ; &myBackGround_Phase - this project's sda_base is 0x8042f980
//                           ; (projects/nsmbw/nsmbw.yml), so the phase object's real address is
//                           ; 0x8042f980 - 0x5348 = 0x8042A638.
//   bl 0x8015f760           ; sPhase_c::callMethod(this=&myBackGround_Phase, arg=dScBoot_c*)
//   cmpwi r3, 2; beq ...    ; sPhase_c::DONE == 2
// then, only once DONE:
//   bl 0x80108020 / bl 0x80108260     ; no register setup before either call -> both take no
//                                     ; arguments (BootComplete()-equivalent housekeeping)
//   stb 0, -0x7700(r13)               ; a reset-related flag at 0x8042f980-0x7700 = 0x80428280
//   li r3,0x1e; bl 0x800e2040         ; dScene_c::setFadeInFrame(30)
//   li r3,0x1e; bl 0x800e2050         ; dScene_c::setFadeOutFrame(30)
//   li r3,0x2c0; bl 0x8008f610        ; dDyl::Unlink(YES_NO_WINDOW); r3==0 -> NOT_READY
//   li r3,0x2e5; bl 0x8008f610        ; dDyl::Unlink(CONTROLLER_INFORMATION); r3==0 -> NOT_READY
//   li r3,0; bl 0x800e5170            ; dSystem::createEffectManagerPhase2(nullptr)
//   li r3,1                           ; return SUCCEEDED
// This override reimplements that exact sequence (delegating every sub-step to the real,
// untouched guest functions via InvokeIndirectCpu) so it is value-identical to the original -
// only the logging around the phase-list result is new.
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"

#include <cstdio>

namespace {
constexpr uint32_t kMyBackGroundPhaseAddr = 0x8042A638u; // sda_base(0x8042f980) - 0x5348
constexpr uint32_t kPhaseCallMethodAddr = 0x8015F760u;   // sPhase_c::callMethod
constexpr uint32_t kPhaseDone = 2u;                      // sPhase_c::DONE

constexpr uint32_t kResetFlagAddr = 0x80428280u;         // sda_base - 0x7700
constexpr uint32_t kBootCompleteAddr = 0x80108020u;
constexpr uint32_t kPostBootHousekeepingAddr = 0x80108260u;
constexpr uint32_t kSetFadeInFrameAddr = 0x800E2040u;
constexpr uint32_t kSetFadeOutFrameAddr = 0x800E2050u;
constexpr uint32_t kDylUnlinkAddr = 0x8008F610u;
constexpr uint32_t kCreateEffectManagerPhase2Addr = 0x800E5170u;
constexpr uint32_t kYesNoWindowProfile = 0x2C0u;
constexpr uint32_t kControllerInfoProfile = 0x2E5u;

void CallNoArgs(uint32_t addr)
{
    auto& cpu = GetPersistentCpuContext();
    InvokeIndirectCpu(addr, &cpu);
}

uint32_t CallOneArg(uint32_t addr, uint32_t arg)
{
    auto& cpu = GetPersistentCpuContext();
    cpu.gpr[3] = arg;
    InvokeIndirectCpu(addr, &cpu);
    return cpu.gpr[3];
}
}

extern "C" uint32_t DScBootDoDelete_Diag_8015C600(uint32_t thisPtr)
{
    auto& cpu = GetPersistentCpuContext();
    cpu.gpr[3] = kMyBackGroundPhaseAddr;
    cpu.gpr[4] = thisPtr;
    InvokeIndirectCpu(kPhaseCallMethodAddr, &cpu);
    const uint32_t phaseResult = cpu.gpr[3];

    static uint64_t callCount = 0;
    ++callCount;
    if (callCount <= 15 || (callCount % 60) == 0) {
        std::fprintf(stderr,
            "[nsmbw][diag] dScBoot_c::doDelete() call #%llu myBackGround_Phase.callMethod()=%u "
            "(2=DONE) -> %s\n",
            static_cast<unsigned long long>(callCount), phaseResult,
            phaseResult == kPhaseDone ? "proceeding to finish" : "NOT_READY");
        std::fflush(stderr);
    }

    if (phaseResult != kPhaseDone) {
        return 0; // NOT_READY - the phase list hasn't finished yet
    }

    std::fprintf(stderr, "[nsmbw][diag] dScBoot_c::doDelete(): phase list DONE, finishing boot\n");
    std::fflush(stderr);

    CallNoArgs(kBootCompleteAddr);
    CallNoArgs(kPostBootHousekeepingAddr);
    Memory::Write8(kResetFlagAddr, 0);
    CallOneArg(kSetFadeInFrameAddr, 30);
    CallOneArg(kSetFadeOutFrameAddr, 30);

    if (CallOneArg(kDylUnlinkAddr, kYesNoWindowProfile) == 0) {
        return 0; // NOT_READY
    }
    if (CallOneArg(kDylUnlinkAddr, kControllerInfoProfile) == 0) {
        return 0; // NOT_READY
    }
    CallOneArg(kCreateEffectManagerPhase2Addr, 0);

    return 1; // SUCCEEDED
}

PPC_NATIVE_OVERRIDE(8015C600, DScBootDoDelete_Diag_8015C600, uint32_t, (uint32_t thisPtr), (thisPtr));
