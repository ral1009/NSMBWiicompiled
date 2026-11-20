// Ultrawide: centre the camera in zones narrower than the view (World 3-3's pipe room at 21:9,
// 2026-11-19: the camera sat at the room's right limit, so all the extra width was on the left).
//
// dBg_c::getLeftLimit (0x80078A70) and getRightLimit (0x80078C10) give the zone's left and right
// edges; every camera mode clamps the camera's left edge to [left, right - view width]. In a zone
// narrower than the view that range is empty and each mode resolves it its own way (0x8007A6C0, the
// one nsmbw_wide_camera.cpp patches, is only one of them; the pipe room's camera uses another). Here
// both limits are moved apart symmetrically to exactly one view width around the zone's centre, so
// every mode's clamp lands on the centred position. Zones at least as wide as the view are untouched.
// The view width is dBgParameter_c::ms_Instance_p (0x8042A0E0) mSize.x (+0x40).
// The originals run unchanged as structural copies (build/nsmbw/functions/func_80078A70.cpp,
// func_80078C10.cpp); only the wrappers below are new. NSMBW_WIDE_CAMERA_CENTRE=0 turns it off.
#include "nsmbw_va.h"
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include "recomp_mod_loader.h"

static void OrigLeft(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r1_psq_tmp_0 = 0;
    uint32_t r1_psq_tmp_1 = 0;
    uint32_t r1_psq_tmp_2 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r2 = ctx->gpr[2];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r13 = ctx->gpr[13];
    uint32_t r31 = ctx->gpr[31];
    PPC_FPR f0 = ctx->fpr[0];
    PPC_FPR f1 = ctx->fpr[1];
    PPC_FPR f2 = ctx->fpr[2];
    PPC_FPR f29 = ctx->fpr[29];
    PPC_FPR f30 = ctx->fpr[30];
    PPC_FPR f31 = ctx->fpr[31];
    uint32_t cr = ctx->cr;
    uint32_t xer = ctx->xer;

    goto loc_80078A70;

loc_80078A70:
{
    MemoryInline::FlatWriteRam32((r1 + -64), r1);
    r1 = (r1 + -64);
    r0 = ctx->lr;
    r4 = (r3 + 589824);
    MemoryInline::FlatWriteRam32((r1 + 68), r0);
    MemoryInline::FlatWriteRamFloat64((r1 + 48), f31.d);
    // psq_store w=0 quant=0 (using PPC_PsqSt)
    r1_psq_tmp_0 = (r1 + 56);
    PPC_PsqStStackInline<0u, 0u>(ctx, r1_psq_tmp_0, PPC_PsFromScalarInline(f31.d));
    MemoryInline::FlatWriteRamFloat64((r1 + 32), f30.d);
    // psq_store w=0 quant=0 (using PPC_PsqSt)
    r1_psq_tmp_1 = (r1 + 40);
    PPC_PsqStStackInline<0u, 0u>(ctx, r1_psq_tmp_1, PPC_PsFromScalarInline(f30.d));
    MemoryInline::FlatWriteRamFloat64((r1 + 16), f29.d);
    // psq_store w=0 quant=0 (using PPC_PsqSt)
    r1_psq_tmp_2 = (r1 + 24);
    PPC_PsqStStackInline<0u, 0u>(ctx, r1_psq_tmp_2, PPC_PsFromScalarInline(f29.d));
    MemoryInline::FlatWriteRam32((r1 + 12), r31);
    r31 = r3;
    r0 = MemoryInline::FlatRead8((r4 + 254));
    f29.d = MemoryInline::FlatReadFloat32((r4 + 48));
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(1));
}

loc_80078AAC:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80078B08;
    }
}

loc_80078AB0:
{
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(4));
}

loc_80078AB4:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80078B08;
    }
}

loc_80078AB8:
{
    f31.d = MemoryInline::FlatReadFloat32((r4 + 12));
    ctx->lr = NSMBW_VA(0x80078AC0u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x80079060u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    cr = ctx->cr;
    f0.d = MemoryInline::FlatReadFloat32((r2 + -NSMBW_SDA2(29544)));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d / f31.d));
    SetCRFloatResident(cr, 0, f0.d, f1.d);
}

loc_80078ACC:
{
    if (((cr & NSMBW_VA(0x80000000u)) == 0)) {
        goto loc_80078B08;
    }
}

