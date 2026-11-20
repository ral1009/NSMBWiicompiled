// Option: no freeze when a player dies (F10 > Advanced > "Power-up freeze", same switch as the
// power-up one; nsmbw_power_up_freeze.cpp explains stopOther).
//
// dAcPy_c::stopOtherDownDemo (0x8013DA30) runs as a player starts the death demo. For every death
// except the last player's (m_7c, which ends the level and sets mExecStopReq itself) it calls
// daPlBase_c::stopOther (0x8004E050), which freezes every other actor for the death jingle. With the
// option off that call is skipped, so the level keeps moving. Structural copy of
// build/nsmbw/functions/func_8013DA30.cpp (2026-11-18); only the lines marked EDIT differ. The
// function is reached through calls, not inlined (not a leaf), so an override at its address runs.
#include "nsmbw_va.h"
#include <cstdint>
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include "recomp_mod_loader.h"

namespace settings_overlay::settings { bool PowerUpFreeze() noexcept; }

extern "C" void NsmbwDeathFreeze_StopOtherDownDemo_8013DA30(CpuContext* MKW_RESTRICT ctx)
{
    const bool freeze = settings_overlay::settings::PowerUpFreeze();
    uint32_t cr0_0 = 0;
    uint32_t r0_addze_src_0 = 0;
    uint32_t r0_addze_src_1 = 0;
    uint32_t r0_addze_src_2 = 0;
    uint32_t r0_addze_src_3 = 0;
    uint32_t r0_addze_src_4 = 0;
    uint32_t r0_ca_0 = 0;
    uint32_t r0_ca_1 = 0;
    uint32_t r0_ca_2 = 0;
    uint32_t r0_ca_3 = 0;
    uint32_t r0_ca_4 = 0;
    uint32_t r0_rot_0 = 0;
    uint32_t r0_rot_1 = 0;
    uint32_t r0_rot_2 = 0;
    uint32_t r0_rot_3 = 0;
    uint32_t r0_rot_4 = 0;
    uint32_t r3_rot_0 = 0;
    uint32_t r3_rot_1 = 0;
    uint32_t r3_rot_2 = 0;
    uint32_t r3_rot_3 = 0;
    uint32_t r3_rot_4 = 0;
    uint32_t r4_rot_0 = 0;
    uint32_t r4_rot_1 = 0;
    uint32_t r4_rot_2 = 0;
    uint32_t r4_rot_3 = 0;
    uint32_t r4_rot_4 = 0;
    uint32_t r5_rot_0 = 0;
    uint32_t r5_rot_1 = 0;
    uint32_t r5_rot_2 = 0;
    uint32_t r5_rot_3 = 0;
    uint32_t r5_rot_4 = 0;
    uint32_t r6_rot_0 = 0;
    uint32_t r6_rot_1 = 0;
    uint32_t r6_rot_2 = 0;
    uint32_t r6_rot_3 = 0;
    uint32_t r6_rot_4 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r7 = ctx->gpr[7];
    uint32_t r12 = ctx->gpr[12];
    uint32_t r13 = ctx->gpr[13];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_8013DA30;

loc_8013DA30:
{
    MemoryInline::FlatWriteRam32((r1 + -16), r1);
    r1 = (r1 + -16);
    r0 = ctx->lr;
    MemoryInline::FlatWriteRam32((r1 + 20), r0);
    MemoryInline::FlatWriteRam32((r1 + 12), r31);
    MemoryInline::FlatWriteRam32((r1 + 8), r30);
    r30 = r3;
    r12 = MemoryInline::FlatRead32((r3 + 96));
    r12 = MemoryInline::FlatRead32((r12 + 224));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x8013DA58u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    ctx->xer = xer;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_8013DA5C:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8013DB18;
    }
}

loc_8013DA60:
{
    r0 = MemoryInline::FlatRead32((r30 + 5460));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_8013DA68:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8013DA78;
    }
}

