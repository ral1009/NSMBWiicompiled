// Ultrawide: the level camera when the zone is narrower than the view (World 4's Big Bertha arena
// at 21:9, developer report 2026-11-19: "the camera suddenly switches from way to the right to way to
// the left depending on the player's position").
//
// 0x8007A6C0 (a dBg_c camera update; no symbol) clamps the camera's x between f26 = dBg_c::getLeftLimit
// (0x80078A70) and f25 = dBg_c::getRightLimit (0x80078C10) minus the view width: below f26 -> f26,
// else above f25 -> f25 (loc_8007A864 and, in one special case, loc_8007A8B8). At 16:9 the zone is
// at least as wide as the view, so f26 <= f25. The 21:9 view is wider than some arenas: then f25 < f26
// and the result jumps between the two limits as the player crosses the zone's centre. With this
// copy, when f25 < f26 the camera sits halfway between them (the zone centred in the view; the area
// past the zone's edges shows at both sides). Otherwise nothing changes.
//
// Structural copy of build/nsmbw/functions/func_8007A6C0.cpp (2026-11-19); only the lines marked
// EDIT differ. Callers 0x8007BEF0 and 0x801550A0 call it (too big to inline), so the override runs.
#include "nsmbw_va.h"
#include <cstdint>
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include "recomp_mod_loader.h"

#include <cstdio>
#include <cstdlib>
extern "C" uint32_t g_nsmbwCurrentViTick;
// NSMBW_LOG_WIDE_CAMERA=1: a line (at most once a second) when the centring below is used.
static void LogCentred(double left, double right) {
    static const bool on = std::getenv("NSMBW_LOG_WIDE_CAMERA") != nullptr;
    static uint32_t last = 0;
    if (!on || g_nsmbwCurrentViTick - last < 60u) return;
    last = g_nsmbwCurrentViTick;
    const uint32_t bgp = MemoryInline::FlatRead32(NSMBW_VA(0x8042A0E0u)); // dBgParameter_c::ms_Instance_p
    const float camX = bgp ? MemoryInline::FlatReadFloat32(bgp + 0x38u) : 0.0f, camW = bgp ? MemoryInline::FlatReadFloat32(bgp + 0x40u) : 0.0f;
    std::fprintf(stderr, "[nsmbw][widecam] zone narrower than the view: left limit %.1f > right limit %.1f, centred at %.1f; visible x %.1f..%.1f (tick %u)\n",
                 left, right, (left + right) * 0.5, camX, camX + camW, g_nsmbwCurrentViTick);
}

