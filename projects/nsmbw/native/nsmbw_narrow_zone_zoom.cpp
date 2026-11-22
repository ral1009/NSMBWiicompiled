// Ultrawide: zoom the level camera in when the zone is narrower than the view, so the zone fills the
// width instead of showing background (pillarboxed, nsmbw_zone_pillarbox.cpp) at its sides.
// Developer's choice 2026-11-21 over widening ~114 room layouts by hand (docs/futureplans.md 1, item 9);
// a room widened later is no longer narrower than the view, so this stops applying to it by itself.
//
// 0x80078EB0 (dBg_c, no symbol) returns the camera's zoom factor Z: 0x80078D00 looks it up from the
// zone's zoom type / ID (dBg_c +0x900FE / +0x900FF, set from dCdFile_c::getAreaZoomType/ID at zone
// entry, 0x801549E0) and 0x80078860 adjusts it. Every caller turns it into the visible rectangle
// as base size * Z: 0x801549E0 stores 1/Z at dBg_c +0x9000C and dBgParameter_c mSize.x = base width
// (r13-21940, 536.4 at 21:9 after ApplyAspectSetting) * Z; the per-frame camera 0x8007A6C0, the
// limits, the tile drawer's column count and the actor spawner all derive from that. Its 7 callers
// (0x80079BF0, 0x80079E80, 0x8007A6C0, 0x8007AC40, 0x8007E0E0, 0x801549E0, 0x801550A0) call it
// (it is not a leaf, so not inlined), so this override sees every use.
//
// Here, after the original body: if base width * Z is wider than the current zone, Z is lowered to
// zone width / base width (the view exactly fills the zone), but never by more than the cap
// (NSMBW_NARROW_ZONE_ZOOM=<max factor>, default no cap; =0 turns this off). The view's height
// shrinks by the same factor, so a fixed one-screen room loses part of its height (survey
// 2026-11-21: 1.23-1.31x for most 512-wide bonus rooms, 3-5 tiles).
//
// The zone's width comes from the course data, not from dBg_c's zone limits (+0x90030 / +0x90034),
// because 0x801549E0 calls this while it is still setting the zone up:
//   dScStage_c::m_instance (r13-21720): mCurrFile +0x120E (the file, Reggie's "area"),
//                                        mCurrAreaNo +0x120F (the zone)
//   dCd_c::m_instance (r13-22536): dCdFile_c[mCurrFile], 944 bytes each; mpAreas +44, mAreaCount +156
//   sAreaData (24 bytes): width u16 +4, ID u8 +12 (getAreaDataP matches on the ID)
// (NSMBW-Decomp d_cd.hpp / d_s_stage.hpp; 0x801549E0 reads the same fields.)
// NSMBW_LOG_NARROW_ZONE_ZOOM=1 logs each change with both widths.
//
// Structural copy of build/nsmbw/functions/func_80078EB0.cpp (2026-11-22); the lines after the two
// calls are new.
#include "nsmbw_va.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include "recomp_mod_loader.h"

namespace {

// <0: off; 0: no cap; >1: the most the view may be narrowed by.
double ZoomCap() {
    static const double cap = [] {
        const char* v = std::getenv("NSMBW_NARROW_ZONE_ZOOM");
        if (v == nullptr || v[0] == '\0') return 0.0;
        const double d = std::atof(v);
        return d <= 0.0 ? -1.0 : (d < 1.0 ? 1.0 : d);
    }();
    return cap;
}

// The current zone's width from the course data, or 0 if it can't be found.
double CurrentZoneWidth(uint32_t r13, unsigned& zoneOut) {
    const uint32_t stage = MemoryInline::FlatRead32(r13 - NSMBW_SDA13(21720));
    const uint32_t cd = MemoryInline::FlatRead32(r13 - NSMBW_SDA13(22536));
    if (stage == 0 || cd == 0) return 0.0;
    const uint32_t file = MemoryInline::FlatRead8(stage + 0x120Eu);
    const uint32_t zone = MemoryInline::FlatRead8(stage + 0x120Fu);
    if (file >= 4u) return 0.0;
    const uint32_t cdFile = cd + file * 944u;
    const uint32_t areas = MemoryInline::FlatRead32(cdFile + 44u);
    const uint32_t count = MemoryInline::FlatRead32(cdFile + 156u);
    if (areas == 0 || count > 64u) return 0.0;
    zoneOut = zone;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t a = areas + i * 24u;
        if (MemoryInline::FlatRead8(a + 12u) == zone) return MemoryInline::FlatRead16(a + 4u);
    }
    return 0.0;
}