loc_8013DA6C:
{
    if (freeze) { // EDIT: stopOther only when the freeze is on
    r3 = r30;
    ctx->lr = NSMBW_VA(0x8013DA74u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    ctx->xer = xer;
    InvokeDirectCpu<NSMBW_VA(0x8004E050u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    }
    goto loc_8013DB18;
}

loc_8013DA78:
{
    r0 = MemoryInline::FlatRead8((r30 + 909));
    r30 = 0;
    r3 = MemoryInline::FlatRead32((r13 + -NSMBW_SDA13(23052)));
    r0 = (static_cast<int32_t>(static_cast<int8_t>(r0)));
    MemoryInline::FlatWrite32((r3 + 132), r0);
    r0 = MemoryInline::FlatRead8((r13 + -NSMBW_SDA13(22952)));
    r0 = (r0 | 15);
    MemoryInline::FlatWrite8((r13 + -NSMBW_SDA13(22952)), static_cast<uint8_t>(r0));
}

loc_8013DA98:
{
    r3 = r30;
    ctx->lr = NSMBW_VA(0x8013DAA0u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->cr = cr;
    ctx->xer = xer;
    InvokeDirectCpu<NSMBW_VA(0x8005F900u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    cr = ctx->cr;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_8013DAA4:
{
    r31 = r3;
    if (((cr & 0x20000000u) != 0)) {
        goto loc_8013DAE4;
    }
}

loc_8013DAAC:
{
    r4 = 83;
    // inline leaf 0x80056CF0 (17 guest instruction(s))
    {
        const uint32_t ppcCarryValue = static_cast<uint32_t>(r4);
        const uint32_t ppcCarryShift = static_cast<uint32_t>(5) & 0x3Fu;
        const bool ppcCarryNegative = (ppcCarryValue & NSMBW_VA(0x80000000u)) != 0;
        const uint32_t ppcCarry = ppcCarryShift == 0 ? 0u : (ppcCarryShift >= 32 ? (ppcCarryNegative && ppcCarryValue != 0 ? 1u : 0u) : (ppcCarryNegative && (ppcCarryValue & ((1u << ppcCarryShift) - 1u)) != 0 ? 1u : 0u));
        xer = (xer & 0xDFFFFFFFu) | (ppcCarry << 29);
    }
    r0 = (static_cast<int32_t>(r4) >> 5);
    r6_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r4), static_cast<uint32_t>(27));
    r6 = (r6_rot_2 & -134217728);
    r5_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r4), static_cast<uint32_t>(1));
    r5 = (r5_rot_2 & 1);
    r7 = 1;
    r0_addze_src_2 = r0;
    r0_ca_2 = (xer >> 29) & 1u;
    r0 = (r0_addze_src_2 + r0_ca_2);
    {
        const uint64_t ppcCarryWide = static_cast<uint64_t>(static_cast<uint32_t>(r0_addze_src_2)) + static_cast<uint64_t>(static_cast<uint32_t>(0)) + (static_cast<uint64_t>(static_cast<uint32_t>(r0_ca_2)) & 1u);
        xer = (xer & 0xDFFFFFFFu) | (static_cast<uint32_t>((ppcCarryWide >> 32) & 1u) << 29);
    }
    r4 = (r6 - r5);
    r0_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(2));
    r0 = (r0_rot_2 & -4);
    r4_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r4), static_cast<uint32_t>(5));
    r4 = (r4_rot_2 & -1);
    r3 = (r3 + r0);
    r4 = (r4 + r5);
    r0 = MemoryInline::FlatRead32((r3 + 4116));
    r3 = PPC_Slw(static_cast<uint32_t>(r7), static_cast<uint32_t>(r4));
    r3 = (r3 & r0);
    r0 = (0 - r3);
    r0 = (r0 | r3);
    r3_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(1));
    r3 = (r3_rot_2 & 1);
    // end of inlined leaf 0x80056CF0
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_8013DAB8:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8013DAE4;
    }
}

loc_8013DABC:
{
    r12 = MemoryInline::FlatRead32((r31 + 96));
    r3 = r31;
    r12 = MemoryInline::FlatRead32((r12 + 224));
    ctr = r12;
    ctx->lr = NSMBW_VA(0x8013DAD0u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    ctx->xer = xer;
    InvokeIndirectCpu(ctr, ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_8013DAD4:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8013DAE4;
    }
}

loc_8013DAD8:
{
    r0 = MemoryInline::FlatRead8((r31 + 910));
    r0 = (r0 & 253);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_8013DAE0:
{
    MemoryInline::FlatWrite8((r31 + 910), static_cast<uint8_t>(r0));
}

loc_8013DAE4:
{
    r3 = r30;
    ctx->lr = NSMBW_VA(0x8013DAECu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    ctx->xer = xer;
    InvokeDirectCpu<NSMBW_VA(0x8005FA60u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r12 = ctx->gpr[12];
    r13 = ctx->gpr[13];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_8013DAF0:
{
    if ((static_cast<int32_t>(r3) == static_cast<int32_t>(0))) {
        goto loc_8013DB00;
    }
}

loc_8013DAF4:
{
    r0 = MemoryInline::FlatRead8((r3 + 910));
    r0 = (r0 & 253);
}

loc_8013DAFC:
{
    MemoryInline::FlatWrite8((r3 + 910), static_cast<uint8_t>(r0));
}

loc_8013DB00:
{
    r30 = (r30 + 1);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r30), static_cast<int32_t>(4));
}

loc_8013DB08:
{
    if (((cr & NSMBW_VA(0x80000000u)) != 0)) {
        goto loc_8013DA98;
    }
}

loc_8013DB0C:
{
    r0 = MemoryInline::FlatRead8((r13 + -NSMBW_SDA13(22952)));
    r0 = (r0 | 13);
    MemoryInline::FlatWrite8((r13 + -NSMBW_SDA13(22952)), static_cast<uint8_t>(r0));
}

loc_8013DB18:
{
    r0 = MemoryInline::FlatRead32((r1 + 20));
    r31 = MemoryInline::FlatRead32((r1 + 12));
    r30 = MemoryInline::FlatRead32((r1 + 8));
    ctx->lr = r0;
    r1 = (r1 + 16);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    ctx->xer = xer;
    return;
}

}

// RECOMP_GUEST_ABI gpr_read=0xFFFFFFFF gpr_write=0xFFFFFFFF gpr_return=0x00000018 fpr_read=0xFFFFFFFF fpr_write=0xFFFFFFFF fpr_return=0x00000002 cr_read=0xFF cr_write=0xFF xer_read=1 xer_write=1 fence=1
// RECOMP_REGISTRATION base 0x8013DA30 func_8013DA30 preserves=true fpr_mask=0x00000000

PPC_NATIVE_OVERRIDE_VOID(8013DA30, NsmbwDeathFreeze_StopOtherDownDemo_8013DA30, (CpuContext* ctx), (ctx));