extern "C" void NsmbwWideCamera_8007A6C0(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r1_psq_tmp_0 = 0;
    uint32_t r1_psq_tmp_1 = 0;
    uint32_t r1_psq_tmp_2 = 0;
    uint32_t r1_psq_tmp_3 = 0;
    uint32_t r1_psq_tmp_4 = 0;
    uint32_t r1_psq_tmp_5 = 0;
    uint32_t r1_psq_tmp_6 = 0;
    uint32_t r1_psq_tmp_7 = 0;
    uint32_t r1_psq_tmp_8 = 0;
    uint8_t* guest_range_0 = nullptr;
    uint8_t* guest_range_1 = nullptr;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r2 = ctx->gpr[2];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r11 = ctx->gpr[11];
    uint32_t r13 = ctx->gpr[13];
    uint32_t r26 = ctx->gpr[26];
    uint32_t r27 = ctx->gpr[27];
    uint32_t r28 = ctx->gpr[28];
    uint32_t r29 = ctx->gpr[29];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    PPC_FPR f0 = ctx->fpr[0];
    PPC_FPR f1 = ctx->fpr[1];
    PPC_FPR f2 = ctx->fpr[2];
    PPC_FPR f3 = ctx->fpr[3];
    PPC_FPR f23 = ctx->fpr[23];
    PPC_FPR f24 = ctx->fpr[24];
    PPC_FPR f25 = ctx->fpr[25];
    PPC_FPR f26 = ctx->fpr[26];
    PPC_FPR f27 = ctx->fpr[27];
    PPC_FPR f28 = ctx->fpr[28];
    PPC_FPR f29 = ctx->fpr[29];
    PPC_FPR f30 = ctx->fpr[30];
    PPC_FPR f31 = ctx->fpr[31];
    uint32_t cr = ctx->cr;
    uint32_t xer = ctx->xer;

    goto loc_8007A6C0;

loc_8007A6C0:
{
    guest_range_0 = MemoryInline::ResolveRangeHost((r1 + -176), 0, 184u, false, true);
    MemoryInline::WriteResolved32(guest_range_0, 0u, (r1 + -176), r1);
    r1 = (r1 + -176);
    r0 = ctx->lr;
    MemoryInline::WriteResolved32(guest_range_0, 180u, (r1 + 180), r0);
    r11 = (r1 + 32);
    MemoryInline::WriteResolvedFloat64(guest_range_0, 160u, (r1 + 160), f31.d);
    // psq_store w=0 quant=0 (using PPC_PsqSt)
    r1_psq_tmp_0 = (r1 + 168);
    PPC_PsqStStackInline<0u, 0u>(ctx, r1_psq_tmp_0, PPC_PsFromScalarInline(f31.d));
    MemoryInline::WriteResolvedFloat64(guest_range_0, 144u, (r1 + 144), f30.d);
    // psq_store w=0 quant=0 (using PPC_PsqSt)
    r1_psq_tmp_1 = (r1 + 152);
    PPC_PsqStStackInline<0u, 0u>(ctx, r1_psq_tmp_1, PPC_PsFromScalarInline(f30.d));
    MemoryInline::WriteResolvedFloat64(guest_range_0, 128u, (r1 + 128), f29.d);
    // psq_store w=0 quant=0 (using PPC_PsqSt)
    r1_psq_tmp_2 = (r1 + 136);
    PPC_PsqStStackInline<0u, 0u>(ctx, r1_psq_tmp_2, PPC_PsFromScalarInline(f29.d));
    MemoryInline::WriteResolvedFloat64(guest_range_0, 112u, (r1 + 112), f28.d);
    // psq_store w=0 quant=0 (using PPC_PsqSt)
    r1_psq_tmp_3 = (r1 + 120);
    PPC_PsqStStackInline<0u, 0u>(ctx, r1_psq_tmp_3, PPC_PsFromScalarInline(f28.d));
    MemoryInline::WriteResolvedFloat64(guest_range_0, 96u, (r1 + 96), f27.d);
    // psq_store w=0 quant=0 (using PPC_PsqSt)
    r1_psq_tmp_4 = (r1 + 104);
    PPC_PsqStStackInline<0u, 0u>(ctx, r1_psq_tmp_4, PPC_PsFromScalarInline(f27.d));
    MemoryInline::WriteResolvedFloat64(guest_range_0, 80u, (r1 + 80), f26.d);
    // psq_store w=0 quant=0 (using PPC_PsqSt)
    r1_psq_tmp_5 = (r1 + 88);
    PPC_PsqStStackInline<0u, 0u>(ctx, r1_psq_tmp_5, PPC_PsFromScalarInline(f26.d));
    MemoryInline::WriteResolvedFloat64(guest_range_0, 64u, (r1 + 64), f25.d);
    // psq_store w=0 quant=0 (using PPC_PsqSt)
    r1_psq_tmp_6 = (r1 + 72);
    PPC_PsqStStackInline<0u, 0u>(ctx, r1_psq_tmp_6, PPC_PsFromScalarInline(f25.d));
    MemoryInline::WriteResolvedFloat64(guest_range_0, 48u, (r1 + 48), f24.d);
    // psq_store w=0 quant=0 (using PPC_PsqSt)
    r1_psq_tmp_7 = (r1 + 56);
    PPC_PsqStStackInline<0u, 0u>(ctx, r1_psq_tmp_7, PPC_PsFromScalarInline(f24.d));
    MemoryInline::WriteResolvedFloat64(guest_range_0, 32u, (r1 + 32), f23.d);
    // psq_store w=0 quant=0 (using PPC_PsqSt)
    r1_psq_tmp_8 = (r1 + 40);
    PPC_PsqStStackInline<0u, 0u>(ctx, r1_psq_tmp_8, PPC_PsFromScalarInline(f23.d));
    // inline leaf 0x802DD060 (7 guest instruction(s))
    if (!MemoryInline::WriteResolvedPair32(guest_range_0, 8u, ((static_cast<uint64_t>(static_cast<uint32_t>(r26)) << 32) | static_cast<uint32_t>(r27)))) {
        MemoryInline::WriteResolved32(guest_range_0, 8u, (r11 + -24), r26);
        MemoryInline::WriteResolved32(guest_range_0, 12u, (r11 + -20), r27);
    }
    if (!MemoryInline::WriteResolvedPair32(guest_range_0, 16u, ((static_cast<uint64_t>(static_cast<uint32_t>(r28)) << 32) | static_cast<uint32_t>(r29)))) {
        MemoryInline::WriteResolved32(guest_range_0, 16u, (r11 + -16), r28);
        MemoryInline::WriteResolved32(guest_range_0, 20u, (r11 + -12), r29);
    }
    if (!MemoryInline::WriteResolvedPair32(guest_range_0, 24u, ((static_cast<uint64_t>(static_cast<uint32_t>(r30)) << 32) | static_cast<uint32_t>(r31)))) {
        MemoryInline::WriteResolved32(guest_range_0, 24u, (r11 + -8), r30);
        MemoryInline::WriteResolved32(guest_range_0, 28u, (r11 + -4), r31);
    }
    // end of inlined leaf 0x802DD060
    r6 = MemoryInline::FlatRead32((r13 + -NSMBW_SDA13(22736)));
    r30 = r3;
    r29 = MemoryInline::FlatRead32((r13 + -NSMBW_SDA13(22688)));
    r26 = r4;
    r3 = (r6 + 589824);
    f29.d = MemoryInline::FlatReadFloat32((r13 + -NSMBW_SDA13(21940)));
    r0 = MemoryInline::FlatRead8((r3 + 2506));
    r31 = r5;
    f31.d = MemoryInline::FlatReadFloat32((r13 + -NSMBW_SDA13(21936)));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_8007A744:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8007AAB0;
    }
}

