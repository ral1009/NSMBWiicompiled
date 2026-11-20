// Option: no freeze while a player changes size (F10 > Advanced > "Power-up freeze").
//
// In single player NSMBW stops every other actor for about a second while Mario grows or shrinks
// (a power-up, or damage). The decision is made in the power-change state, dAcPy_c (d_a_player.cpp):
//   0x80141080 executeChangeInit   first frame of a change: calls daPlBase_c::stopOther
//              (0x8004E050; sets dActor_c::mExecStopReq |= 0xF, the execute-stop request: every
//              actor whose kind is in that mask skips its update, the changing player and Yoshi are
//              taken out of it) and dAudio::pauseMove (0x8006A7D0, inlined here; pauses that
//              player's sound), then returns isPlayerGameStop().
//   0x80141460 executeChangeNormal every following frame of the flashing: returns isPlayerGameStop().
// isPlayerGameStop (0x8004E040) is bit 1 of dInfo_c::mGameFlag (0x8042A260). stopOther does
// nothing when that bit is clear, and a false return makes updateChange (0x80141600) let
// dAcPy_c::executeMain carry on, so the player keeps moving while flashing. So the game already has a
// no-freeze path; with the option off these copies take it whatever the bit says.
//
// Why here and not at isPlayerGameStop: the translator inlined that 3-instruction leaf into its
// callers (see the "inline leaf 0x8004E040" lines below and in 2 shards), so an override at its
// address would not run there; and the pipe, door, goal-pole and water-tank freezes use the same
// check and should stay. Both functions below are reached only through updateChange's member-function
// table (l_changeActionProc), so no caller can have inlined them.
//
// Structural copies of build/nsmbw/functions/func_80141080.cpp / func_80141460.cpp (made by script,
// 2026-11-18): every store and call is unchanged; only the lines marked EDIT differ, and with the
// option on (the default) they do exactly what the original does. initChangeInit (0x80141020) still
// runs playOther / pauseOffMove at the end of the change; both are harmless when nothing was stopped.
#include "nsmbw_va.h"  // NSMBW_VA / NSMBW_FUNC: guest addresses are PAL rev 1, mapped per version
#include <cstdint>
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include "recomp_mod_loader.h"

// Forward-declared instead of #include "settings_overlay.h" (it pulls in SDL3 headers, which are not
// on the native sources' include path; same as nsmbw_wiistrap_dismiss.cpp).
namespace settings_overlay::settings {
bool PowerUpFreeze() noexcept;
}

extern "C" void NsmbwPowerUpFreeze_ExecuteChangeInit_80141080(CpuContext* MKW_RESTRICT ctx)
{
    const bool freeze = settings_overlay::settings::PowerUpFreeze();
    uint32_t cr0_0 = 0;
    uint32_t r3_rot_0 = 0;
    uint32_t r3_rot_1 = 0;
    uint32_t r3_rot_2 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r12 = ctx->gpr[12];
    uint32_t r13 = ctx->gpr[13];
    uint32_t r31 = ctx->gpr[31];
    PPC_FPR f0 = ctx->fpr[0];
    PPC_FPR f1 = ctx->fpr[1];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_80141080;

loc_80141080:
{
    MemoryInline::FlatWriteRam32((r1 + -16), r1);
    r1 = (r1 + -16);
    r0 = ctx->lr;
    MemoryInline::FlatWriteRam32((r1 + 20), r0);
    MemoryInline::FlatWriteRam32((r1 + 12), r31);
    r31 = r3;
    r0 = MemoryInline::FlatRead32((r3 + 4240));
    r4 = MemoryInline::FlatRead32((r3 + 5344));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(r4));
}

loc_801410A0:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801410AC;
    }
}

loc_801410A4:
{
    r3 = 0;
    goto loc_80141114;
}

