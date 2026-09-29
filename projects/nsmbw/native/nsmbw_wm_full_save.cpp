// Full save from the world-map menu before the game is beaten (no Quick Save).
//
// NSMBW's world-map menu (dCourseSelectMenu_c / dCourseSelectManager_c, d_basesNP) offers
// "Quick Save" until the final boss is beaten and "Save" afterwards. The choice is made in exactly
// two functions, both reading the permanent save slot's dMj2dGame_c::mGameCompletion (+2) and
// testing FINAL_BOSS_BEATEN (bit 0x02, d_mj2d_data.hpp) after an inlined getSaveGame(-1)
// (0x800E0470):
//   0x8077AA10  menu open: sets the save button's text - message 7 (MSG_WM_QUICK_SAVE) unless the
//               bit is set, then 4 (MSG_WM_SAVE); group 2 = BMG_CATEGORY_WORLD_MAP
//               (message_list.h). Passed to 0x800C9B50 for both text boxes (+608, +612).
//   0x8092FC30  item selected (index at +1360): for item 2 it changes state to the quick-save
//               chain (dCourseSelectManager_c::StateID_InterruptSave..., "interrupt" = suspend and
//               quit to the title) unless the bit is set, then to the full StateID_Save... chain.
// Every other reader of that bit only draws the file-select stars (0x80796080, 0x80796250,
// 0x8077D9A0) or sets it after World 8 (0x801028D0), so it is left alone: the saved flag keeps
// meaning "final boss beaten".
//
// Both overrides are structural copies of the translated bodies
// (build/nsmbw-d_basesNP/functions/func_<addr>.cpp; the same approach as nsmbw_exi_imm_wait.cpp):
// every branch, store and sub-call is unchanged. The ONLY edit in each is the line marked
// "EDIT", which makes the bit test read as "beaten", so the menu takes the branch the game itself
// takes after the final boss. Copying rather than reimplementing keeps every store the originals
// make (the pane flags at +187, the state writes), so no guest bookkeeping is dropped.
// These are the first overrides at REL addresses; the REL manifests share this native directory
// (nsmbw-d_basesNP.yml), so the REL translation leaves both addresses to these.
#include <cstdint>
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include "recomp_mod_loader.h"

extern "C" void NsmbwWmMenuSaveLabel_8077AA10(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r7 = ctx->gpr[7];
    uint32_t r29 = ctx->gpr[29];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t xer = ctx->xer;

    goto loc_8077AA10;

loc_8077AA10:
{
    MemoryInline::FlatWriteRam32((r1 + -32), r1);
    r1 = (r1 + -32);
    r0 = ctx->lr;
    MemoryInline::FlatWriteRam32((r1 + 36), r0);
    MemoryInline::FlatWriteRam32((r1 + 28), r31);
    MemoryInline::FlatWriteRam32((r1 + 24), r30);
    MemoryInline::FlatWriteRam32((r1 + 20), r29);
    r29 = r3;
    r0 = MemoryInline::FlatRead8((r3 + 624));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_8077AA34:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_8077AA40;
    }
}

loc_8077AA38:
{
    r3 = 1;
    goto loc_8077AB4C;
}

loc_8077AA40:
{
    ctx->lr = 0x8077AA44u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<0x8077AB70u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_8077AA48:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8077AA54;
    }
}

loc_8077AA4C:
{
    r3 = 0;
    goto loc_8077AB4C;
}

loc_8077AA54:
{
    r3 = 0x80430000u;
    r4 = -1;
    r3 = MemoryInline::FlatRead32((r3 + -23776));
    // inline leaf 0x800E0470 (10 guest instruction(s))
}

loc_inl0_0x800E0470:
{
    r0 = (static_cast<int32_t>(static_cast<int8_t>(r4)));
}

loc_inl0_0x800E0478:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(-1))) {
        goto loc_inl0_0x800E0484;
    }
}

loc_inl0_0x800E047C:
{
    r0 = MemoryInline::FlatRead8((r3 + 38));
    r4 = (static_cast<int32_t>(static_cast<int8_t>(r0)));
}

loc_inl0_0x800E0484:
{
    r0 = (static_cast<int32_t>(static_cast<int8_t>(r4)));
    r0 = (r0 * 2432);
    r3 = (r3 + r0);
    r3 = (r3 + 1728);
}