void Log(unsigned zone, double zoneW, double base, double z, double newZ, uint32_t lr) {
    static const bool on = std::getenv("NSMBW_LOG_NARROW_ZONE_ZOOM") != nullptr;
    static unsigned lastZone = ~0u;
    static float lastZ = -1.0f, lastNew = -1.0f;
    if (!on) return;
    if (zone == lastZone && static_cast<float>(z) == lastZ && static_cast<float>(newZ) == lastNew) return;
    lastZone = zone; lastZ = static_cast<float>(z); lastNew = static_cast<float>(newZ);
    const uint32_t bg = MemoryInline::FlatRead32(NSMBW_VA(0x8042A0B0u));
    const float zl = bg ? MemoryInline::FlatReadFloat32(bg + 0x90030u) : 0.0f;
    const float zr = bg ? MemoryInline::FlatReadFloat32(bg + 0x90034u) : 0.0f;
    std::fprintf(stderr,
                 "[nsmbw][zonezoom] zone %u width %.0f (dBg limits %.1f..%.1f = %.1f) base %.2f: zoom %.4f -> %.4f, view %.1f -> %.1f (x%.3f) caller %08X\n",
                 zone, zoneW, zl, zr, zr - zl, base, z, newZ, base * z, base * newZ, z / newZ, lr);
}

void AdjustZoom(CpuContext* ctx, uint32_t lr) {
    const double cap = ZoomCap();
    if (cap < 0.0) return;
    const uint32_t r13 = ctx->gpr[13];
    const double base = MemoryInline::FlatReadFloat32(r13 - NSMBW_SDA13(21940));
    // Only when the view is wider than 16:9's 408.70 (ApplyAspectSetting); no zone is narrower than
    // the 16:9 view (survey), so this is only a guard.
    if (!(base > 409.0)) return;
    const double z = ctx->fpr[1].d;
    if (!std::isfinite(z) || z <= 0.0) return;
    unsigned zone = 0;
    const double zoneW = CurrentZoneWidth(r13, zone);
    if (zoneW < 64.0) return;
    double newZ = z;
    if (base * z > zoneW + 0.5) {
        newZ = zoneW / base;
        if (cap > 0.0 && z / newZ > cap) newZ = z / cap;
    }
    Log(zone, zoneW, base, z, newZ, lr);
    if (newZ != z) ctx->fpr[1].d = static_cast<double>(static_cast<float>(newZ));
}

} // namespace

extern "C" void NsmbwNarrowZoneZoom_80078EB0(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r31 = ctx->gpr[31];
    const uint32_t callerLr = ctx->lr;

    MemoryInline::FlatWriteRam32((r1 + -16), r1);
    r1 = (r1 + -16);
    r0 = ctx->lr;
    MemoryInline::FlatWriteRam32((r1 + 20), r0);
    MemoryInline::FlatWriteRam32((r1 + 12), r31);
    r31 = r3;
    ctx->lr = NSMBW_VA(0x80078EC8u);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[31] = r31;
    InvokeDirectCpu<NSMBW_VA(0x80078D00u)>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r31 = ctx->gpr[31];
    r3 = r31;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    InvokeDirectCpu<NSMBW_VA(0x80078860u)>(ctx);
    r0 = ctx->gpr[0];
    r3 = ctx->gpr[3];
    AdjustZoom(ctx, callerLr); // new: f1 is the zoom factor
    r0 = MemoryInline::FlatRead32((r1 + 20));
    r31 = MemoryInline::FlatRead32((r1 + 12));
    ctx->lr = r0;
    r1 = (r1 + 16);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[31] = r31;
}

PPC_NATIVE_OVERRIDE_VOID(80078EB0, NsmbwNarrowZoneZoom_80078EB0, (CpuContext* ctx), (ctx));