loc_801410AC:
{
    r12 = MemoryInline::FlatRead32((r3 + 96));
    r5 = 0;
    r12 = MemoryInline::FlatRead32((r12 + 228));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x801410C0u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    if (freeze) { // EDIT: stopOther only when the freeze is on
        r3 = r31;
        ctx->lr = NSMBW_VA(0x801410C8u);
        ctx->gpr[0] = r0;
        ctx->gpr[1] = r1;
        ctx->gpr[3] = r3;
        ctx->gpr[4] = r4;
        ctx->gpr[5] = r5;
        ctx->gpr[12] = r12;
        ctx->gpr[31] = r31;
        ctx->fpr[0] = f0;
        ctx->cr = cr;
        ctx->ctr = ctr;
        InvokeDirectCpu<NSMBW_VA(0x8004E050u)>(ctx);
        r0 = ctx->gpr[0];
        r1 = ctx->gpr[1];
        r3 = ctx->gpr[3];
        r4 = ctx->gpr[4];
        r5 = ctx->gpr[5];
        r12 = ctx->gpr[12];
        r13 = ctx->gpr[13];
        r31 = ctx->gpr[31];
        f0 = ctx->fpr[0];
        f1 = ctx->fpr[1];
        cr = ctx->cr;
        ctr = ctx->ctr;
        xer = ctx->xer;
    }
    if (freeze) { // EDIT: pause the player's sound only when the freeze is on
        r3 = MemoryInline::FlatRead8((r31 + 909));
        r3 = (static_cast<int32_t>(static_cast<int8_t>(r3)));
        // inline leaf 0x8006A7D0 (7 guest instruction(s))
        r0 = 1;
        r4 = MemoryInline::FlatRead8((r13 + -NSMBW_SDA13(22812)));
        r0 = PPC_Slw(static_cast<uint32_t>(r0), static_cast<uint32_t>(r3));
        r0 = (r0 & 255);
        r0 = (r4 | r0);
        MemoryInline::FlatWrite8((r13 + -NSMBW_SDA13(22812)), static_cast<uint8_t>(r0));
        // end of inlined leaf 0x8006A7D0
    }
    r3 = r31;
    r4 = 1;
    ctx->lr = NSMBW_VA(0x801410E0u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x801415A0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r0 = MemoryInline::FlatRead32((r31 + 4240));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(3));
}

loc_801410E8:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8014110C;
    }
}

loc_801410EC:
{
    r3 = r31;
    ctx->lr = NSMBW_VA(0x801410F4u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x801275B0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    f0.d = MemoryInline::FlatReadFloat32((r31 + 236));
    SetCRFloatResident(cr, 0, f0.d, f1.d);
}

loc_801410FC:
{
    if (((cr & 0x40000000u) == 0)) {
        goto loc_80141104;
    }
}

loc_80141100:
{
    MemoryInline::FlatWriteFloat32((r31 + 236), f1.d);
}

loc_80141104:
{
    r3 = r31;
    ctx->lr = NSMBW_VA(0x8014110Cu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x8012E6E0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_8014110C:
{
    r3 = r31;
    // inline leaf 0x8004E040 (3 guest instruction(s))
    r0 = MemoryInline::FlatRead32((r13 + -NSMBW_SDA13(22304)));
    r3_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(31));
    r3 = freeze ? (r3_rot_1 & 1) : 0; // EDIT: was (r3_rot_1 & 1), isPlayerGameStop()
    // end of inlined leaf 0x8004E040
}

loc_80141114:
{
    r0 = MemoryInline::FlatRead32((r1 + 20));
    r31 = MemoryInline::FlatRead32((r1 + 12));
    ctx->lr = r0;
    r1 = (r1 + 16);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->cr = cr;
    ctx->ctr = ctr;
    return;
}

}

extern "C" void NsmbwPowerUpFreeze_ExecuteChangeNormal_80141460(CpuContext* MKW_RESTRICT ctx)
{
    const bool freeze = settings_overlay::settings::PowerUpFreeze();
    uint32_t cr0_0 = 0;
    uint32_t r3_rot_0 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r12 = ctx->gpr[12];
    uint32_t r13 = ctx->gpr[13];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_80141460;

loc_80141460:
{
    MemoryInline::FlatWriteRam32((r1 + -16), r1);
    r1 = (r1 + -16);
    r0 = ctx->lr;
    MemoryInline::FlatWriteRam32((r1 + 20), r0);
    MemoryInline::FlatWriteRam32((r1 + 12), r31);
    MemoryInline::FlatWriteRam32((r1 + 8), r30);
    r30 = r3;
    ctx->lr = NSMBW_VA(0x8014147Cu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x80141130u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
    r0 = MemoryInline::FlatRead8((r30 + 5438));
    r31 = MemoryInline::FlatRead32((r30 + 4240));
    r0 = (static_cast<int32_t>(static_cast<int8_t>(r0)));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801414E4;
    }
}

loc_8014148C:
{
    r0 = (r3 & 1);
}

loc_80141490:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_80141498;
    }
}