loc_inl0_cont_800E0470:
{
    // end of inlined leaf 0x800E0470
    r30 = r3;
    // inline leaf 0x800CDD50 (4 guest instruction(s))
    r3 = 0x80360000u;
    r3 = (r3 + -24676);
    r3 = MemoryInline::FlatRead32((r3 + 24));
    // end of inlined leaf 0x800CDD50
    r0 = MemoryInline::FlatRead8((r30 + 2));
    r31 = r3;
    r30 = 7;
    r0 = 2; // EDIT: was (r0 & 2), mGameCompletion & FINAL_BOSS_BEATEN - always "beaten"
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_8077AA7C:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_8077AA84;
    }
}

loc_8077AA80:
{
    r30 = 4;
}

loc_8077AA84:
{
    ctx->lr = 0x8077AA88u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<0x800B5500u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
    r0 = (r3 & 255);
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(1));
}

loc_8077AA90:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8077AABC;
    }
}

loc_8077AA94:
{
    r3 = MemoryInline::FlatRead32((r29 + 600));
    r0 = MemoryInline::FlatRead8((r3 + 187));
    r0 = (r0 & 254);
    r0 = (r0 | 1);
    MemoryInline::FlatWrite8((r3 + 187), static_cast<uint8_t>(r0));
    r3 = MemoryInline::FlatRead32((r29 + 604));
    r0 = MemoryInline::FlatRead8((r3 + 187));
    r0 = (r0 & 254);
    MemoryInline::FlatWrite8((r3 + 187), static_cast<uint8_t>(r0));
    goto loc_8077AAE0;
}

loc_8077AABC:
{
    r3 = MemoryInline::FlatRead32((r29 + 600));
    r0 = MemoryInline::FlatRead8((r3 + 187));
    r0 = (r0 & 254);
    MemoryInline::FlatWrite8((r3 + 187), static_cast<uint8_t>(r0));
    r3 = MemoryInline::FlatRead32((r29 + 604));
    r0 = MemoryInline::FlatRead8((r3 + 187));
    r0 = (r0 & 254);
    r0 = (r0 | 1);
    MemoryInline::FlatWrite8((r3 + 187), static_cast<uint8_t>(r0));
}

loc_8077AAE0:
{
    r3 = MemoryInline::FlatRead32((r29 + 608));
    r4 = r31;
    r6 = r30;
    r5 = 2;
    r7 = 0;
    cr = PpcCrLogicalResident(cr, static_cast<uint32_t>(2), static_cast<uint32_t>(6), static_cast<uint32_t>(6), static_cast<uint32_t>(6));
    ctx->lr = 0x8077AAFCu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<0x800C9B50u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
    r3 = MemoryInline::FlatRead32((r29 + 612));
    r4 = r31;
    r6 = r30;
    r5 = 2;
    r7 = 0;
    cr = PpcCrLogicalResident(cr, static_cast<uint32_t>(2), static_cast<uint32_t>(6), static_cast<uint32_t>(6), static_cast<uint32_t>(6));
    ctx->lr = 0x8077AB18u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<0x800C9B50u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r7 = ctx->gpr[7];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
    r7 = MemoryInline::FlatRead32((r29 + 580));
    r5 = 3;
    r4 = 1;
    r0 = 0;
    r6 = MemoryInline::FlatRead8((r7 + 187));
    r3 = 1;
    r6 = (r6 & 254);
    MemoryInline::FlatWrite8((r7 + 187), static_cast<uint8_t>(r6));
    MemoryInline::FlatWrite8((r29 + 124), static_cast<uint8_t>(r5));
    MemoryInline::FlatWrite8((r29 + 624), static_cast<uint8_t>(r4));
    MemoryInline::FlatWrite8((r29 + 625), static_cast<uint8_t>(r0));
    MemoryInline::FlatWrite8((r29 + 626), static_cast<uint8_t>(r0));
    MemoryInline::FlatWrite8((r29 + 627), static_cast<uint8_t>(r0));
}

