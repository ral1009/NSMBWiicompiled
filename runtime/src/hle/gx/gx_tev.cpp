// gx_tev.cpp - TEV Stage Configuration
#include "gx_internal.h"
#include "runtime_log.h"

// Aurora's CHECK() on these tev array indices compiles to nothing under NDEBUG, so this
// guest->native boundary must reject out-of-range IDs itself; a bad ID here is a malformed
// display list, never legitimate traffic.
namespace {

bool GxTevIdOk(uint32_t value, uint32_t limit, const char* what) {
    if (value < limit) return true;
    RT_LOGF(RT_TAG_GX, "%s out of range: %u (max %u), ignoring\n", what, value, limit - 1u);
    return false;
}

inline bool TevStageOk(uint32_t s) { return GxTevIdOk(s, GX_MAX_TEVSTAGE, "TEV stage"); }
inline bool TevRegOk(uint32_t id) { return GxTevIdOk(id, GX_MAX_TEVREG, "TEV register"); }
inline bool TevKColorOk(uint32_t id) { return GxTevIdOk(id, GX_MAX_KCOLOR, "TEV konstant color"); }
inline bool TevSwapOk(uint32_t id) { return GxTevIdOk(id, GX_MAX_TEVSWAP, "TEV swap selector"); }

} // namespace

NsmbwLastTevStageDiag g_nsmbwLastTevStage[16]{};

// ============================================================================
// TEV Stage Count and Order
// ============================================================================

extern "C" void GX__SetNumTevStages_801722a8(uint32_t n) {
    // GXSetNumTevStages takes a count, not an index, so the inclusive bound is
    // GX_MAX_TEVSTAGE itself.
    if (n > GX_MAX_TEVSTAGE) {
        RT_LOGF(RT_TAG_GX, "GXSetNumTevStages: invalid count %u, ignoring\n", n);
        return;
    }
    GXSetNumTevStages((u8)n);
}
PPC_NATIVE_OVERRIDE_VOID(801722a8, GX__SetNumTevStages_801722a8, (uint32_t n), (n));

extern "C" void GX__SetTevOp_80171c4c(uint32_t s, uint32_t m) {
    if (!TevStageOk(s)) return;
    GXSetTevOp((GXTevStageID)s, (GXTevMode)m);
}
PPC_NATIVE_OVERRIDE_VOID(80171c4c, GX__SetTevOp_80171c4c, (uint32_t s, uint32_t m), (s, m));

extern "C" void GX__SetTevOrder_8017214c(uint32_t s, uint32_t c, uint32_t m, uint32_t col) {
    if (!TevStageOk(s)) return;
    g_nsmbwLastTevStage[s].texCoord = c;
    g_nsmbwLastTevStage[s].texMap = m;
    g_nsmbwLastTevStage[s].channel = col;
    ++g_nsmbwLastTevStage[s].orderSetCount;
    GXSetTevOrder((GXTevStageID)s, (GXTexCoordID)c, (GXTexMapID)m, (GXChannelID)col);
}
PPC_NATIVE_OVERRIDE_VOID(8017214c, GX__SetTevOrder_8017214c, (uint32_t s, uint32_t c, uint32_t m, uint32_t col), (s, c, m, col));

// ============================================================================
// TEV Color/Alpha Inputs
// ============================================================================

// KNOWN BUG (not fixed here): these four addresses (ColorIn/AlphaIn/ColorOp/AlphaOp) were
// all copy-pasted from MKW's HLE table without re-verifying against NSMBW's own
// function_map.txt. The real, confirmed NSMBW addresses (function_map.txt:
// "GXSetTevColorIn"/"GXSetTevAlphaIn"/"GXSetTevColorOp"/"GXSetTevAlphaOp") are
// 0x801C8430/0x801C8470/0x801C84B0/0x801C8510 - none of the 0x80171xxx addresses below
// correspond to real function entry points in this build (that whole range disassembles as
// unrelated list/allocator code), so these overrides are silently dead for NSMBW; the
// correct TEV routing seen in diagnostics comes from the real functions running as plain
// translated code, unassisted by any of these. Pointing PPC_NATIVE_OVERRIDE at the real
// addresses directly breaks the NSMBW build (linker: duplicate symbol against the
// translator's own auto-generated func_801C8430 etc.) - this file lives in the shared
// runtime/src/hle tree, which the NSMBW shard generator (Translator.Cli
// emit-nsmbw-build-shards --native-source-dir projects/nsmbw/native) does not scan for
// exclusions, unlike projects/nsmbw/native/*.cpp. Properly fixing this means moving these
// overrides into a project-specific native/ source file and regenerating the shard
// manifest, not just correcting the address inline here.
extern "C" void GX__SetTevColorIn_80171ce0(uint32_t s, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    if (!TevStageOk(s)) return;
    g_nsmbwLastTevStage[s].colorA = a;
    g_nsmbwLastTevStage[s].colorB = b;
    g_nsmbwLastTevStage[s].colorC = c;
    g_nsmbwLastTevStage[s].colorD = d;
    ++g_nsmbwLastTevStage[s].colorSetCount;
    GXSetTevColorIn((GXTevStageID)s, (GXTevColorArg)a, (GXTevColorArg)b, (GXTevColorArg)c, (GXTevColorArg)d);
}
PPC_NATIVE_OVERRIDE_VOID(80171ce0, GX__SetTevColorIn_80171ce0, (uint32_t s, uint32_t a, uint32_t b, uint32_t c, uint32_t d), (s, a, b, c, d));