loc_80078AD0:
{
    r3 = (r31 + 589824);
    f30.d = MemoryInline::FlatReadFloat32((r13 + -NSMBW_SDA13(21940)));
    f31.d = MemoryInline::FlatReadFloat32((r3 + 12));
    r3 = r31;
    ctx->lr = NSMBW_VA(0x80078AE4u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x80079060u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    cr = ctx->cr;
    f0.d = MemoryInline::FlatReadFloat32((r2 + -NSMBW_SDA2(29544)));
    f1.d = PpcFmulsInline(f1.d, f30.d);
    f2.d = MemoryInline::FlatReadFloat32((r2 + -NSMBW_SDA2(29476)));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d / f31.d));
    f0.d = PpcFmulsInline(f0.d, f30.d);
    f1.d = PpcFmulsInline(f2.d, f1.d);
    f0.d = PpcFmulsInline(f2.d, f0.d);
    f0.d = static_cast<double>(PpcForceSingleValueInline(f1.d - f0.d));
    f29.d = static_cast<double>(PpcForceSingleValueInline(f29.d + f0.d));
}

loc_80078B08:
{
    // psq_load w=0 quant=0 (using PPC_PsqL)
    f1.d = f29.d;
    f31.d = MemoryInline::FlatReadFloat64((r1 + 48));
    // psq_load w=0 quant=0 (using PPC_PsqL)
    f30.d = MemoryInline::FlatReadFloat64((r1 + 32));
    // psq_load w=0 quant=0 (using PPC_PsqL)
    f29.d = MemoryInline::FlatReadFloat64((r1 + 16));
    r31 = MemoryInline::FlatRead32((r1 + 12));
    r0 = MemoryInline::FlatRead32((r1 + 68));
    ctx->lr = r0;
    r1 = (r1 + 64);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[29] = f29;
    ctx->fpr[30] = f30;
    ctx->fpr[31] = f31;
    ctx->cr = cr;
    return;
}

}


static void OrigRight(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r1_psq_tmp_0 = 0;
    uint32_t r1_psq_tmp_1 = 0;
    uint32_t r1_psq_tmp_2 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r2 = ctx->gpr[2];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r13 = ctx->gpr[13];
    uint32_t r31 = ctx->gpr[31];
    PPC_FPR f0 = ctx->fpr[0];
    PPC_FPR f1 = ctx->fpr[1];
    PPC_FPR f2 = ctx->fpr[2];
    PPC_FPR f29 = ctx->fpr[29];
    PPC_FPR f30 = ctx->fpr[30];
    PPC_FPR f31 = ctx->fpr[31];
    uint32_t cr = ctx->cr;
    uint32_t xer = ctx->xer;

    goto loc_80078C10;

loc_80078C10:
{
    MemoryInline::FlatWriteRam32((r1 + -64), r1);
    r1 = (r1 + -64);
    r0 = ctx->lr;
    r4 = (r3 + 589824);
    MemoryInline::FlatWriteRam32((r1 + 68), r0);
    MemoryInline::FlatWriteRamFloat64((r1 + 48), f31.d);
    // psq_store w=0 quant=0 (using PPC_PsqSt)
    r1_psq_tmp_0 = (r1 + 56);
    PPC_PsqStStackInline<0u, 0u>(ctx, r1_psq_tmp_0, PPC_PsFromScalarInline(f31.d));
    MemoryInline::FlatWriteRamFloat64((r1 + 32), f30.d);
    // psq_store w=0 quant=0 (using PPC_PsqSt)
    r1_psq_tmp_1 = (r1 + 40);
    PPC_PsqStStackInline<0u, 0u>(ctx, r1_psq_tmp_1, PPC_PsFromScalarInline(f30.d));
    MemoryInline::FlatWriteRamFloat64((r1 + 16), f29.d);
    // psq_store w=0 quant=0 (using PPC_PsqSt)
    r1_psq_tmp_2 = (r1 + 24);
    PPC_PsqStStackInline<0u, 0u>(ctx, r1_psq_tmp_2, PPC_PsFromScalarInline(f29.d));
    MemoryInline::FlatWriteRam32((r1 + 12), r31);
    r31 = r3;
    r0 = MemoryInline::FlatRead8((r4 + 254));
    f29.d = MemoryInline::FlatReadFloat32((r4 + 52));
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(1));
}

loc_80078C4C:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80078CA8;
    }
}

loc_80078C50:
{
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(4));
}

loc_80078C54:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_80078CA8;
    }
}

loc_80078C58:
{
    f31.d = MemoryInline::FlatReadFloat32((r4 + 12));
    ctx->lr = NSMBW_VA(0x80078C60u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x80079060u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    cr = ctx->cr;
    f0.d = MemoryInline::FlatReadFloat32((r2 + -NSMBW_SDA2(29544)));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d / f31.d));
    SetCRFloatResident(cr, 0, f0.d, f1.d);
}