loc_8007A748:
{
    r3 = r6;
    ctx->lr = NSMBW_VA(0x8007A750u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[11] = r11;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x800791D0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r11 = ctx->gpr[11];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f3 = ctx->fpr[3];
    cr = ctx->cr;
    r3 = (r30 + 589824);
    f0.d = MemoryInline::FlatReadFloat32((r2 + -NSMBW_SDA2(29552)));
    f30.d = MemoryInline::FlatReadFloat32((r3 + 16));
    SetCRFloatResident(cr, 0, f30.d, f0.d);
}

loc_8007A760:
{
    if (((cr & 0x40000000u) == 0)) {
        goto loc_8007A7A8;
    }
}

loc_8007A764:
{
    r3 = MemoryInline::FlatRead32((r13 + -NSMBW_SDA13(22688)));
    r28 = MemoryInline::FlatRead32((r13 + -NSMBW_SDA13(22736)));
    f28.d = MemoryInline::FlatReadFloat32((r3 + 52));
    r3 = r28;
    ctx->lr = NSMBW_VA(0x8007A778u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[11] = r11;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x80078EB0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r11 = ctx->gpr[11];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    cr = ctx->cr;
    f0.d = MemoryInline::FlatReadFloat32((r2 + -NSMBW_SDA2(29544)));
    r3 = r28;
    f1.d = static_cast<double>(PpcForceSingleValueInline(f0.d / f1.d));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d / f30.d));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f1.d));
    f30.d = PpcFmulsInline(f28.d, f0.d);
    ctx->lr = NSMBW_VA(0x8007A794u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[11] = r11;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x80078EB0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r11 = ctx->gpr[11];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    cr = ctx->cr;
    f0.d = MemoryInline::FlatReadFloat32((r2 + -NSMBW_SDA2(29544)));
    r3 = (r30 + 589824);
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d / f1.d));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d + f30.d));
    MemoryInline::FlatWriteFloat32((r3 + 12), f0.d);
}