extern "C" void GX__SetTevAlphaIn_80171d20(uint32_t s, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    if (!TevStageOk(s)) return;
    g_nsmbwLastTevStage[s].alphaA = a;
    g_nsmbwLastTevStage[s].alphaB = b;
    g_nsmbwLastTevStage[s].alphaC = c;
    g_nsmbwLastTevStage[s].alphaD = d;
    ++g_nsmbwLastTevStage[s].alphaSetCount;
    GXSetTevAlphaIn((GXTevStageID)s, (GXTevAlphaArg)a, (GXTevAlphaArg)b, (GXTevAlphaArg)c, (GXTevAlphaArg)d);
}
PPC_NATIVE_OVERRIDE_VOID(80171d20, GX__SetTevAlphaIn_80171d20, (uint32_t s, uint32_t a, uint32_t b, uint32_t c, uint32_t d), (s, a, b, c, d));

// ============================================================================
// TEV Color/Alpha Operations
// ============================================================================

extern "C" void GX__SetTevColorOp_80171d60(uint32_t s, uint32_t op, uint32_t b, uint32_t sc, uint32_t cl, uint32_t or_) {
    if (!TevStageOk(s) || !TevRegOk(or_)) return;
    GXSetTevColorOp((GXTevStageID)s, (GXTevOp)op, (GXTevBias)b, (GXTevScale)sc, (GXBool)cl, (GXTevRegID)or_);
}
PPC_NATIVE_OVERRIDE_VOID(80171d60, GX__SetTevColorOp_80171d60, (uint32_t s, uint32_t op, uint32_t b, uint32_t sc, uint32_t cl, uint32_t or_), (s, op, b, sc, cl, or_));

extern "C" void GX__SetTevAlphaOp_80171db8(uint32_t s, uint32_t op, uint32_t b, uint32_t sc, uint32_t cl, uint32_t or_) {
    if (!TevStageOk(s) || !TevRegOk(or_)) return;
    GXSetTevAlphaOp((GXTevStageID)s, (GXTevOp)op, (GXTevBias)b, (GXTevScale)sc, (GXBool)cl, (GXTevRegID)or_);
}
PPC_NATIVE_OVERRIDE_VOID(80171db8, GX__SetTevAlphaOp_80171db8, (uint32_t s, uint32_t op, uint32_t b, uint32_t sc, uint32_t cl, uint32_t or_), (s, op, b, sc, cl, or_));

// ============================================================================
// TEV Color Registers
// ============================================================================

extern "C" void GX__SetTevColor_80171e10(uint32_t id, uint32_t cp) {
    if (!TevRegOk(id)) return;
    const uint8_t* p=Memory::GetPointer(cp, 4); GXColor c; c.r=p[0]; c.g=p[1]; c.b=p[2]; c.a=p[3];
    GXSetTevColor((GXTevRegID)id, c);
}
// KNOWN BUG (not fixed here): this override targets 0x80171e10, MKW's address for
// GXSetTevColor, copy-pasted without re-verifying against NSMBW's own function_map.txt.
// Disassembly confirmed 0x80171e10 is mid-function inside an unrelated NSMBW
// list/allocator routine (func_80171DA0), not a real function entry point, so this override
// silently never fires for NSMBW. The real, confirmed NSMBW GXSetTevColor - verified via
// disassembly of original/wiimj2d.dol (matches the RA/BG BP-register-pair pattern with the
// SDK's redundant-write-3x quirk) - is at 0x801C8570. Pointing this override there directly
// breaks the NSMBW build (linker: duplicate symbol against the translator's own
// auto-generated func_801C8570) because this file lives in the shared runtime/src/hle tree,
// which the NSMBW shard generator does not scan for override exclusions (unlike
// projects/nsmbw/native/*.cpp) - see the identical note above GXSetTevColorIn.
PPC_NATIVE_OVERRIDE_VOID(80171e10, GX__SetTevColor_80171e10, (uint32_t id, uint32_t cp), (id, cp));

