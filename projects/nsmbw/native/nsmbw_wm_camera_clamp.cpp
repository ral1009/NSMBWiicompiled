// Ultrawide: keep the world-map camera from showing past the map's edges (developer report
// 2026-11-20: "you can see the edge of the map", including in free-look).
//
// The map camera (dWCamera_c, not decompiled) has no limits of its own: 0x8092CD30 (camera mode 0)
// copies the player's focus point into the camera target every frame (+0x4BC x / +0x4C0 / +0x4C4)
// and 0x8092CC20 derives the eye from it. The 16:9 game never shows the edge only because each map's
// art extends past the camera's reach. Free-look ("View Map") does clamp, to per-map limits it
// computes when it starts (0x8092D620 -> box at +0x658 / +0x664); measured with NSMBW_DUMP_WMCAM
// (World 1: camera x 210 .. 2200). At 21:9 the view is wider, so this copy clamps the target x to
// those limits pulled in by the extra half-width (kMargin), and nsmbw_tick_read_pump.cpp shrinks the
// free-look box the same way. Where the limits leave no room (a map narrower than the 21:9 view),
// the camera is centred between them. Only when the view is wider than 16:9; NSMBW_WMCAM_CLAMP=0
// turns it off, NSMBW_WMCAM_MARGIN=<units> overrides the margin.
//
// Structural copy of build/nsmbw-d_basesNP/functions/func_8092CD30.cpp; only the EDIT lines differ.
#include "nsmbw_va.h"
#include "hle_stubs.h"
#include <cstdlib>
#include "nsmbw_wm_camera_limits.h"
#include <cstdint>
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include "recomp_mod_loader.h"

extern "C" void NsmbwWmCameraClamp_8092CD30(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    PPC_FPR f0 = ctx->fpr[0];
    PPC_FPR f1 = ctx->fpr[1];
    PPC_FPR f2 = ctx->fpr[2];
    PPC_FPR f3 = ctx->fpr[3];
    PPC_FPR f4 = ctx->fpr[4];
    uint32_t cr = ctx->cr;
    uint32_t xer = ctx->xer;

    goto loc_8092CD30;

loc_8092CD30:
{
    MemoryInline::FlatWriteRam32((r1 + -48), r1);
    r1 = (r1 + -48);
    r0 = ctx->lr;
    r4 = NSMBW_VA_PAIR(0x80430000u, -23424);
    MemoryInline::FlatWriteRam32((r1 + 52), r0);
    MemoryInline::FlatWriteRam32((r1 + 44), r31);
    MemoryInline::FlatWriteRam32((r1 + 40), r30);
    r30 = r3;
    r0 = MemoryInline::FlatRead32((r3 + 1544));
    r31 = MemoryInline::FlatRead32((r4 + -23424));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_8092CD58:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_8092CD68;
    }
}

loc_8092CD5C:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(1));
}

loc_8092CD60:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_8092CD6C;
    }
}

loc_8092CD64:
{
    goto loc_8092CDB4;
}

loc_8092CD68:
{
    // inline leaf 0x8092CBE0 (15 guest instruction(s))
    r4 = NSMBW_VA_PAIR(0x80940000u, -7956);
    f3.d = MemoryInline::FlatReadFloat32((r3 + 1212));
    f4.d = MemoryInline::FlatReadFloat32((r4 + -7956));
    r0 = 1;
    f2.d = MemoryInline::FlatReadFloat32((r3 + 1216));
    f1.d = MemoryInline::FlatReadFloat32((r3 + 1220));
    f0.d = MemoryInline::FlatReadFloat32((r3 + 1516));
    MemoryInline::FlatWriteFloat32((r3 + 1548), f4.d);
    MemoryInline::FlatWriteFloat32((r3 + 1568), f4.d);
    MemoryInline::FlatWriteFloat32((r3 + 1552), f3.d);
    MemoryInline::FlatWriteFloat32((r3 + 1556), f2.d);
    MemoryInline::FlatWriteFloat32((r3 + 1560), f1.d);
    MemoryInline::FlatWriteFloat32((r3 + 1564), f0.d);
    MemoryInline::FlatWrite32((r3 + 1544), r0);
    // end of inlined leaf 0x8092CBE0
}

loc_8092CD6C:
{
    r4 = r31;
    r3 = (r1 + 20);
    // inline leaf 0x80907990 (8 guest instruction(s))
    r4 = MemoryInline::FlatRead32((r4 + 668));
    f2.d = MemoryInline::FlatReadFloat32((r4 + 20));
    f1.d = MemoryInline::FlatReadFloat32((r4 + 16));
    f0.d = MemoryInline::FlatReadFloat32((r4 + 12));
    MemoryInline::FlatWriteRamFloat32(r3, f0.d);
    MemoryInline::FlatWriteRamFloat32((r3 + 4), f1.d);
    MemoryInline::FlatWriteRamFloat32((r3 + 8), f2.d);
    // end of inlined leaf 0x80907990
    f0.d = MemoryInline::FlatReadFloat32((r1 + 20));
    r4 = r31;
    f0.d = NsmbwWmClampCameraX(f0.d); // EDIT: clamp the target x at wide ratios
    MemoryInline::FlatWriteFloat32((r30 + 1212), f0.d);
    r3 = (r1 + 8);
    f0.d = MemoryInline::FlatReadFloat32((r1 + 24));
    MemoryInline::FlatWriteFloat32((r30 + 1216), f0.d);
    f0.d = MemoryInline::FlatReadFloat32((r1 + 28));
    f0.d = NsmbwWmClampCameraZ(f0.d); // EDIT: clamp the target z to the free-look range
    MemoryInline::FlatWriteFloat32((r30 + 1220), f0.d);
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    InvokeDirectCpu<NSMBW_VA(0x809079B0u)>(ctx);
    r4 = ctx->gpr[4];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f0.d = MemoryInline::FlatReadFloat32((r1 + 8));
    MemoryInline::FlatWriteFloat32((r30 + 1588), f0.d);
    f0.d = MemoryInline::FlatReadFloat32((r1 + 12));
    MemoryInline::FlatWriteFloat32((r30 + 1592), f0.d);
    f0.d = MemoryInline::FlatReadFloat32((r1 + 16));
    MemoryInline::FlatWriteFloat32((r30 + 1596), f0.d);
}

loc_8092CDB4:
{
    r3 = r30;
    ctx->lr = NSMBW_VA(0x8092CDBCu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[4] = f4;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x8092CC20u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f3 = ctx->fpr[3];
    f4 = ctx->fpr[4];
    cr = ctx->cr;
    r0 = MemoryInline::FlatRead32((r1 + 52));
    r31 = MemoryInline::FlatRead32((r1 + 44));
    r30 = MemoryInline::FlatRead32((r1 + 40));
    ctx->lr = r0;
    r1 = (r1 + 48);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[4] = f4;
    ctx->cr = cr;
    return;
}

}

// RECOMP_GUEST_ABI gpr_read=0xC000001F gpr_write=0xC000003B gpr_return=0x00000018 fpr_read=0xC000001F fpr_write=0xC00001FF fpr_return=0x00000002 cr_read=0x01 cr_write=0x01 xer_read=1 xer_write=0 fence=0
// RECOMP_REGISTRATION base 0x8092CD30 func_8092CD30 preserves=true fpr_mask=0x00000000

PPC_NATIVE_OVERRIDE_VOID(8092CD30, NsmbwWmCameraClamp_8092CD30, (CpuContext* ctx), (ctx));