loc_8007A7A8:
{
    r4 = (r30 + 589824);
    f1.d = MemoryInline::FlatReadFloat32((r2 + -NSMBW_SDA2(29544)));
    f2.d = MemoryInline::FlatReadFloat32((r4 + 12));
    r3 = r30;
    f0.d = MemoryInline::FlatReadFloat32((r2 + -NSMBW_SDA2(29476)));
    f1.d = static_cast<double>(PpcForceSingleValueInline(f1.d / f2.d));
    MemoryInline::FlatWriteFloat32((r4 + 136), f2.d);
    MemoryInline::FlatWriteFloat32((r4 + 156), f2.d);
    f30.d = PpcFmulsInline(f29.d, f1.d);
    f29.d = PpcFmulsInline(f31.d, f1.d);
    MemoryInline::FlatWriteFloat32((r4 + -392), f30.d);
    f27.d = PpcFmulsInline(f0.d, f30.d);
    MemoryInline::FlatWriteFloat32((r4 + -388), f29.d);
    ctx->lr = NSMBW_VA(0x8007A7E0u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[11] = r11;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[29] = f29;
    ctx->fpr[30] = f30;
    ctx->fpr[31] = f31;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x80078A70u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r11 = ctx->gpr[11];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f3 = ctx->fpr[3];
    f29 = ctx->fpr[29];
    f30 = ctx->fpr[30];
    f31 = ctx->fpr[31];
    cr = ctx->cr;
    f26.d = f1.d;
    r3 = r30;
    ctx->lr = NSMBW_VA(0x8007A7ECu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[11] = r11;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[29] = f29;
    ctx->fpr[30] = f30;
    ctx->fpr[31] = f31;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x80078C10u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r11 = ctx->gpr[11];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f3 = ctx->fpr[3];
    f29 = ctx->fpr[29];
    f30 = ctx->fpr[30];
    f31 = ctx->fpr[31];
    cr = ctx->cr;
    r4 = (r30 + 589824);
    f25.d = static_cast<double>(PpcForceSingleValueInline(f1.d - f30.d));
    f0.d = MemoryInline::FlatReadFloat32((r4 + 60));
    r3 = r30;
    f31.d = MemoryInline::FlatReadFloat32((r4 + 56));
    r28 = 1;
    f24.d = static_cast<double>(PpcForceSingleValueInline(f29.d + f0.d));
    r27 = 1;
    ctx->lr = NSMBW_VA(0x8007A810u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[11] = r11;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[29] = f29;
    ctx->fpr[30] = f30;
    ctx->fpr[31] = f31;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x800789A0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r11 = ctx->gpr[11];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f3 = ctx->fpr[3];
    f29 = ctx->fpr[29];
    f30 = ctx->fpr[30];
    f31 = ctx->fpr[31];
    cr = ctx->cr;
    r3 = (r30 + 589824);
    f2.d = MemoryInline::FlatReadFloat32((r29 + 56));
    MemoryInline::FlatWriteFloat32((r3 + -344), f2.d);
    f30.d = f1.d;
    f3.d = MemoryInline::FlatReadFloat32((r3 + -388));
    f0.d = MemoryInline::FlatReadFloat32((r2 + -NSMBW_SDA2(29476)));
    f1.d = MemoryInline::FlatReadFloat32((r26 + 4));
    f0.d = PpcFmulsInline(f0.d, f3.d);
    f3.d = MemoryInline::FlatReadFloat32((r29 + 60));
    f2.d = MemoryInline::FlatReadFloat32(r26);
    f28.d = MemoryInline::FlatReadFloat32((r3 + -444));
    MemoryInline::FlatWriteFloat32((r3 + -340), f3.d);
    f0.d = static_cast<double>(PpcForceSingleValueInline(f1.d + f0.d));
    f29.d = MemoryInline::FlatReadFloat32((r3 + -440));
    f2.d = static_cast<double>(PpcForceSingleValueInline(f2.d + f28.d));
    MemoryInline::FlatWriteFloat32((r3 + 96), f1.d);
    f23.d = static_cast<double>(PpcForceSingleValueInline(f0.d - f29.d));
    r0 = MemoryInline::FlatRead32((r13 + -NSMBW_SDA13(21656)));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f2.d - f27.d));
}

loc_8007A860:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(2))) {
        goto loc_8007A884;
    }
}

loc_8007A864:
{
    if (f25.d < f26.d) { // EDIT: zone narrower than the view: centre it
        LogCentred(f26.d, f25.d);
        f0.d = static_cast<double>(PpcForceSingleValueInline((f25.d + f26.d) * 0.5));
        goto loc_8007A8D4;
    }
    SetCRFloatResident(cr, 0, f0.d, f26.d);
}

loc_8007A868:
{
    if (((cr & NSMBW_VA(0x80000000u)) == 0)) {
        goto loc_8007A874;
    }
}

loc_8007A86C:
{
    f0.d = f26.d;
    goto loc_8007A8D4;
}

loc_8007A874:
{
    SetCRFloatResident(cr, 0, f0.d, f25.d);
}

loc_8007A878:
{
    if (((cr & 0x40000000u) == 0)) {
        goto loc_8007A8D4;
    }
}

loc_8007A87C:
{
    f0.d = f25.d;
    goto loc_8007A8D4;
}

loc_8007A884:
{
    r4 = MemoryInline::FlatRead32((r13 + -NSMBW_SDA13(21720)));
    r0 = MemoryInline::FlatRead8((r4 + 4620));
}

loc_8007A890:
{
    if ((static_cast<uint32_t>(r0) != static_cast<uint32_t>(1))) {
        goto loc_8007A8D4;
    }
}

loc_8007A894:
{
    r0 = MemoryInline::FlatRead8((r4 + 4621));
}

loc_8007A89C:
{
    if ((static_cast<uint32_t>(r0) != static_cast<uint32_t>(23))) {
        goto loc_8007A8D4;
    }
}

loc_8007A8A0:
{
    r0 = MemoryInline::FlatRead8((r4 + 4622));
}

loc_8007A8A8:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_8007A8D4;
    }
}

loc_8007A8AC:
{
    r0 = MemoryInline::FlatRead16((r3 + 2504));
    r0 = (r0 << 16);
    r0 = (static_cast<int32_t>(r0) >> 16);
}

loc_8007A8B4:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_8007A8D4;
    }
}

loc_8007A8B8:
{
    if (f25.d < f26.d) { // EDIT: zone narrower than the view: centre it
        LogCentred(f26.d, f25.d);
        f0.d = static_cast<double>(PpcForceSingleValueInline((f25.d + f26.d) * 0.5));
        goto loc_8007A8D4;
    }
    SetCRFloatResident(cr, 0, f0.d, f26.d);
}