extern "C" void GX__SetTevColorS10_80171e70(uint32_t id, uint32_t cp) {
    if (!TevRegOk(id)) return;
    const uint8_t* p=Memory::GetPointer(cp, 8); GXColorS10 c;
    c.r=(p[0]<<8)|p[1]; c.g=(p[2]<<8)|p[3]; c.b=(p[4]<<8)|p[5]; c.a=(p[6]<<8)|p[7];
    GXSetTevColorS10((GXTevRegID)id, c);
}
#ifndef MKW_RUNTIME_PRODUCT_NSMBW
// Not registered for NSMBW: this MKW address is a live, unrelated NSMBW function
// (GX::SetTevColorS10 -> NSMBW func_80171E70). The lowercase hex in the macro means the generated
// func_ symbol never collided with the translator's uppercase one, so this went
// unnoticed - but REGISTER_NATIVE_FUNCTION still binds the address, which would send
// any indirect call to that NSMBW function into MKW's GX code.
PPC_NATIVE_OVERRIDE_VOID(80171e70, GX__SetTevColorS10_80171e70, (uint32_t id, uint32_t cp), (id, cp));
#endif

extern "C" void GX__SetTevKColor_80171ed4(uint32_t id, uint32_t cp) {
    if (!TevKColorOk(id)) return;
    const uint8_t* p=Memory::GetPointer(cp, 4); GXColor c; c.r=p[0]; c.g=p[1]; c.b=p[2]; c.a=p[3];
    GXSetTevKColor((GXTevKColorID)id, c);
}
PPC_NATIVE_OVERRIDE_VOID(80171ed4, GX__SetTevKColor_80171ed4, (uint32_t id, uint32_t cp), (id, cp));

extern "C" void GX__SetTevKColorSel_80171f30(uint32_t s, uint32_t sel) { if (!TevStageOk(s)) return; GXSetTevKColorSel((GXTevStageID)s, (GXTevKColorSel)sel); }
PPC_NATIVE_OVERRIDE_VOID(80171f30, GX__SetTevKColorSel_80171f30, (uint32_t s, uint32_t sel), (s, sel));

extern "C" void GX__SetTevKAlphaSel_80171f80(uint32_t s, uint32_t sel) { if (!TevStageOk(s)) return; GXSetTevKAlphaSel((GXTevStageID)s, (GXTevKAlphaSel)sel); }
PPC_NATIVE_OVERRIDE_VOID(80171f80, GX__SetTevKAlphaSel_80171f80, (uint32_t s, uint32_t sel), (s, sel));

// ============================================================================
// TEV Swap Tables
// ============================================================================

extern "C" void GX__SetTevSwapModeTable_8017200c(uint32_t id, uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    if (!TevSwapOk(id)) return;
    GXSetTevSwapModeTable((GXTevSwapSel)id, (GXTevColorChan)r, (GXTevColorChan)g, (GXTevColorChan)b, (GXTevColorChan)a);
}
PPC_NATIVE_OVERRIDE_VOID(8017200c, GX__SetTevSwapModeTable_8017200c, (uint32_t id, uint32_t r, uint32_t g, uint32_t b, uint32_t a), (id, r, g, b, a));

extern "C" void GX__SetTevSwapMode_80171fd0(uint32_t s, uint32_t rs, uint32_t ts) {
    if (!TevStageOk(s) || !TevSwapOk(rs) || !TevSwapOk(ts)) return;
    GXSetTevSwapMode((GXTevStageID)s, (GXTevSwapSel)rs, (GXTevSwapSel)ts);
}
PPC_NATIVE_OVERRIDE_VOID(80171fd0, GX__SetTevSwapMode_80171fd0, (uint32_t s, uint32_t rs, uint32_t ts), (s, rs, ts));