loc_80141494:
{
    r31 = MemoryInline::FlatRead32((r30 + 5340));
}

loc_80141498:
{
}

loc_8014149C:
{
    if ((static_cast<int32_t>(r31) == static_cast<int32_t>(0))) {
        goto loc_801414A8;
    }
}

loc_801414A0:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r31), static_cast<int32_t>(3));
}

loc_801414A4:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801414F8;
    }
}

loc_801414A8:
{
    r3 = MemoryInline::FlatRead32((r30 + 4240));
}

loc_801414B0:
{
    if ((static_cast<int32_t>(r3) != static_cast<int32_t>(0))) {
        goto loc_801414C0;
    }
}

loc_801414B4:
{
    r0 = MemoryInline::FlatRead32((r30 + 5340));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(3));
}

loc_801414BC:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801414D4;
    }
}

loc_801414C0:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(3));
}

loc_801414C4:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801414DC;
    }
}

loc_801414C8:
{
    r0 = MemoryInline::FlatRead32((r30 + 5340));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_801414D0:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801414DC;
    }
}

loc_801414D4:
{
    r31 = 0;
    goto loc_801414F8;
}

loc_801414DC:
{
    r31 = 1;
    goto loc_801414F8;
}

loc_801414E4:
{
    r3 = r30;
    r4 = 0;
    ctx->lr = NSMBW_VA(0x801414F0u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<NSMBW_VA(0x801415A0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r0 = 8;
    MemoryInline::FlatWrite32((r30 + 4216), r0);
}

loc_801414F8:
{
    r3 = MemoryInline::FlatRead32((r30 + 10852));
    r4 = r31;
    r12 = MemoryInline::FlatRead32(r3);
    r12 = MemoryInline::FlatRead32((r12 + 48));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x80141510u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r31), static_cast<int32_t>(2));
}

loc_80141514:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80141524;
    }
}

loc_80141518:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r31), static_cast<int32_t>(6));
}

loc_8014151C:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80141540;
    }
}

loc_80141520:
{
    goto loc_8014155C;
}

loc_80141524:
{
    r3 = MemoryInline::FlatRead32((r30 + 10852));
    r4 = 1;
    r12 = MemoryInline::FlatRead32(r3);
    r12 = MemoryInline::FlatRead32((r12 + 52));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x8014153Cu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_80141574;
}

loc_80141540:
{
    r3 = MemoryInline::FlatRead32((r30 + 10852));
    r4 = 2;
    r12 = MemoryInline::FlatRead32(r3);
    r12 = MemoryInline::FlatRead32((r12 + 52));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x80141558u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_80141574;
}

loc_8014155C:
{
    r3 = MemoryInline::FlatRead32((r30 + 10852));
    r4 = 0;
    r12 = MemoryInline::FlatRead32(r3);
    r12 = MemoryInline::FlatRead32((r12 + 52));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x80141574u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_80141574:
{
    r3 = r30;
    // inline leaf 0x8004E040 (3 guest instruction(s))
    r0 = MemoryInline::FlatRead32((r13 + -NSMBW_SDA13(22304)));
    r3_rot_0 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(31));
    r3 = freeze ? (r3_rot_0 & 1) : 0; // EDIT: was (r3_rot_0 & 1), isPlayerGameStop()
    // end of inlined leaf 0x8004E040
    r0 = MemoryInline::FlatRead32((r1 + 20));
    r31 = MemoryInline::FlatRead32((r1 + 12));
    r30 = MemoryInline::FlatRead32((r1 + 8));
    ctx->lr = r0;
    r1 = (r1 + 16);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(80141080, NsmbwPowerUpFreeze_ExecuteChangeInit_80141080, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(80141460, NsmbwPowerUpFreeze_ExecuteChangeNormal_80141460, (CpuContext* ctx), (ctx));