loc_8007A8BC:
{
    if (((cr & NSMBW_VA(0x80000000u)) == 0)) {
        goto loc_8007A8C8;
    }
}

loc_8007A8C0:
{
    f0.d = f26.d;
    goto loc_8007A8D4;
}

loc_8007A8C8:
{
    SetCRFloatResident(cr, 0, f0.d, f25.d);
}

loc_8007A8CC:
{
    if (((cr & 0x40000000u) == 0)) {
        goto loc_8007A8D4;
    }
}

loc_8007A8D0:
{
    f0.d = f25.d;
}

loc_8007A8D4:
{
    SetCRFloatResident(cr, 0, f23.d, f24.d);
}

loc_8007A8D8:
{
    if (((cr & NSMBW_VA(0x80000000u)) == 0)) {
        goto loc_8007A8E4;
    }
}

loc_8007A8DC:
{
    f23.d = f24.d;
    goto loc_8007A8F0;
}

loc_8007A8E4:
{
    SetCRFloatResident(cr, 0, f23.d, f31.d);
}

loc_8007A8E8:
{
    if (((cr & 0x40000000u) == 0)) {
        goto loc_8007A8F0;
    }
}

loc_8007A8EC:
{
    f23.d = f31.d;
}

loc_8007A8F0:
{
    r3 = (r30 + 589824);
    r0 = MemoryInline::FlatRead8((r3 + 198));
}

loc_8007A8FC:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_8007A940;
    }
}

loc_8007A900:
{
    r0 = MemoryInline::FlatRead8((r3 + 196));
}

loc_8007A908:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_8007A920;
    }
}

loc_8007A90C:
{
}

loc_8007A910:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(1))) {
        goto loc_8007A92C;
    }
}

loc_8007A914:
{
}

loc_8007A918:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(2))) {
        goto loc_8007A938;
    }
}

loc_8007A91C:
{
    goto loc_8007A940;
}

loc_8007A920:
{
    r28 = 0;
    r27 = 0;
    goto loc_8007A940;
}

loc_8007A92C:
{
    r28 = 0;
    r27 = 1;
    goto loc_8007A940;
}

loc_8007A938:
{
    r28 = 1;
    r27 = 0;
}

loc_8007A940:
{
}

loc_8007A944:
{
    if ((static_cast<int32_t>(r28) == static_cast<int32_t>(0))) {
        goto loc_8007A950;
    }
}

loc_8007A948:
{
    r3 = (r30 + 589824);
    MemoryInline::FlatWriteFloat32((r3 + -376), f0.d);
}

loc_8007A950:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r27), static_cast<int32_t>(0));
}

loc_8007A954:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_8007A9F4;
    }
}

loc_8007A958:
{
    r3 = r30;
    ctx->lr = NSMBW_VA(0x8007A960u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[11] = r11;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[29] = f29;
    ctx->fpr[30] = f30;
    ctx->fpr[31] = f31;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x8007E0E0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r11 = ctx->gpr[11];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f29 = ctx->fpr[29];
    f30 = ctx->fpr[30];
    f31 = ctx->fpr[31];
    cr = ctx->cr;
    f0.d = MemoryInline::FlatReadFloat32((r2 + -NSMBW_SDA2(29548)));
    SetCRFloatResident(cr, 0, f1.d, f0.d);
}

loc_8007A968:
{
    if (((cr & 0x40000000u) == 0)) {
        goto loc_8007A9B4;
    }
}

loc_8007A96C:
{
    r4 = MemoryInline::FlatRead32((r13 + -NSMBW_SDA13(22688)));
    r3 = r30;
    f31.d = MemoryInline::FlatReadFloat32((r4 + 60));
    ctx->lr = NSMBW_VA(0x8007A97Cu);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[11] = r11;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[29] = f29;
    ctx->fpr[30] = f30;
    ctx->fpr[31] = f31;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x8007E0E0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r11 = ctx->gpr[11];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f29 = ctx->fpr[29];
    f30 = ctx->fpr[30];
    f31 = ctx->fpr[31];
    cr = ctx->cr;
    SetCRFloatResident(cr, 0, f31.d, f1.d);
}

loc_8007A980:
{
    cr = PpcCrLogicalResident(cr, static_cast<uint32_t>(7), static_cast<uint32_t>(2), static_cast<uint32_t>(0), static_cast<uint32_t>(2));
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8007A9B4;
    }
}