loc_8077AB4C:
{
    r0 = MemoryInline::FlatRead32((r1 + 36));
    r31 = MemoryInline::FlatRead32((r1 + 28));
    r30 = MemoryInline::FlatRead32((r1 + 24));
    r29 = MemoryInline::FlatRead32((r1 + 20));
    ctx->lr = r0;
    r1 = (r1 + 32);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(8077AA10, NsmbwWmMenuSaveLabel_8077AA10, (CpuContext* ctx), (ctx));

extern "C" void NsmbwWmMenuSelect_8092FC30(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r12 = ctx->gpr[12];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_8092FC30;

loc_8092FC30:
{
    MemoryInline::FlatWriteRam32((r1 + -16), r1);
    r1 = (r1 + -16);
    r0 = ctx->lr;
    MemoryInline::FlatWriteRam32((r1 + 20), r0);
    MemoryInline::FlatWriteRam32((r1 + 12), r31);
    r31 = 0x809A0000u;
    r31 = (r31 + 6776);
    MemoryInline::FlatWriteRam32((r1 + 8), r30);
    r30 = r3;
    r0 = MemoryInline::FlatRead32((r3 + 1360));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_8092FC58:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8092FCA4;
    }
}

loc_8092FC5C:
{
    r3 = 0x80430000u;
    r4 = 127;
    r3 = MemoryInline::FlatRead32((r3 + -22680));
    r5 = 1;
    ctx->lr = 0x8092FC70u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x801954C0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    // inline leaf 0x808DEB70 (3 guest instruction(s))
    r3 = 0x80430000u;
    r3 = MemoryInline::FlatRead8((r3 + -23251));
    // end of inlined leaf 0x808DEB70
    r5 = MemoryInline::FlatRead32((r30 + 188));
    r4 = (r3 & 255);
    r0 = 1;
    r3 = (r30 + 112);
    MemoryInline::FlatWrite32((r5 + 2884), r4);
    r4 = (r31 + 480);
    MemoryInline::FlatWrite8((r5 + 2893), static_cast<uint8_t>(r0));
    r12 = MemoryInline::FlatRead32((r30 + 112));
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = 0x8092FCA0u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
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
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_8092FD60;
}

loc_8092FCA4:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(1));
}

loc_8092FCA8:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8092FCE8;
    }
}

loc_8092FCAC:
{
    r3 = 0x80430000u;
    r4 = 127;
    r3 = MemoryInline::FlatRead32((r3 + -22680));
    r5 = 1;
    ctx->lr = 0x8092FCC0u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    InvokeDirectCpu<0x801954C0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    r5 = MemoryInline::FlatRead32((r30 + 176));
    r0 = 1;
    r3 = (r30 + 112);
    r4 = (r31 + 544);
    MemoryInline::FlatWrite8((r5 + 1662), static_cast<uint8_t>(r0));
    r12 = MemoryInline::FlatRead32((r30 + 112));
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = 0x8092FCE4u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
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
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_8092FD60;
}

loc_8092FCE8:
{
}

loc_8092FCEC:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(2))) {
        goto loc_8092FD44;
    }
}

loc_8092FCF0:
{
    r3 = 0x80430000u;
    r4 = -1;
    r3 = MemoryInline::FlatRead32((r3 + -23776));
    // inline leaf 0x800E0470 (10 guest instruction(s))
}

loc_inl1_0x800E0470:
{
    r0 = (static_cast<int32_t>(static_cast<int8_t>(r4)));
}

loc_inl1_0x800E0478:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(-1))) {
        goto loc_inl1_0x800E0484;
    }
}

loc_inl1_0x800E047C:
{
    r0 = MemoryInline::FlatRead8((r3 + 38));
    r4 = (static_cast<int32_t>(static_cast<int8_t>(r0)));
}

loc_inl1_0x800E0484:
{
    r0 = (static_cast<int32_t>(static_cast<int8_t>(r4)));
    r0 = (r0 * 2432);
    r3 = (r3 + r0);
    r3 = (r3 + 1728);
}

loc_inl1_cont_800E0470:
{
    // end of inlined leaf 0x800E0470
    r0 = MemoryInline::FlatRead8((r3 + 2));
    r0 = 2; // EDIT: was (r0 & 2), mGameCompletion & FINAL_BOSS_BEATEN - always "beaten"
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_8092FD08:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_8092FD28;
    }
}

loc_8092FD0C:
{
    r12 = MemoryInline::FlatRead32((r30 + 112));
    r3 = (r30 + 112);
    r4 = (r31 + 608);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = 0x8092FD24u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
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
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_8092FD60;
}

loc_8092FD28:
{
    r12 = MemoryInline::FlatRead32((r30 + 112));
    r3 = (r30 + 112);
    r4 = (r31 + 992);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = 0x8092FD40u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
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
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
    goto loc_8092FD60;
}

loc_8092FD44:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(3));
}

loc_8092FD48:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8092FD60;
    }
}

loc_8092FD4C:
{
    r3 = (r3 + 112);
    r12 = MemoryInline::FlatRead32(r3);
    r4 = (r31 + 1568);
    r12 = MemoryInline::FlatRead32((r12 + 24));
    ctr = r12;
    ctx->lr = 0x8092FD60u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
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
    r5 = ctx->gpr[5];
    r12 = ctx->gpr[12];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    ctr = ctx->ctr;
    xer = ctx->xer;
}

loc_8092FD60:
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
    ctx->gpr[12] = r12;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(8092FC30, NsmbwWmMenuSelect_8092FC30, (CpuContext* ctx), (ctx));