loc_80078C6C:
{
    if (((cr & NSMBW_VA(0x80000000u)) == 0)) {
        goto loc_80078CA8;
    }
}

loc_80078C70:
{
    r3 = (r31 + 589824);
    f30.d = MemoryInline::FlatReadFloat32((r13 + -NSMBW_SDA13(21940)));
    f31.d = MemoryInline::FlatReadFloat32((r3 + 12));
    r3 = r31;
    ctx->lr = NSMBW_VA(0x80078C84u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->cr = cr;
    InvokeDirectCpu<NSMBW_VA(0x80079060u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r31 = ctx->gpr[31];
    f0 = ctx->fpr[0];
    f1 = ctx->fpr[1];
    f2 = ctx->fpr[2];
    cr = ctx->cr;
    f0.d = MemoryInline::FlatReadFloat32((r2 + -NSMBW_SDA2(29544)));
    f1.d = PpcFmulsInline(f1.d, f30.d);
    f2.d = MemoryInline::FlatReadFloat32((r2 + -NSMBW_SDA2(29476)));
    f0.d = static_cast<double>(PpcForceSingleValueInline(f0.d / f31.d));
    f0.d = PpcFmulsInline(f0.d, f30.d);
    f1.d = PpcFmulsInline(f2.d, f1.d);
    f0.d = PpcFmulsInline(f2.d, f0.d);
    f0.d = static_cast<double>(PpcForceSingleValueInline(f1.d - f0.d));
    f29.d = static_cast<double>(PpcForceSingleValueInline(f29.d - f0.d));
}

loc_80078CA8:
{
    // psq_load w=0 quant=0 (using PPC_PsqL)
    f1.d = f29.d;
    f31.d = MemoryInline::FlatReadFloat64((r1 + 48));
    // psq_load w=0 quant=0 (using PPC_PsqL)
    f30.d = MemoryInline::FlatReadFloat64((r1 + 32));
    // psq_load w=0 quant=0 (using PPC_PsqL)
    f29.d = MemoryInline::FlatReadFloat64((r1 + 16));
    r31 = MemoryInline::FlatRead32((r1 + 12));
    r0 = MemoryInline::FlatRead32((r1 + 68));
    ctx->lr = r0;
    r1 = (r1 + 64);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[31] = r31;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[29] = f29;
    ctx->fpr[30] = f30;
    ctx->fpr[31] = f31;
    ctx->cr = cr;
    return;
}

}


namespace {
// Both limits for the dBg_c in r3; the context is restored after each call.
void Limits(CpuContext* ctx, double& left, double& right) {
    const CpuContext saved = *ctx;
    OrigLeft(ctx);
    left = ctx->fpr[1].d;
    *ctx = saved;
    OrigRight(ctx);
    right = ctx->fpr[1].d;
    *ctx = saved;
}
bool Centre(double left, double right, double& outLeft, double& outRight) {
    static const bool off = [] { const char* v = std::getenv("NSMBW_WIDE_CAMERA_CENTRE"); return v != nullptr && v[0] == '0'; }();
    if (off) return false;
    const uint32_t bgp = MemoryInline::FlatRead32(NSMBW_VA(0x8042A0E0u));
    if (bgp == 0) return false;
    const double w = MemoryInline::FlatReadFloat32(bgp + 0x40u);
    if (!std::isfinite(w) || !std::isfinite(left) || !std::isfinite(right) || w <= 0.0 || right - left >= w) return false;
    const double c = (left + right) * 0.5;
    outLeft = c - w * 0.5;
    outRight = c + w * 0.5;
    return true;
}
} // namespace

extern "C" void NsmbwWideCameraLeft_80078A70(CpuContext* MKW_RESTRICT ctx) {
    double l, r, nl, nr;
    Limits(ctx, l, r);
    OrigLeft(ctx);
    if (Centre(l, r, nl, nr)) ctx->fpr[1].d = static_cast<double>(static_cast<float>(nl));
}
extern "C" void NsmbwWideCameraRight_80078C10(CpuContext* MKW_RESTRICT ctx) {
    double l, r, nl, nr;
    Limits(ctx, l, r);
    OrigRight(ctx);
    if (Centre(l, r, nl, nr)) ctx->fpr[1].d = static_cast<double>(static_cast<float>(nr));
}

PPC_NATIVE_OVERRIDE_VOID(80078A70, NsmbwWideCameraLeft_80078A70, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(80078C10, NsmbwWideCameraRight_80078C10, (CpuContext* ctx), (ctx));