loc_8007A988:
{
    r3 = r30;
    ctx->lr = NSMBW_VA(0x8007A990u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[11] = r11;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[29] = f29;
    ctx->fpr[30] = f30;
    ctx->fpr[31] = f31;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x8007E0E0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r11 = ctx->gpr[11];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f29 = ctx->fpr[29];
    f30 = ctx->fpr[30];
    f31 = ctx->fpr[31];
    cr = ctx->cr;
    SetCRFloatResident(cr, 0, f23.d, f1.d);
}

loc_8007A994:
{
    if (((cr & 0x40000000u) == 0)) {
        goto loc_8007A9B4;
    }
}

loc_8007A998:
{
    r3 = r30;
    ctx->lr = NSMBW_VA(0x8007A9A0u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x80078540u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_8007A9A4:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8007A9B4;
    }
}

loc_8007A9A8:
{
    r3 = r30;
    ctx->lr = NSMBW_VA(0x8007A9B0u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[11] = r11;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[29] = f29;
    ctx->fpr[30] = f30;
    ctx->fpr[31] = f31;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x8007E0E0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r11 = ctx->gpr[11];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f29 = ctx->fpr[29];
    f30 = ctx->fpr[30];
    f31 = ctx->fpr[31];
    cr = ctx->cr;
    f23.d = f1.d;
}

loc_8007A9B4:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r31), static_cast<int32_t>(0));
}

loc_8007A9B8:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8007A9D4;
    }
}

loc_8007A9BC:
{
    r3 = r30;
    ctx->lr = NSMBW_VA(0x8007A9C4u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[11] = r11;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[29] = f29;
    ctx->fpr[30] = f30;
    ctx->fpr[31] = f31;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x8007E0E0u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r11 = ctx->gpr[11];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f29 = ctx->fpr[29];
    f30 = ctx->fpr[30];
    f31 = ctx->fpr[31];
    cr = ctx->cr;
    f0.d = MemoryInline::FlatReadFloat32((r2 + -NSMBW_SDA2(29548)));
    SetCRFloatResident(cr, 0, f1.d, f0.d);
}

loc_8007A9CC:
{
    cr = PpcCrLogicalResident(cr, static_cast<uint32_t>(7), static_cast<uint32_t>(2), static_cast<uint32_t>(0), static_cast<uint32_t>(2));
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8007A9E0;
    }
}

loc_8007A9D4:
{
    r3 = (r30 + 589824);
    MemoryInline::FlatWriteFloat32((r3 + -372), f23.d);
    goto loc_8007A9F4;
}

loc_8007A9E0:
{
    f1.d = f23.d;
    r3 = (r30 + 589824);
    f2.d = MemoryInline::FlatReadFloat32((r2 + -NSMBW_SDA2(29304)));
    r3 = (r3 + -372);
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x8015F5B0u)>(ctx);
    r3 = ctx->gpr[3];
    f0 = ctx->fpr[0];
    f2 = ctx->fpr[2];
    f3 = ctx->fpr[3];
    cr = ctx->cr;
}

loc_8007A9F4:
{
    r3 = (r30 + 589824);
    r0 = MemoryInline::FlatRead8((r3 + 254));
}

loc_8007AA00:
{
    if ((static_cast<uint32_t>(r0) == static_cast<uint32_t>(1))) {
        goto loc_8007AA0C;
    }
}

loc_8007AA04:
{
}

loc_8007AA08:
{
    if ((static_cast<uint32_t>(r0) != static_cast<uint32_t>(4))) {
        goto loc_8007AA30;
    }
}

loc_8007AA0C:
{
    r3 = (r30 + 589824);
    f0.d = MemoryInline::FlatReadFloat32((r2 + -NSMBW_SDA2(29476)));
    f1.d = MemoryInline::FlatReadFloat32((r3 + -352));
    f2.d = MemoryInline::FlatReadFloat32((r3 + -392));
    f1.d = PpcFmulsInline(f0.d, f1.d);
    f0.d = PpcFmulsInline(f0.d, f2.d);
    f1.d = static_cast<double>(PpcForceSingleValueInline(f30.d + f1.d));
    f1.d = static_cast<double>(PpcForceSingleValueInline(f1.d - f0.d));
    MemoryInline::FlatWriteFloat32((r3 + -376), f1.d);
}

loc_8007AA30:
{
    r3 = (r30 + 589824);
    r0 = MemoryInline::FlatRead8((r3 + 254));
}

loc_8007AA3C:
{
    if ((static_cast<uint32_t>(r0) != static_cast<uint32_t>(6))) {
        goto loc_8007AA50;
    }
}

loc_8007AA40:
{
    f0.d = MemoryInline::FlatReadFloat32((r3 + -388));
    f1.d = MemoryInline::FlatReadFloat32((r3 + -400));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f1.d + f0.d));
    MemoryInline::FlatWriteFloat32((r3 + -372), f0.d);
}

loc_8007AA50:
{
    r3 = (r30 + 589824);
    r0 = MemoryInline::FlatRead8((r3 + 198));
    MemoryInline::FlatWriteFloat32((r3 + -444), f28.d);
}

loc_8007AA60:
{
    MemoryInline::FlatWriteFloat32((r3 + -440), f29.d);
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_8007AA78;
    }
}

loc_8007AA68:
{
    f0.d = MemoryInline::FlatReadFloat32((r3 + -376));
    f1.d = MemoryInline::FlatReadFloat32((r3 + -372));
    MemoryInline::FlatWriteFloat32((r3 + 172), f0.d);
    MemoryInline::FlatWriteFloat32((r3 + 176), f1.d);
}

loc_8007AA78:
{
    r3 = (r30 + 589824);
    r0 = MemoryInline::FlatRead8((r3 + 279));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_8007AA84:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_8007AAB0;
    }
}

loc_8007AA88:
{
    f2.d = MemoryInline::FlatReadFloat32((r3 + -392));
    f0.d = MemoryInline::FlatReadFloat32((r2 + -NSMBW_SDA2(29476)));
    f1.d = MemoryInline::FlatReadFloat32((r3 + 284));
    f0.d = PpcFmulsInline(f0.d, f2.d);
    f2.d = MemoryInline::FlatReadFloat32((r3 + 288));
    f3.d = MemoryInline::FlatReadFloat32((r3 + -388));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f1.d - f0.d));
    f1.d = static_cast<double>(PpcForceSingleValueInline(f2.d + f3.d));
    MemoryInline::FlatWriteFloat32((r3 + -376), f0.d);
    MemoryInline::FlatWriteFloat32((r3 + -372), f1.d);
}

loc_8007AAB0:
{
    r4 = (r30 + 589824);
    r3 = r30;
    r5 = MemoryInline::FlatRead8((r4 + 121));
    r6 = 1;
    r4 = (r4 + 2520);
    ctx->lr = NSMBW_VA(0x8007AAC8u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[11] = r11;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[23] = f23;
    ctx->fpr[24] = f24;
    ctx->fpr[25] = f25;
    ctx->fpr[26] = f26;
    ctx->fpr[27] = f27;
    ctx->fpr[28] = f28;
    ctx->fpr[29] = f29;
    ctx->fpr[30] = f30;
    ctx->fpr[31] = f31;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x8007CA90u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r2 = ctx->gpr[2];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r11 = ctx->gpr[11];
    r13 = ctx->gpr[13];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f3 = ctx->fpr[3];
    f23 = ctx->fpr[23];
    f24 = ctx->fpr[24];
    f25 = ctx->fpr[25];
    f26 = ctx->fpr[26];
    f27 = ctx->fpr[27];
    f28 = ctx->fpr[28];
    f29 = ctx->fpr[29];
    f30 = ctx->fpr[30];
    f31 = ctx->fpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
    r4 = (r30 + 589824);
    r3 = r30;
    r5 = MemoryInline::FlatRead8((r4 + 122));
    r6 = 1;
    r4 = (r4 + 2520);
    ctx->lr = NSMBW_VA(0x8007AAE0u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[11] = r11;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[23] = f23;
    ctx->fpr[24] = f24;
    ctx->fpr[25] = f25;
    ctx->fpr[26] = f26;
    ctx->fpr[27] = f27;
    ctx->fpr[28] = f28;
    ctx->fpr[29] = f29;
    ctx->fpr[30] = f30;
    ctx->fpr[31] = f31;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x8007CA90u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r2 = ctx->gpr[2];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r11 = ctx->gpr[11];
    r13 = ctx->gpr[13];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f3 = ctx->fpr[3];
    f23 = ctx->fpr[23];
    f24 = ctx->fpr[24];
    f25 = ctx->fpr[25];
    f26 = ctx->fpr[26];
    f27 = ctx->fpr[27];
    f28 = ctx->fpr[28];
    f29 = ctx->fpr[29];
    f30 = ctx->fpr[30];
    f31 = ctx->fpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
    r3 = MemoryInline::FlatRead32((r13 + -NSMBW_SDA13(22736)));
    r3 = (r3 + 589824);
    r0 = MemoryInline::FlatRead8((r3 + 2506));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_8007AAF0:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_8007AB10;
    }
}

loc_8007AAF4:
{
    r4 = (r30 + 589824);
    r3 = r30;
    r5 = r4;
    r6 = 1;
    r4 = (r4 + 2572);
    r5 = (r5 + 2520);
    ctx->lr = NSMBW_VA(0x8007AB10u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[11] = r11;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[23] = f23;
    ctx->fpr[24] = f24;
    ctx->fpr[25] = f25;
    ctx->fpr[26] = f26;
    ctx->fpr[27] = f27;
    ctx->fpr[28] = f28;
    ctx->fpr[29] = f29;
    ctx->fpr[30] = f30;
    ctx->fpr[31] = f31;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x8007CD70u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r6 = ctx->gpr[6];
    r11 = ctx->gpr[11];
    r26 = ctx->gpr[26];
    r27 = ctx->gpr[27];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    f3 = ctx->fpr[3];
    f23 = ctx->fpr[23];
    f24 = ctx->fpr[24];
    f25 = ctx->fpr[25];
    f26 = ctx->fpr[26];
    f27 = ctx->fpr[27];
    f28 = ctx->fpr[28];
    f29 = ctx->fpr[29];
    f30 = ctx->fpr[30];
    f31 = ctx->fpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
}

loc_8007AB10:
{
    r11 = (r1 + 32);
    // psq_load w=0 quant=0 (using PPC_PsqL)
    guest_range_1 = MemoryInline::ResolveRangeHost((r1 + 8), 0, 176u, true, false);
    f31.d = MemoryInline::ReadResolvedFloat64(guest_range_1, 152u, (r1 + 160));
    // psq_load w=0 quant=0 (using PPC_PsqL)
    f30.d = MemoryInline::ReadResolvedFloat64(guest_range_1, 136u, (r1 + 144));
    // psq_load w=0 quant=0 (using PPC_PsqL)
    f29.d = MemoryInline::ReadResolvedFloat64(guest_range_1, 120u, (r1 + 128));
    // psq_load w=0 quant=0 (using PPC_PsqL)
    f28.d = MemoryInline::ReadResolvedFloat64(guest_range_1, 104u, (r1 + 112));
    // psq_load w=0 quant=0 (using PPC_PsqL)
    f27.d = MemoryInline::ReadResolvedFloat64(guest_range_1, 88u, (r1 + 96));
    // psq_load w=0 quant=0 (using PPC_PsqL)
    f26.d = MemoryInline::ReadResolvedFloat64(guest_range_1, 72u, (r1 + 80));
    // psq_load w=0 quant=0 (using PPC_PsqL)
    f25.d = MemoryInline::ReadResolvedFloat64(guest_range_1, 56u, (r1 + 64));
    // psq_load w=0 quant=0 (using PPC_PsqL)
    f24.d = MemoryInline::ReadResolvedFloat64(guest_range_1, 40u, (r1 + 48));
    // psq_load w=0 quant=0 (using PPC_PsqL)
    f23.d = MemoryInline::ReadResolvedFloat64(guest_range_1, 24u, (r1 + 32));
    // inline leaf 0x802DD0AC (7 guest instruction(s))
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_1, 0u);
        if (resolved_pair.valid) {
            r26 = resolved_pair.first;
            r27 = resolved_pair.second;
        } else {
            r26 = MemoryInline::ReadResolved32(guest_range_1, 0u, (r11 + -24));
            r27 = MemoryInline::ReadResolved32(guest_range_1, 4u, (r11 + -20));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_1, 8u);
        if (resolved_pair.valid) {
            r28 = resolved_pair.first;
            r29 = resolved_pair.second;
        } else {
            r28 = MemoryInline::ReadResolved32(guest_range_1, 8u, (r11 + -16));
            r29 = MemoryInline::ReadResolved32(guest_range_1, 12u, (r11 + -12));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_1, 16u);
        if (resolved_pair.valid) {
            r30 = resolved_pair.first;
            r31 = resolved_pair.second;
        } else {
            r30 = MemoryInline::ReadResolved32(guest_range_1, 16u, (r11 + -8));
            r31 = MemoryInline::ReadResolved32(guest_range_1, 20u, (r11 + -4));
        }
    }
    // end of inlined leaf 0x802DD0AC
    r0 = MemoryInline::ReadResolved32(guest_range_1, 172u, (r1 + 180));
    ctx->lr = r0;
    r1 = (r1 + 176);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[11] = r11;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[23] = f23;
    ctx->fpr[24] = f24;
    ctx->fpr[25] = f25;
    ctx->fpr[26] = f26;
    ctx->fpr[27] = f27;
    ctx->fpr[28] = f28;
    ctx->fpr[29] = f29;
    ctx->fpr[30] = f30;
    ctx->fpr[31] = f31;
    ctx->cr = cr;
    return;
}

}

// RECOMP_GUEST_ABI gpr_read=0xFFFFFFFF gpr_write=0xFFFFFFFF gpr_return=0x00000018 fpr_read=0xFFFFFFFF fpr_write=0xFFFFFFFF fpr_return=0x00000002 cr_read=0xFF cr_write=0xFF xer_read=1 xer_write=1 fence=1
// RECOMP_REGISTRATION base 0x8007A6C0 func_8007A6C0 preserves=false fpr_mask=0xFF800000

PPC_NATIVE_OVERRIDE_VOID(8007A6C0, NsmbwWideCamera_8007A6C0, (CpuContext* ctx), (ctx));
