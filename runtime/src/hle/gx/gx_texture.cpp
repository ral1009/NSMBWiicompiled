// gx_texture.cpp - Texture Object and TLUT Functions
#include "gx_internal.h"
#include <aurora/env.hpp>
#include "runtime_log.h"

#include <algorithm>
#include <map>
#include <unordered_set>
#include <cstdlib>

// DIAGNOSTIC (temporary): live TEV alpha-combiner state mirrored by aurora-main's
// command_processor.cpp - see that file's g_nsmbwLiveTevAlpha* comment for why this exists and
// why it's plain extern "C" globals instead of reading aurora's internal gx.hpp directly. Remove
// once the P_stripe_00 investigation (NSMBW_TARGET_WRAP_TEV below) resolves.
extern "C" {
extern uint32_t g_nsmbwLiveTevAlphaA[16];
extern uint32_t g_nsmbwLiveTevAlphaB[16];
extern uint32_t g_nsmbwLiveTevAlphaC[16];
extern uint32_t g_nsmbwLiveTevAlphaD[16];
extern uint32_t g_nsmbwLiveTevTexMap[16];
extern float g_nsmbwLiveTevRegAlpha[4];
extern uint32_t g_nsmbwLiveNumTevStages;
extern uint32_t g_nsmbwLiveTevChannelId[16];
extern uint32_t g_nsmbwLiveChanMatSrc[4];
extern float g_nsmbwLiveChanMatColorA[4];
extern float g_nsmbwLiveChanAmbColorA[4];
// DIAGNOSTIC (temporary): live TEV COLOR-combiner routing, mirrored the same way as the alpha
// state above but never captured before now - see command_processor.cpp's g_nsmbwLiveTevColor*
// comment. Used by NSMBW_LOG_COLOR_TEV below to check whether panes across every screen (not
// just P_stripe_00) are actually sampling their bound texture's color (GX_CC_TEXC=8) or getting a
// constant/vertex color instead (GX_CC_RASC=0, GX_CC_KONST=6, GX_CC_ONE=15, GX_CC_ZERO=16, ...).
extern uint32_t g_nsmbwLiveTevColorA[16];
extern uint32_t g_nsmbwLiveTevColorB[16];
extern uint32_t g_nsmbwLiveTevColorC[16];
extern uint32_t g_nsmbwLiveTevColorD[16];
extern float g_nsmbwLiveTevRegColorR[4];
extern float g_nsmbwLiveTevRegColorG[4];
extern float g_nsmbwLiveTevRegColorB[4];
// FIX: forces aurora's consecutive-draw merge optimization to start a fresh batch after this
// texture upload, since that optimization's stateDirty flag is otherwise never set by this native
// upload path - see the function's own definition (aurora-main/lib/gx/command_processor.cpp) for
// the full reasoning.
void NsmbwInvalidateMergeStateForTextureUpload();
}

// Helper to write GXTexObj structure to guest memory in SDK format
// This is needed because other code may read the structure directly
static void WriteGuestTexObj(uint32_t addr, uint32_t dataAddr, uint16_t width, uint16_t height, 
                             uint32_t format, uint32_t wrapS, uint32_t wrapT, bool mipmap,
                             bool writeTlut, uint32_t tlut) {
    dataAddr = CanonicalizeGxMainRamAddress(dataAddr);
    for (int i = 0; i < 8; i++) {
        Memory::Write32(addr + i * 4, 0);
    }

    uint32_t word0 = (wrapS & 0x3u) | ((wrapT & 0x3u) << 2) | 0x10u;
    uint32_t word1 = 0;
    if (!mipmap) {
        word0 = (word0 & 0xFFFFFF10u) | (wrapS & 0x3u) | ((wrapT & 0x3u) << 2) | 0x90u;
    } else {
        word0 = (word0 & 0xFFFFFF10u) | (wrapS & 0x3u) | ((wrapT & 0x3u) << 2) |
                (((format - 8u) < 3u) ? 0xB0u : 0xD0u);
        const uint32_t maxDim = std::max<uint32_t>(width, height);
        const uint32_t maxLod = maxDim > 0 ? (31u - static_cast<uint32_t>(__builtin_clz(maxDim))) : 0u;
        word1 |= (std::min<uint32_t>(maxLod * 16u, 0xFFu) << 8);
    }

    uint32_t blockShiftX = 2;
    uint32_t blockShiftY = 2;
    uint8_t blockType = 2;
    switch (format & 0xFu) {
    case 0:
    case 8:
        blockShiftX = 3;
        blockShiftY = 3;
        blockType = 1;
        break;
    case 1:
    case 2:
    case 9:
        blockShiftX = 3;
        blockShiftY = 2;
        blockType = 2;
        break;
    case 3:
    case 4:
    case 5:
    case 10:
        blockShiftX = 2;
        blockShiftY = 2;
        blockType = 2;
        break;
    case 6:
        blockShiftX = 2;
        blockShiftY = 2;
        blockType = 3;
        break;
    case 14:
        blockShiftX = 3;
        blockShiftY = 3;
        blockType = 0;
        break;
    default:
        blockShiftX = 2;
        blockShiftY = 2;
        blockType = 2;
        break;
    }

    const uint32_t word2 =
        ((width - 1u) & 0x3FFu) |
        (((height - 1u) & 0x3FFu) << 10) |
        ((format & 0xFu) << 20);
    const uint32_t word3 = (dataAddr >> 5) & 0x00FFFFFFu;
    const uint32_t blocksX = (static_cast<uint32_t>(width) + ((1u << blockShiftX) - 1u)) >> blockShiftX;
    const uint32_t blocksY = (static_cast<uint32_t>(height) + ((1u << blockShiftY) - 1u)) >> blockShiftY;
    const uint16_t blockCount = static_cast<uint16_t>((blocksX * blocksY) & 0x7FFFu);
    uint8_t flags = mipmap ? 0x03u : 0x02u;

    Memory::Write32(addr + 0x00, word0);
    Memory::Write32(addr + 0x04, word1);
    Memory::Write32(addr + 0x08, word2);
    Memory::Write32(addr + 0x0C, word3);
    Memory::Write32(addr + 0x14, format);
    if (writeTlut) {
        Memory::Write32(addr + 0x18, tlut);
    }
    Memory::Write16(addr + 0x1C, blockCount);
    Memory::Write8(addr + 0x1E, blockType);
    Memory::Write8(addr + 0x1F, flags);
}

// SDK min-filter enum -> the hardware encoding stored in GXTexObj word0[7:5]
// (GX2HWFiltConv). Entries 6 and 7 have no SDK filter and read back as 0; the
// inverse table (HW -> SDK) lives in gx_objects.cpp.
static const uint8_t kGxToHwMinFilter[8] = {0, 4, 1, 5, 2, 6, 0, 0};

// Helper to update LOD info in guest GXTexObj structure
static void WriteGuestTexObjLOD(uint32_t addr, uint32_t minFilt, uint32_t magFilt,
                                 float minLod, float maxLod, float lodBias,
                                 bool biasClamp, bool edgeLod, uint32_t maxAniso) {
    // Read existing word0 and update relevant bits
    uint32_t word0 = Memory::Read32(addr + 0x00);

    // bits [4] = magFilter, bits [7:5] = hardware minFilter encoding
    uint32_t minFiltBits = (minFilt < 6u) ? kGxToHwMinFilter[minFilt] : 4;
    word0 = (word0 & ~0xF0) | ((magFilt & 1) << 4) | ((minFiltBits & 7) << 5);
    
    // bits [8] = edgeLod (inverted), bits [17:9] = lodBias (signed, scaled by 32)
    int8_t biasScaled = (int8_t)(lodBias * 32.0f);
    word0 = (word0 & ~0x3FF00) | ((edgeLod ? 0 : 1) << 8) | ((uint32_t)(uint8_t)biasScaled << 9);
    
    // bits [20:19] = maxAniso, bit [21] = biasClamp
    word0 = (word0 & ~0x380000) | ((maxAniso & 0x3) << 19) | ((biasClamp ? 1 : 0) << 21);
    
    Memory::Write32(addr + 0x00, word0);
    
    // word1: bits [7:0] = minLod * 16, bits [15:8] = maxLod * 16
    uint8_t minLodScaled = (uint8_t)(minLod * 16.0f);
    uint8_t maxLodScaled = (uint8_t)(maxLod * 16.0f);
    uint32_t word1 = Memory::Read32(addr + 0x04);
    word1 = (word1 & 0xFFFF0000) | minLodScaled | ((uint32_t)maxLodScaled << 8);
    Memory::Write32(addr + 0x04, word1);
}

// ============================================================================
// Texture Object Initialization
// ============================================================================

static uint32_t g_unsupportedFormatLogCount = 0;

// Must cover every format aurora's convert_texture() decodes; anything missing here is refused
// and never reaches aurora (GX_TF_C14X2 was once missing despite aurora decoding it, silently
// dropping C14X2 binds). GX_CTF_YUVA8 stays listed for THP EFB-copy frames, which never go through convert_texture.
static bool IsAuroraLoadableTexFormat(uint32_t fmt) noexcept {
    switch (fmt) {
    case GX_TF_I4:
    case GX_TF_I8:
    case GX_TF_IA4:
    case GX_TF_IA8:
    case GX_TF_C4:
    case GX_TF_C8:
    case GX_TF_C14X2:
    case GX_TF_RGB565:
    case GX_TF_RGB5A3:
    case GX_TF_Z8:
    case GX_TF_Z16:
    case GX_TF_Z24X8:
    case GX_TF_RGBA8:
    case GX_CTF_YUVA8:
    case GX_TF_CMPR:
        return true;
    default:
        return false;
    }
}

static GXTexWrapMode SanitizeWrapMode(uint32_t rawWrap) noexcept {
    switch (rawWrap) {
    case GX_CLAMP:
    case GX_REPEAT:
    case GX_MIRROR:
        return static_cast<GXTexWrapMode>(rawWrap);
    default:
        return GX_CLAMP;
    }
}

static GXTexFilter SanitizeMinFilter(uint32_t rawFilter) noexcept {
    switch (rawFilter) {
    case GX_NEAR:
    case GX_LINEAR:
    case GX_NEAR_MIP_NEAR:
    case GX_LIN_MIP_NEAR:
    case GX_NEAR_MIP_LIN:
    case GX_LIN_MIP_LIN:
        return static_cast<GXTexFilter>(rawFilter);
    // Some decoded hardware encodings surface as 6/7; map to the strongest valid mip filter.
    case 6:
    case 7:
        return GX_LIN_MIP_LIN;
    default:
        return GX_LINEAR;
    }
}

static GXTexFilter SanitizeMagFilter(uint32_t rawFilter) noexcept {
    return (rawFilter == GX_NEAR) ? GX_NEAR : GX_LINEAR;
}

static GXAnisotropy SanitizeAniso(uint32_t rawAniso) noexcept {
    switch (rawAniso) {
    case GX_ANISO_1:
    case GX_ANISO_2:
    case GX_ANISO_4:
        return static_cast<GXAnisotropy>(rawAniso);
    default:
        return GX_ANISO_4;
    }
}

static bool IsReasonableTextureDimensions(uint16_t width, uint16_t height) noexcept {
    // Hardware textures are small; reject absurd dimensions that can destabilize host texture creation.
    constexpr uint16_t kMaxDimension = 4096;
    return width > 0 && height > 0 && width <= kMaxDimension && height <= kMaxDimension;
}

static float SanitizeLodValue(float lod, float fallback) noexcept {
    if (!std::isfinite(lod)) {
        return fallback;
    }
    return std::max(0.0f, lod);
}

static float SanitizeLodBias(float lodBias) noexcept {
    if (!std::isfinite(lodBias)) {
        return 0.0f;
    }
    return std::clamp(lodBias, -4.0f, 3.99f);
}

// Republish the host texobj's LOD/filter block from `meta`, with the three
// fields each caller may be overriding passed in explicitly.
static void ApplyHostTexObjLod(GXTexObj* obj, const TexObjMeta& meta, GXTexFilter minFilter,
                               GXTexFilter magFilter, float lodBias, GXAnisotropy maxAniso) {
    GXInitTexObjLOD(obj, minFilter, magFilter, meta.minLod, meta.maxLod, lodBias,
                    meta.biasClamp ? GX_TRUE : GX_FALSE, meta.edgeLod ? GX_TRUE : GX_FALSE,
                    maxAniso);
}

static uint32_t ComputeMaxMipLevel(uint16_t width, uint16_t height) noexcept {
    uint32_t w = std::max<uint32_t>(1u, width);
    uint32_t h = std::max<uint32_t>(1u, height);
    uint32_t levels = 0;
    while (w > 1u || h > 1u) {
        w = std::max<uint32_t>(1u, w >> 1u);
        h = std::max<uint32_t>(1u, h >> 1u);
        ++levels;
    }
    return levels;
}

extern "C" void GX__InitTexObj_801707f8(uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m) {
    // DIAGNOSTIC (temporary): NSMBW_LOG_WRAP_MODE - confirms exactly what wrapS/wrapT the guest
    // passes into GXInitTexObj for the specific GXTexObj (oa=0x8043FC48) shared by P_back_00 and
    // P_mask_00, the two panes identified via NSMBW_LOG_PANE_IDENTITY as sharing one texture
    // object. The real .brlyt asset specifies wrapS=wrapT=0 (GX_CLAMP) for both; this checks
    // whether that value survives correctly, or whether wrap ends up wrong (e.g. GX_REPEAT),
    // which - since P_back_00 stretches a small text texture across a much larger 850x456 quad -
    // would tile/repeat it into exactly the banded pattern seen on screen. Remove once resolved.
    if (AURORA_ENV("NSMBW_LOG_WRAP_MODE") != nullptr) {
        static int logged = 0;
        if (logged < 40) {
            ++logged;
            RT_LOGF(RT_TAG_GX, "NSMBW_WRAP_MODE GXInitTexObj oa=0x%08X w=%u h=%u fmt=%u ws=%u wt=%u\n", oa, w, h,
                    f, ws, wt);
        }
    }
    if (w == 0 || h == 0) {
        auto* cpu = TryGetCpuContext();
        RT_LOGF(RT_TAG_GX,
                "GXInitTexObj zero size oa=0x%08X da=0x%08X w=%u h=%u f=%u ws=%u wt=%u m=%u pc=0x%08X lr=0x%08X\n",
                oa, da, w, h, f, ws, wt, m, cpu ? cpu->pc : 0u, cpu ? cpu->lr : 0u);
    }
    const uint32_t canonicalDataAddr = CanonicalizeGxMainRamAddress(da);
    std::lock_guard<std::mutex> guard(g_texObjMutex); GXTexObj* obj = CreateHostTexObj(oa); TexObjMeta& meta = GetTexObjMeta(oa);
    meta.dataAddr=canonicalDataAddr; meta.width=(u16)w; meta.height=(u16)h; meta.format=f; meta.wrapS=ws; meta.wrapT=wt; meta.mipmap=(m!=0); meta.userData=0; meta.needsUpload=true;
    GXInitTexObj(obj, GuestToHostPtr(da), (u16)w, (u16)h, (GXTexFmt)f, (GXTexWrapMode)ws, (GXTexWrapMode)wt, (GXBool)m); MarkHostTexObjConstructed(oa);
    // Also write to guest memory so reads work
    WriteGuestTexObj(oa, canonicalDataAddr, (u16)w, (u16)h, f, ws, wt, m != 0, false, 0);
}
PPC_NATIVE_OVERRIDE_VOID(801707f8, GX__InitTexObj_801707f8, (uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m), (oa, da, w, h, f, ws, wt, m));

extern "C" void GX__InitTexObj_switch_80170938(uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m) { GX__InitTexObj_801707f8(oa, da, w, h, f, ws, wt, m); }
PPC_NATIVE_OVERRIDE_VOID(80170938, GX__InitTexObj_switch_80170938, (uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m), (oa, da, w, h, f, ws, wt, m));

extern "C" void GX__InitTexObj_caseD_0_8017093c(uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m) { GX__InitTexObj_801707f8(oa, da, w, h, f, ws, wt, m); }
PPC_NATIVE_OVERRIDE_VOID(8017093c, GX__InitTexObj_caseD_0_8017093c, (uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m), (oa, da, w, h, f, ws, wt, m));

extern "C" void GX__InitTexObj_caseD_1_80170950(uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m) { GX__InitTexObj_801707f8(oa, da, w, h, f, ws, wt, m); }
PPC_NATIVE_OVERRIDE_VOID(80170950, GX__InitTexObj_caseD_1_80170950, (uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m), (oa, da, w, h, f, ws, wt, m));

extern "C" void GX__InitTexObj_caseD_3_80170964(uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m) { GX__InitTexObj_801707f8(oa, da, w, h, f, ws, wt, m); }
PPC_NATIVE_OVERRIDE_VOID(80170964, GX__InitTexObj_caseD_3_80170964, (uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m), (oa, da, w, h, f, ws, wt, m));

extern "C" void GX__InitTexObj_caseD_6_80170978(uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m) { GX__InitTexObj_801707f8(oa, da, w, h, f, ws, wt, m); }
PPC_NATIVE_OVERRIDE_VOID(80170978, GX__InitTexObj_caseD_6_80170978, (uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m), (oa, da, w, h, f, ws, wt, m));

extern "C" void GX__InitTexObj_caseD_e_8017098c(uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m) { GX__InitTexObj_801707f8(oa, da, w, h, f, ws, wt, m); }
PPC_NATIVE_OVERRIDE_VOID(8017098c, GX__InitTexObj_caseD_e_8017098c, (uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m), (oa, da, w, h, f, ws, wt, m));

extern "C" void GX__InitTexObj_caseD_7_801709a0(uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m) { GX__InitTexObj_801707f8(oa, da, w, h, f, ws, wt, m); }
PPC_NATIVE_OVERRIDE_VOID(801709a0, GX__InitTexObj_caseD_7_801709a0, (uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m), (oa, da, w, h, f, ws, wt, m));

// ============================================================================
// Texture Object Configuration
// ============================================================================

extern "C" void GX__InitTexObjCI_80170a04(uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m, uint32_t tl) {
    if (w == 0 || h == 0) {
        auto* cpu = TryGetCpuContext();
        RT_LOGF(RT_TAG_GX,
                "GXInitTexObjCI zero size oa=0x%08X da=0x%08X w=%u h=%u f=%u ws=%u wt=%u m=%u tl=%u pc=0x%08X lr=0x%08X\n",
                oa, da, w, h, f, ws, wt, m, tl, cpu ? cpu->pc : 0u, cpu ? cpu->lr : 0u);
    }
    const uint32_t canonicalDataAddr = CanonicalizeGxMainRamAddress(da);
    std::lock_guard<std::mutex> guard(g_texObjMutex); GXTexObj* obj = CreateHostTexObj(oa); TexObjMeta& meta = GetTexObjMeta(oa);
    meta.dataAddr=canonicalDataAddr; meta.width=(u16)w; meta.height=(u16)h; meta.format=f; meta.wrapS=ws; meta.wrapT=wt; meta.mipmap=(m!=0); meta.tlut=tl; meta.userData=0; meta.needsUpload=true;
    GXInitTexObjCI(obj, GuestToHostPtr(da), (u16)w, (u16)h, (GXCITexFmt)f, (GXTexWrapMode)ws, (GXTexWrapMode)wt, (GXBool)m, tl); MarkHostTexObjConstructed(oa);
    // Also write to guest memory so reads work
    WriteGuestTexObj(oa, canonicalDataAddr, (u16)w, (u16)h, f, ws, wt, m != 0, true, tl);
    // GXInitTexObjCI clears bit1 in the flags byte; keep guest memory consistent.
    try {
        uint8_t flags = Memory::Read8(oa + 0x1F);
        Memory::Write8(oa + 0x1F, static_cast<uint8_t>(flags & 0xFD));
    } catch (...) {
    }
}
PPC_NATIVE_OVERRIDE_VOID(80170a04, GX__InitTexObjCI_80170a04, (uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m, uint32_t tl), (oa, da, w, h, f, ws, wt, m, tl));

extern "C" void GX__InitTexObjLOD_80170a4c(uint32_t oa, uint32_t mif, uint32_t maf, float mil, float mal, float lb, uint32_t bc, uint32_t el, uint32_t ma) {
    std::lock_guard<std::mutex> guard(g_texObjMutex); GXTexObj* obj = GetHostTexObj(oa); TexObjMeta& meta = GetTexObjMeta(oa);
    float fmal = std::isfinite(mal) && mal >= 0.f ? mal : 0.f, fmil = std::isfinite(mil) && mil >= 0.f ? std::min(mil, fmal) : 0.f;
    meta.minFilter=mif; meta.magFilter=maf; meta.minLod=fmil; meta.maxLod=fmal; meta.lodBias=lb; meta.biasClamp=(bc!=0); meta.edgeLod=(el!=0); meta.maxAniso=ma;
    GXInitTexObjLOD(obj, (GXTexFilter)mif, (GXTexFilter)maf, fmil, fmal, lb, (GXBool)bc, (GXBool)el, (GXAnisotropy)ma);
    // Also write LOD info to guest memory
    WriteGuestTexObjLOD(oa, mif, maf, fmil, fmal, lb, bc != 0, el != 0, ma);
}
PPC_NATIVE_OVERRIDE_VOID(80170a4c, GX__InitTexObjLOD_80170a4c, (uint32_t oa, uint32_t mif, uint32_t maf, float mil, float mal, float lb, uint32_t bc, uint32_t el, uint32_t ma), (oa, mif, maf, mil, mal, lb, bc, el, ma));

extern "C" void GX__InitTexObjWrapMode_80170b50(uint32_t oa, uint32_t ws, uint32_t wt) {
    std::lock_guard<std::mutex> guard(g_texObjMutex);
    GXTexObj* obj = GetHostTexObj(oa);
    TexObjMeta& meta = GetTexObjMeta(oa);
    meta.wrapS = ws;
    meta.wrapT = wt;
    try {
        uint32_t word0 = Memory::Read32(oa + 0x00);
        word0 = (word0 & ~0xFu) | (ws & 0x3u) | ((wt & 0x3u) << 2);
        Memory::Write32(oa + 0x00, word0);
    } catch (...) {
    }
    GXInitTexObjWrapMode(obj, (GXTexWrapMode)ws, (GXTexWrapMode)wt);
}
PPC_NATIVE_OVERRIDE_VOID(80170b50, GX__InitTexObjWrapMode_80170b50, (uint32_t oa, uint32_t ws, uint32_t wt), (oa, ws, wt));

extern "C" void GX__InitTexObjTlut_80170b64(uint32_t oa, uint32_t tl) {
    std::lock_guard<std::mutex> guard(g_texObjMutex);
    GXTexObj* obj = GetHostTexObj(oa);
    TexObjMeta& meta = GetTexObjMeta(oa);
    meta.tlut = tl;
    GXInitTexObjTlut(obj, tl);
    // Keep guest GXTexObj coherent for later GXLoadTexObj from memory.
    try {
        Memory::Write32(oa + 0x18, tl);
    } catch (...) {
    }
}
PPC_NATIVE_OVERRIDE_VOID(80170b64, GX__InitTexObjTlut_80170b64, (uint32_t oa, uint32_t tl), (oa, tl));

// GXInitTexObjFilter - sets mag/min filter modes
extern "C" void GX__InitTexObjFilter_80170b6c(uint32_t oa, uint32_t minFilter, uint32_t magFilter) {
    std::lock_guard<std::mutex> guard(g_texObjMutex);
    TexObjMeta& meta = GetTexObjMeta(oa);
    meta.minFilter = minFilter;
    meta.magFilter = magFilter;
    
    try {
        uint32_t word0 = Memory::Read32(oa + 0x00);
        // bit 4: magFilter - set if magFilter == GX_LINEAR (1), clear otherwise
        // Original code used cntlzw to check: cntlzw(magFilter-1) >> 1 & 0x10
        // This sets bit 4 when magFilter-1 is 0 (i.e., magFilter == 1)
        uint32_t magBit = (magFilter == 1) ? 0x10 : 0;
        word0 = (word0 & 0xFFFFFFEF) | magBit;
        // bits [7:5]: minFilter from LUT
        uint32_t minBits = (kGxToHwMinFilter[minFilter & 7] & 7) << 5;
        word0 = (word0 & 0xFFFFFF0F) | magBit | minBits;
        Memory::Write32(oa + 0x00, word0);
    } catch (...) {}

    // Update host texture object if it exists
    if (GXTexObj* obj = TryGetHostTexObj(oa)) {
        ApplyHostTexObjLod(obj, meta, (GXTexFilter)minFilter, (GXTexFilter)magFilter, meta.lodBias,
                           (GXAnisotropy)meta.maxAniso);
    }
}
PPC_NATIVE_OVERRIDE_VOID(80170b6c, GX__InitTexObjFilter_80170b6c, (uint32_t oa, uint32_t minFilter, uint32_t magFilter), (oa, minFilter, magFilter));

// GXInitTexObjLODBias - sets LOD bias value
extern "C" void GX__InitTexObjLODBias_80170b94(uint32_t oa, float bias) {
    std::lock_guard<std::mutex> guard(g_texObjMutex);
    TexObjMeta& meta = GetTexObjMeta(oa);
    
    // Clamp bias to [-4.0, 3.99...] range as per SDK
    constexpr float kMinBias = -4.0f;
    constexpr float kMaxBias = 3.99f; // Actually ~3.990000009536743
    float clampedBias = bias;
    if (clampedBias < kMinBias) clampedBias = kMinBias;
    if (clampedBias >= 4.0f) clampedBias = kMaxBias;
    
    meta.lodBias = clampedBias;
    
    // Update guest GXTexObj word0 with bias (scale by 32, store in bits [16:9])
    try {
        uint32_t word0 = Memory::Read32(oa + 0x00);
        int8_t biasScaled = static_cast<int8_t>(clampedBias * 32.0f);
        word0 = (word0 & 0xFFFE01FF) | ((static_cast<uint32_t>(static_cast<uint8_t>(biasScaled)) & 0xFF) << 9);
        Memory::Write32(oa + 0x00, word0);
    } catch (...) {}
    
    // Update host texture object if it exists
    if (GXTexObj* obj = TryGetHostTexObj(oa)) {
        ApplyHostTexObjLod(obj, meta, (GXTexFilter)meta.minFilter, (GXTexFilter)meta.magFilter,
                           clampedBias, (GXAnisotropy)meta.maxAniso);
    }
}
PPC_NATIVE_OVERRIDE_VOID(80170b94, GX__InitTexObjLODBias_80170b94, (uint32_t oa, float bias), (oa, bias));

extern "C" void GX__InitTexObjUserData_80170be8(uint32_t oa, uint32_t userData) {
    std::lock_guard<std::mutex> guard(g_texObjMutex);
    TexObjMeta& meta = GetTexObjMeta(oa);
    meta.userData = userData;

    // The RVL SDK stores this opaque guest pointer verbatim at GXTexObj + 0x10.
    WriteGuest32(oa + 0x10, userData, "GXInitTexObjUserData");

    // Aurora keeps an expanded host-side object, so mirror the pointer when
    // that representation has already been constructed.
    if (GXTexObj* obj = TryGetHostTexObj(oa)) {
        GXInitTexObjUserData(obj, GuestToHostPtr(userData));
    }
}
PPC_NATIVE_OVERRIDE_VOID(80170be8, GX__InitTexObjUserData_80170be8,
              (uint32_t oa, uint32_t userData), (oa, userData));

// ============================================================================
// Texture Loading
// ============================================================================

// A refused GXLoadTexObj must not leave the previous material's texture bound in that TEV
// slot (rejection paths used to just return, so draws sampled stale, unrelated art). Bind
// aurora's empty texture instead: null image data resolves to a 1x1 transparent placeholder,
// which is also the correct result for the common case of an intentionally-transparent hide texture.
static void BindUnloadableTexturePlaceholder(uint32_t tid) {
    if (tid >= g_boundTexMaps.size()) {
        return;
    }
    // Callers hold g_texObjMutex, so this initialization is serialized.
    static GXTexObj s_placeholder{};
    static bool s_placeholderReady = false;
    if (!s_placeholderReady) {
        GXInitTexObj(&s_placeholder, nullptr, 1, 1, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
        s_placeholderReady = true;
    }
    // A zeroed binding record can never compare equal to a real load (those
    // always carry a non-zero objAddr), so the CanSkipHostLoad fast path cannot
    // mistake a later genuine bind for a no-op.
    g_boundTexMaps[tid] = BoundTexInfo{};
    GXLoadTexObj(&s_placeholder, (GXTexMapID)tid);
}

// DIAGNOSTIC (temporary): NSMBW_LOG_PANE_IDENTITY - ground-truth identification of which real
// nw4r::lyt Pane/Material object is behind a given GXLoadTexObj call, instead of inferring it from
// texture dimensions (which turned out ambiguous - multiple panes in WiiStrap.brlyt legitimately
// share texture sizes/content by design). nw4r::lyt::Pane stores its resource name as a
// null-terminated ASCII string at offset +0xBC (confirmed from NSMBW-Decomp's lyt_pane.h:
// `char mName[NW4R_LYT_RES_NAME_LEN + 1]; // at 0xBC`). This scans every GPR live at the moment
// GXLoadTexObj is called for one that looks like a Pane* (its +0xBC bytes decode as a short,
// printable ASCII string), which - since GXLoadTexObj is reached via Picture::DrawSelf ->
// Material::LoadTexture with the Pane/Picture `this` typically still resident in a callee-saved
// register - identifies the actual pane by name with certainty, rather than by guessing from
// texture size. Remove once resolved.
// Shared by NsmbwLogPaneIdentityForTexLoad (the general 60-call survey) and the narrower
// NSMBW_TARGET_WRAP_PANE_ID trigger below, which needs its own budget so a screen that only
// shows up after the general survey's 60 calls are already spent (e.g. anything past the
// WiiStrap screens) can still be identified.
static void NsmbwScanForPaneName(const char* logPrefix, int callIndex, uint32_t oa, uint32_t tid) {
    CpuContext* ctx = TryGetCpuContext();
    RT_LOGF(RT_TAG_GX, "%s call#%d oa=0x%08X tid=%u lr=0x%08X\n", logPrefix, callIndex, oa, tid,
            ctx ? ctx->lr : 0u);
    if (ctx == nullptr) return;
    auto tryCandidate = [logPrefix](const char* label, uint32_t candidate) {
        if (candidate < 0x80000000u || candidate > 0x817FFFFFu) return; // must look like a MEM1 pointer
        char name[17] = {};
        bool ok = true;
        int len = 0;
        try {
            for (int i = 0; i < 16; ++i) {
                const uint8_t b = Memory::Read8(candidate + 0xBC + i);
                if (b == 0) { name[i] = 0; break; }
                if (b < 0x21 || b > 0x7E) { ok = false; break; } // require printable, non-space (real names have no spaces)
                name[i] = static_cast<char>(b);
                ++len;
            }
        } catch (...) {
            return;
        }
        // Real pane names in this layout are all >= 5 chars (e.g. "P_back_00") - filter out
        // single/double-char coincidental matches from unrelated data.
        if (ok && len >= 5) {
            RT_LOGF(RT_TAG_GX, "%s   %s=0x%08X +0xBC(name-if-Pane)=\"%s\"\n", logPrefix, label, candidate, name);
        }
    };
    for (int r = 3; r <= 31; ++r) {
        char label[8];
        std::snprintf(label, sizeof(label), "r%d", r);
        tryCandidate(label, ctx->gpr[r]);
    }
    // Also scan a window of guest stack memory below r1 (the stack pointer) for spilled
    // callee-saved registers that might hold the Pane/Picture `this` a few frames up the chain.
    const uint32_t sp = ctx->gpr[1];
    for (uint32_t off = 0; off <= 0x200; off += 4) {
        uint32_t word = 0;
        try {
            word = Memory::Read32(sp + off);
        } catch (...) {
            break;
        }
        char label[16];
        std::snprintf(label, sizeof(label), "stack+0x%03X", off);
        tryCandidate(label, word);
    }
}

// Set by nsmbw_create_next_scene_diag.cpp on every successful scene transition; lets this
// survey gate on "which fProf::PROFILE_NAME_e scene is active" instead of a guessed call count.
extern "C" uint32_t g_nsmbwCurrentSceneProfile;

static void NsmbwLogPaneIdentityForTexLoad(uint32_t oa, uint32_t tid) {
    static const bool s_enabled = AURORA_ENV("NSMBW_LOG_PANE_IDENTITY") != nullptr;
    if (!s_enabled) return;
    static uint32_t s_totalCalls = 0;
    const uint32_t callIndex = s_totalCalls++;
    // NSMBW_LOG_PANE_IDENTITY_SCENE (temporary): the original 60-call budget below only
    // covers the very first GXLoadTexObj calls in the run (the WiiStrap screens, scene
    // profile 0). Screens that show up thousands of calls later - e.g. the STAGE-profile
    // (0x5) demo/attract screen sitting between CRSIN and GAME_SETUP - never get surveyed
    // because the budget is long spent by the time they're reached. This env var (a decimal
    // fProf::PROFILE_NAME_e value, e.g. "5" for STAGE) re-points the 60-call survey window at
    // whichever scene is currently active, using g_nsmbwCurrentSceneProfile instead of a
    // guessed call-count offset. Remove once resolved.
    static const long s_sceneFilter = [] {
        const char* v = AURORA_ENV("NSMBW_LOG_PANE_IDENTITY_SCENE");
        return v ? static_cast<long>(std::strtoul(v, nullptr, 10)) : -1L;
    }();
    if (s_sceneFilter >= 0 && g_nsmbwCurrentSceneProfile != static_cast<uint32_t>(s_sceneFilter)) return;
    static int logged = 0;
    if (logged >= 60) return;
    ++logged;
    NsmbwScanForPaneName("NSMBW_PANE_ID", static_cast<int>(callIndex), oa, tid);

    // NSMBW_LOG_COLOR_TEV (temporary): "every screen shows flat color instead of real texture
    // content" investigation. The P_stripe_00 probe further down this file only ever checked the
    // ALPHA combiner (needed for that fade-opacity question) - whether a pane's actual COLOR
    // comes from its bound texture (GX_CC_TEXC=8) or gets replaced by a constant/vertex color
    // (GX_CC_RASC=0, GX_CC_KONST=6, GX_CC_ONE=15, GX_CC_ZERO=16, ...) was never checked. This
    // dumps every active stage whose texMap matches this GXLoadTexObj call's tid, right after the
    // pane-name scan above so each line can be matched to a specific pane by call index. Shares
    // that scan's budget/scene-gating so both surveys point at the same window of the run.
    if (AURORA_ENV("NSMBW_LOG_COLOR_TEV") != nullptr) {
        bool any = false;
        for (uint32_t st = 0; st < g_nsmbwLiveNumTevStages && st < 16; ++st) {
            if (g_nsmbwLiveTevTexMap[st] != tid) continue;
            any = true;
            RT_LOGF(RT_TAG_GX,
                "NSMBW_COLOR_TEV call#%d oa=0x%08X tid=%u stage=%u colorIn a=%u b=%u c=%u d=%u "
                "(colorArg per dolphin/gx/GXEnum.h GXTevColorArg: 0=CPREV,1=APREV,2=C0,3=A0,4=C1,"
                "5=A1,6=C2,7=A2,8=TEXC,9=TEXA,10=RASC,11=RASA,12=ONE,13=HALF,14=KONST,15=ZERO)\n",
                static_cast<int>(callIndex), oa, tid, st,
                g_nsmbwLiveTevColorA[st], g_nsmbwLiveTevColorB[st],
                g_nsmbwLiveTevColorC[st], g_nsmbwLiveTevColorD[st]);
            // WipeCircle follow-up (2026-09-19): color combiner alone can't explain the on-screen
            // blue - C0/C1 both read (0,0,0), which interpolates to black regardless of the
            // texture-driven blend factor. Dumping this stage's ALPHA combiner and the active
            // blend-src/dst/op alongside it, since either one could be why the visible pixel isn't
            // simply black: e.g. a low/zero alpha here would let whatever's UNDER this draw show
            // through instead (a separate earlier draw, or the raw framebuffer clear color).
            RT_LOGF(RT_TAG_GX,
                "NSMBW_ALPHA_TEV call#%d stage=%u alphaIn a=%u b=%u c=%u d=%u "
                "(alphaArg: 0=APREV,1=A0,2=A1,3=A2,4=TEXA,5=RASA,6=KONST,7=ZERO) "
                "regAlpha APREV=%.3f A0=%.3f A1=%.3f A2=%.3f | "
                "blend type=%u(0=NONE,1=BLEND,2=LOGIC,3=SUBTRACT) src=%u dst=%u op=%u\n",
                static_cast<int>(callIndex), st,
                g_nsmbwLiveTevAlphaA[st], g_nsmbwLiveTevAlphaB[st],
                g_nsmbwLiveTevAlphaC[st], g_nsmbwLiveTevAlphaD[st],
                g_nsmbwLiveTevRegAlpha[0], g_nsmbwLiveTevRegAlpha[1],
                g_nsmbwLiveTevRegAlpha[2], g_nsmbwLiveTevRegAlpha[3],
                g_nsmbwLastBlendDiag.type, g_nsmbwLastBlendDiag.src, g_nsmbwLastBlendDiag.dst,
                g_nsmbwLastBlendDiag.op);
        }
        // idx into g_nsmbwLiveTevRegColor* follows GXTevRegID (GXEnum.h): 0=TEVPREV(CPREV,
        // colorArg value 0), 1=TEVREG0(C0, colorArg value 2), 2=TEVREG1(C1, colorArg value 4),
        // 3=TEVREG2(C2, colorArg value 6).
        RT_LOGF(RT_TAG_GX,
            "NSMBW_COLOR_TEV_REGS call#%d CPREV=(%.3f,%.3f,%.3f) C0=(%.3f,%.3f,%.3f) "
            "C1=(%.3f,%.3f,%.3f) C2=(%.3f,%.3f,%.3f)\n",
            static_cast<int>(callIndex),
            g_nsmbwLiveTevRegColorR[0], g_nsmbwLiveTevRegColorG[0], g_nsmbwLiveTevRegColorB[0],
            g_nsmbwLiveTevRegColorR[1], g_nsmbwLiveTevRegColorG[1], g_nsmbwLiveTevRegColorB[1],
            g_nsmbwLiveTevRegColorR[2], g_nsmbwLiveTevRegColorG[2], g_nsmbwLiveTevRegColorB[2],
            g_nsmbwLiveTevRegColorR[3], g_nsmbwLiveTevRegColorG[3], g_nsmbwLiveTevRegColorB[3]);
        if (!any) {
            RT_LOGF(RT_TAG_GX, "NSMBW_COLOR_TEV call#%d oa=0x%08X tid=%u: no active stage bound to this tid\n",
                    static_cast<int>(callIndex), oa, tid);
        }
    }
}

extern "C" void GX__LoadTexObj_80170f2c(uint32_t oa, uint32_t tid) {
    NsmbwLogPaneIdentityForTexLoad(oa, tid);
    // DIAGNOSTIC (NSMBW_LOG_LIQUID): water/lava/poison surfaces are drawn by raw GX code in main.dol
    // (AC_BG_WATER's m3d::proc_c draw slots -> 0x8000D000 / 0x8000AFA0, helpers up to 0x8000D170; 0x8000A3D0 just below is the tile animator).
    // When that code binds a texture, log it and put a marker into the GX stream; aurora's command
    // processor arms its per-draw state log (NSMBW_TEXGEN lines) for the next draws when it reaches
    // the marker, so exactly the liquid's draws are dumped, in stream order.
    {
        static const bool logLiquid = AURORA_ENV("NSMBW_LOG_LIQUID") != nullptr;
        static std::map<uint32_t, int> armedPerSite; // 6 per call site, so every routine is captured
        if (logLiquid) {
            const CpuContext* c = TryGetCpuContext();
            const uint32_t lr = c ? static_cast<uint32_t>(c->lr) : 0u;
            if (lr >= 0x8000AFA0u && lr < 0x8000D170u && armedPerSite[lr] < 6) {
                ++armedPerSite[lr];
                RT_LOGF(RT_TAG_GX, "NSMBW_LIQUID GXLoadTexObj from LR=0x%08X oa=0x%08X tid=%u\n", lr, oa, tid);
                GXInsertDebugMarker("nsmbw-arm-drawlog");
            }
        }
    }
    static uint32_t s_invalidMetaLogCount = 0;
    static uint32_t s_invalidTidLogCount = 0;
    static uint32_t s_invalidDimLogCount = 0;
    static uint32_t s_hostExceptionLogCount = 0;
    static uint32_t s_metaLookupLogCount = 0;
    static uint32_t s_unknownFormatLogCount = 0;
    static uint32_t s_invalidDataLogCount = 0;
    std::lock_guard<std::mutex> guard(g_texObjMutex);

    if (tid >= g_boundTexMaps.size()) {
        if (s_invalidTidLogCount < 128) {
            RT_LOGF(RT_TAG_GX, "GXLoadTexObj: invalid tex map id %u (oa=0x%08X)\n", tid, oa);
            ++s_invalidTidLogCount;
        }
        return;
    }
    
    // Check if we have metadata for this texture, or try to extract it from guest memory
    TexObjMeta meta;
    if (!TryGetOrExtractTexObjMeta(oa, meta)) {
        if (s_metaLookupLogCount++ < 64) {
            RT_LOGF(RT_TAG_GX,
                    "GXLoadTexObj rejected reason=no-metadata oa=0x%08X tid=%u\n", oa, tid);
        }
        BindUnloadableTexturePlaceholder(tid);
        return;
    }

    // DIAGNOSTIC (temporary): NSMBW_LOG_TEXFMT[=<scene profile>] lists what the guest binds -
    // format / size / tlut / data address per GXLoadTexObj - to check whether the broken 2D
    // layout screens use palette formats (CI4/CI8 = 8/9) or something else. Remove once resolved.
    {
        static const char* s_texFmtEnv = AURORA_ENV("NSMBW_LOG_TEXFMT");
        static const long s_texFmtScene = s_texFmtEnv && *s_texFmtEnv ? std::strtol(s_texFmtEnv, nullptr, 10) : -1L;
        // Each distinct data address once (a level front-loads hundreds of loads of the same few
        // textures, so a plain count cap never reached the tilesets).
        static std::unordered_set<uint32_t> s_texFmtSeen;
        if (s_texFmtEnv && (s_texFmtScene < 0 || g_nsmbwCurrentSceneProfile == static_cast<uint32_t>(s_texFmtScene)) &&
            s_texFmtSeen.insert(meta.dataAddr).second) {
            RT_LOGF(RT_TAG_GX, "NSMBW_TEXFMT scene=%u oa=0x%08X tid=%u fmt=%u %ux%u tlut=%u data=0x%08X mip=%d wrap=%u/%u minF=%u magF=%u lod=%.2f..%.2f bias=%.2f\n",
                    g_nsmbwCurrentSceneProfile, oa, tid, meta.format, meta.width, meta.height, meta.tlut,
                    meta.dataAddr, meta.mipmap ? 1 : 0, meta.wrapS, meta.wrapT, meta.minFilter, meta.magFilter, meta.minLod, meta.maxLod, meta.lodBias);
        }
    }

    if (meta.dataAddr == 0 || meta.width == 0 || meta.height == 0) {
        if (s_invalidMetaLogCount++ < 64) {
            RT_LOGF(RT_TAG_GX,
                    "GXLoadTexObj rejected reason=invalid-meta oa=0x%08X tid=%u data=0x%08X %ux%u\n",
                    oa, tid, meta.dataAddr, meta.width, meta.height);
        }
        BindUnloadableTexturePlaceholder(tid);
        return;
    }
    if (!IsReasonableTextureDimensions(meta.width, meta.height)) {
        if (s_invalidDimLogCount++ < 128) {
            RT_LOGF(RT_TAG_GX,
                    "GXLoadTexObj rejected reason=bad-dimensions oa=0x%08X tid=%u %ux%u fmt=0x%X\n",
                    oa, tid, meta.width, meta.height, meta.format);
        }
        BindUnloadableTexturePlaceholder(tid);
        return;
    }
    // One gate, two reasons: every aurora-loadable format is also a known one,
    // so a separate IsKnownTexFormat rejection could never fire on anything this
    // check would let through. The reason string still distinguishes "GX has no
    // such format" from "aurora cannot decode it", which is what triage needs.
    if (!IsAuroraLoadableTexFormat(meta.format)) {
        const bool known = IsKnownTexFormat(meta.format);
        uint32_t& logCount = known ? g_unsupportedFormatLogCount : s_unknownFormatLogCount;
        if (logCount++ < 128) {
            RT_LOGF(RT_TAG_GX,
                    "GXLoadTexObj rejected reason=%s oa=0x%08X tid=%u fmt=0x%X %ux%u\n",
                    known ? "unsupported-format" : "unknown-format", oa, tid, meta.format,
                    meta.width, meta.height);
        }
        BindUnloadableTexturePlaceholder(tid);
        return;
    }
    const float maxMipLevel = static_cast<float>(ComputeMaxMipLevel(meta.width, meta.height));
    const float minLodSafe = SanitizeLodValue(meta.minLod, 0.0f);
    float maxLodSafe = SanitizeLodValue(meta.maxLod, minLodSafe);
    maxLodSafe = std::min(maxLodSafe, maxMipLevel);
    const float clampedMinLodSafe = std::min(minLodSafe, maxLodSafe);
    const float lodBiasSafe = SanitizeLodBias(meta.lodBias);
    const uint8_t maxLod = (maxLodSafe > 0.0f) ? ((maxLodSafe > 255.0f) ? 255u : static_cast<uint8_t>(maxLodSafe)) : 0u;
    const uint32_t size = GXGetTexBufferSize(meta.width, meta.height, meta.format, (GXBool)meta.mipmap, maxLod);
    if (size == 0 || !Memory::Contains(meta.dataAddr, size)) {
        if (s_invalidDataLogCount++ < 64) {
            auto* cpu = TryGetCpuContext();
            RT_LOGF(RT_TAG_GX,
                    "GXLoadTexObj rejected reason=data-out-of-range "
                    "oa=0x%08X tid=%u data=0x%08X size=0x%X %ux%u fmt=0x%X mip=%u maxLod=%u pc=0x%08X lr=0x%08X\n",
                    oa, tid, meta.dataAddr, size, meta.width, meta.height, meta.format,
                    meta.mipmap ? 1u : 0u, static_cast<uint32_t>(maxLod),
                    cpu ? cpu->pc : 0u, cpu ? cpu->lr : 0u);
        }
        BindUnloadableTexturePlaceholder(tid);
        return;
    }
    // WipeCircle follow-up (2026-09-19): TEV/blend state for W_circle_00 (oa=0x8043FB38, confirmed
    // stable across runs via NSMBW_LOG_PANE_IDENTITY) is a legitimate alpha-masked black wipe
    // (alphaIn passes TEXA straight through; blend is SRCALPHA/INVSRCALPHA) - so a uniformly-zero
    // (or uniformly-one, but the screen isn't black) alpha channel in the bound texture itself
    // would explain "renders as one flat color" better than a TEV misconfiguration does. I4/I8/
    // IA4/IA8 are all tile-blocked (not simple raster order), so picking a "center texel" byte
    // offset by hand would need the tiling geometry worked out first and could easily land on the
    // wrong texel. Scanning the WHOLE decoded buffer for its min/max byte value sidesteps that
    // entirely: min==max==0 across the full buffer means "genuinely all zero, no gradient exists
    // anywhere in this data," while any spread proves real gradient data is present (wherever it
    // physically sits in the tiling). Remove once resolved.
    if (oa == 0x8043FB38u && AURORA_ENV("NSMBW_LOG_WIPECIRCLE_TEXDATA") != nullptr) {
        static int wipeTexLogged = 0;
        if (wipeTexLogged < 8) {
            ++wipeTexLogged;
            const uint8_t* src = static_cast<const uint8_t*>(GuestToHostPtr(meta.dataAddr, size));
            uint8_t minB = 0xFF, maxB = 0x00;
            uint32_t zeroCount = 0, ffCount = 0;
            if (src != nullptr) {
                for (uint32_t i = 0; i < size; ++i) {
                    const uint8_t b = src[i];
                    minB = std::min(minB, b);
                    maxB = std::max(maxB, b);
                    if (b == 0x00) ++zeroCount;
                    if (b == 0xFF) ++ffCount;
                }
            }
            RT_LOGF(RT_TAG_GX,
                    "NSMBW_WIPECIRCLE_TEXDATA #%d dataAddr=0x%08X fmt=%u(0=I4,1=I8,2=IA4,3=IA8,4=RGB565,"
                    "5=RGB5A3,6=RGBA8,8=CMPR) %ux%u mip=%u size=%u hostPtrNull=%d min=0x%02X max=0x%02X "
                    "zeroBytes=%u/%u ffBytes=%u/%u\n",
                    wipeTexLogged, meta.dataAddr, meta.format, meta.width, meta.height,
                    meta.mipmap ? 1u : 0u, size, src == nullptr, minB, maxB, zeroCount, size, ffCount, size);
        }
    }
    // DIAGNOSTIC (temporary): NSMBW_LOG_WRAP_MODE, narrowed to the exact object flagged in
    // GX__InitTexObj_801707f8's comment above (oa=0x8043FC48, P_back_00/P_mask_00's shared
    // GXTexObj). That function's own wrap-mode log never fired even once this run (grep for
    // "NSMBW_WRAP_MODE GXInitTexObj" turns up nothing), so this object's wrap mode is not
    // arriving through GX__InitTexObj_801707f8 at all - it must be getting its meta from
    // TryGetOrExtractTexObjMeta's guest-memory fallback instead. This prints what THAT path
    // actually read for wrapS/wrapT, right before GXLoadTexObj applies it. Remove once resolved.
    if (oa == 0x8043FC48u && AURORA_ENV("NSMBW_LOG_WRAP_MODE") != nullptr) {
        RT_LOGF(RT_TAG_GX, "NSMBW_TARGET_WRAP oa=0x%08X raw wrapS=%u wrapT=%u fmt=%u %ux%u\n",
                oa, meta.wrapS, meta.wrapT, meta.format, meta.width, meta.height);
        // DIAGNOSTIC (temporary): P_back_00's own source bytes - user-reported symptom is the
        // whole WiiStrap screen rendering solid white, not just the P_stripe_00 accent this block
        // was originally added for. Since GXLoadTexObj logs zero rejections for this run (every
        // texture, including this one, passes the meta/format/dimension/data-range checks and
        // gets uploaded), the failure - if it is a data problem at all - has to be in what's
        // actually AT meta.dataAddr, not in whether the load was attempted. RGB5A3 is 2 bytes/
        // texel, no block tiling like I4 (see GXGetTexBufferSize) - print the first 8 texels
        // (16 bytes) as raw shorts, same GuestToHostPtr access pattern the I4 dump above uses.
        // Remove once resolved.
        if (AURORA_ENV("NSMBW_LOG_PBACK_RAWBYTES") != nullptr) {
            static int pbackRawLogged = 0;
            if (pbackRawLogged < 5) {
                ++pbackRawLogged;
                // Wider scan (temporary, same removal note as above): is the RARC container
                // (magic 55 AA 38 2D, confirmed from the real WiiStrap.arc file on disc) present
                // anywhere near dataAddr at all, or is this address disconnected from the loaded
                // archive entirely? Scans 64KB back from dataAddr in 4-byte steps - generous
                // enough to cover the whole archive if dataAddr sits somewhere past its start.
                {
                    constexpr uint32_t kScanBack = 64 * 1024;
                    const uint32_t scanStart = meta.dataAddr > kScanBack ? meta.dataAddr - kScanBack : 0;
                    bool foundMagic = false;
                    uint32_t magicAt = 0;
                    for (uint32_t addr = scanStart; addr + 4 <= meta.dataAddr + 4; addr += 4) {
                        const uint8_t* p4 = static_cast<const uint8_t*>(GuestToHostPtr(addr, 4));
                        if (p4 && p4[0] == 0x55 && p4[1] == 0xAA && p4[2] == 0x38 && p4[3] == 0x2D) {
                            foundMagic = true;
                            magicAt = addr;
                            break;
                        }
                    }
                    RT_LOGF(RT_TAG_GX,
                            "NSMBW_PBACK_RARC_SCAN dataAddr=0x%08X scanStart=0x%08X foundMagic=%d magicAt=0x%08X (offset=%d)\n",
                            meta.dataAddr, scanStart, foundMagic ? 1 : 0, magicAt,
                            foundMagic ? static_cast<int32_t>(meta.dataAddr - magicAt) : -1);
                }
                const uint8_t* src = static_cast<const uint8_t*>(GuestToHostPtr(meta.dataAddr, 16));
                char hex[3 * 16 + 1] = {};
                char* p = hex;
                for (uint32_t i = 0; i < 16; ++i) {
                    const uint8_t b = src ? src[i] : 0xFF;
                    p += std::snprintf(p, 4, "%02X ", b);
                }
                RT_LOGF(RT_TAG_GX,
                        "NSMBW_PBACK_RAWBYTES dataAddr=0x%08X hostPtrNull=%d fmt=%u %ux%u bytes: %s\n",
                        meta.dataAddr, src == nullptr, meta.format, meta.width, meta.height, hex);
            }
        }
        // NSMBW_TARGET_WRAP_PANE_ID: identify which pane this REPEAT load belongs to. This
        // object's wrap mode was CLAMP for the whole WiiStrap screen (confirmed above) but
        // later loads on this same reused GXTexObj slot come back REPEAT for an 8x8 texture -
        // want to know whether that's the save-data-created dialog before treating it as a bug
        // rather than an intentionally tiled background. Own budget (5), separate from
        // NSMBW_LOG_PANE_IDENTITY's 60-call general survey, so it still fires even after that
        // survey's budget is long spent by the time this screen shows up. Remove once resolved.
        if (meta.wrapS == GX_REPEAT || meta.wrapT == GX_REPEAT) {
            if (AURORA_ENV("NSMBW_LOG_PANE_IDENTITY") != nullptr) {
                static int targetRepeatLogged = 0;
                if (targetRepeatLogged < 5) {
                    ++targetRepeatLogged;
                    NsmbwScanForPaneName("NSMBW_TARGET_WRAP_PANE_ID", targetRepeatLogged, oa, tid);
                }
            }
            // NSMBW_TARGET_WRAP_BLEND: identified pane is P_stripe_00, a decorative tile whose
            // REPEAT wrap is presumably correct by design - the reference screenshot shows it as
            // a faint watermark, not bold bands, so the more likely bug is that it's compositing
            // at full opacity instead of blended low-alpha. This reports the blend state active
            // for this draw (type 0=NONE means blending is off entirely - texture alpha is
            // ignored and it draws fully opaque, which would explain bold-instead-of-faint).
            // Gated on wrapS/wrapT==REPEAT so it only fires for P_stripe_00's own loads, not the
            // earlier CLAMP-wrapped P_back_00/P_mask_00 loads on this same reused slot. Remove
            // once resolved.
            static int targetBlendLogged = 0;
            if (targetBlendLogged < 20) {
                ++targetBlendLogged;
                RT_LOGF(RT_TAG_GX,
                        "NSMBW_TARGET_WRAP_BLEND type=%u(0=NONE,1=BLEND,2=LOGIC,3=SUBTRACT) src=%u dst=%u op=%u setCount=%u\n",
                        g_nsmbwLastBlendDiag.type, g_nsmbwLastBlendDiag.src, g_nsmbwLastBlendDiag.dst,
                        g_nsmbwLastBlendDiag.op, g_nsmbwLastBlendDiag.setCount);
            }
            // NSMBW_TARGET_WRAP_TEV: blend mode is confirmed correct (SRCALPHA/INVSRCALPHA), so
            // whether that actually fades P_stripe_00 in/out depends on whether the TEV stage
            // bound to this GXTexMapID (tid) actually routes the I4 texture's intensity value
            // into its alpha output (GX_CA_TEXA=4), versus a constant (GX_CA_KONST=6/GX_CA_ONE)
            // that would make every texel fully opaque regardless of the (possibly-correct)
            // texture data. An earlier version of this probe read g_nsmbwLastTevStage, populated
            // by this file's own GX__SetTevOrder/ColorIn/AlphaIn overrides - those turned out to
            // be bound at MKW's addresses (confirmed via projects/mkwii/MAP.txt) and never fire
            // for NSMBW, so that array stayed all-zero. This reads aurora's actual live BP-decoded
            // state instead, mirrored into g_nsmbwLiveTev* by command_processor.cpp (that file's
            // own comment has the full reasoning). Remove once resolved, along with the mirror
            // globals and their extern declarations below.
            static int targetTevLogged = 0;
            if (targetTevLogged < 10) {
                ++targetTevLogged;
                // Dumps every ACTIVE stage (0..numTevStages-1), not just the one bound to this
                // texture: a later stage could still read APREV (the previous stage's alpha
                // output) and multiply it by a low RASA/KONST value from a fade animation, which
                // would make the final on-screen alpha much lower than TEXA alone suggests.
                // Stages at/beyond numTevStages hold stale state from an unrelated earlier
                // material and must not be read as if they were part of this draw.
                for (uint32_t st = 0; st < g_nsmbwLiveNumTevStages && st < 16; ++st) {
                    RT_LOGF(RT_TAG_GX,
                            "NSMBW_TARGET_WRAP_TEV numStages=%u stage=%u texMap=%u channelId=%u "
                            "alphaIn a=%u b=%u c=%u d=%u "
                            "(alphaArg: 0=APREV,1=A0,2=A1,3=A2,4=TEXA,5=RASA,6=KONST,7=ZERO) | "
                            "regAlpha APREV=%.3f A0=%.3f A1=%.3f A2=%.3f\n",
                            g_nsmbwLiveNumTevStages, st, g_nsmbwLiveTevTexMap[st], g_nsmbwLiveTevChannelId[st],
                            g_nsmbwLiveTevAlphaA[st],
                            g_nsmbwLiveTevAlphaB[st], g_nsmbwLiveTevAlphaC[st], g_nsmbwLiveTevAlphaD[st],
                            g_nsmbwLiveTevRegAlpha[0], g_nsmbwLiveTevRegAlpha[1],
                            g_nsmbwLiveTevRegAlpha[2], g_nsmbwLiveTevRegAlpha[3]);
                    // GX_COLOR0A0=4 -> alpha comes from channel index GX_ALPHA0(=2)'s config/values;
                    // GX_COLOR1A1=5 -> GX_ALPHA1(=3). Other channelId values (bump/zero) don't use
                    // matSrc/matColor this way and are skipped. (channelId enum: GX_COLOR0=0,
                    // GX_COLOR1=1, GX_ALPHA0=2, GX_ALPHA1=3, GX_COLOR0A0=4, GX_COLOR1A1=5.)
                    uint32_t alphaChanIdx = 0xFFu;
                    if (g_nsmbwLiveTevChannelId[st] == 4u) alphaChanIdx = 2u;
                    else if (g_nsmbwLiveTevChannelId[st] == 5u) alphaChanIdx = 3u;
                    if (alphaChanIdx < 4u) {
                        RT_LOGF(RT_TAG_GX,
                                "NSMBW_TARGET_WRAP_CHAN stage=%u alphaChanIdx=%u matSrc=%u(0=REG,1=VTX) "
                                "matColor.a=%.3f ambColor.a=%.3f\n",
                                st, alphaChanIdx, g_nsmbwLiveChanMatSrc[alphaChanIdx],
                                g_nsmbwLiveChanMatColorA[alphaChanIdx], g_nsmbwLiveChanAmbColorA[alphaChanIdx]);
                    }
                }
            }
            // NSMBW_TARGET_WRAP_RAWBYTES: the alpha formula is now confirmed to be a straight
            // passthrough of the I4 texture's own intensity (output_alpha = TEXA, since A0=0/
            // A1=1 are just lerp anchors) - so every piece of *configuration* between texture and
            // screen (wrap, blend, TEV routing, TEV constants) checks out. The only thing left
            // unverified is the actual decoded pixel data. An 8x8 I4 texture is exactly one GX
            // tile (block size 8x8 for I4/C4/CMPR, per aurora's texture_convert.cpp), stored as
            // 64 4-bit texels = 32 bytes, two texels per byte, high nibble first, in simple
            // row-major order within the tile (no sub-tiling possible since the whole texture is
            // one block). This dumps those 32 raw bytes so they can be hand-decoded and checked
            // against what TextureDecoderI4 produces, and against whether high-intensity (near-
            // white/opaque) values here are actually correct source data (wrong asset/wrong
            // dataAddr) or a decode bug. Remove once resolved.
            static int targetRawBytesLogged = 0;
            if (targetRawBytesLogged < 3 && meta.format == GX_TF_I4 && meta.width == 8 && meta.height == 8) {
                ++targetRawBytesLogged;
                // meta.dataAddr is already CanonicalizeGxMainRamAddress()'d (a physical, non-
                // 0x80000000-prefixed offset - see that function's own comment for the MEM1/MEM2
                // scheme). Memory::Read8 expects the original virtual guest address instead (used
                // that way everywhere else in this file, e.g. NsmbwScanForPaneName's direct
                // 0x80000000-0x817FFFFF reads) - an earlier version of this dump called
                // Memory::Read8(meta.dataAddr + i) directly, which is the wrong address format for
                // an already-canonicalized value and produced meaningless bytes. GuestToHostPtr is
                // what the real upload path (GXInitTexObjData below) already calls on this exact
                // meta.dataAddr, so it's used the same way here.
                char hex[3 * 32 + 1] = {};
                char* p = hex;
                const uint8_t* src = static_cast<const uint8_t*>(GuestToHostPtr(meta.dataAddr, 32));
                for (uint32_t i = 0; i < 32; ++i) {
                    const uint8_t b = src ? src[i] : 0xFF;
                    p += std::snprintf(p, 4, "%02X ", b);
                }
                RT_LOGF(RT_TAG_GX, "NSMBW_TARGET_WRAP_RAWBYTES dataAddr=0x%08X hostPtrNull=%d bytes: %s\n",
                        meta.dataAddr, src == nullptr, hex);
            }
        }
    }
    const GXTexWrapMode wrapS = SanitizeWrapMode(meta.wrapS);
    const GXTexWrapMode wrapT = SanitizeWrapMode(meta.wrapT);
    const GXTexFilter minFilter = SanitizeMinFilter(meta.minFilter);
    const GXTexFilter magFilter = SanitizeMagFilter(meta.magFilter);
    const GXAnisotropy maxAnisoSafe = SanitizeAniso(meta.maxAniso);
    meta.wrapS = static_cast<uint32_t>(wrapS);
    meta.wrapT = static_cast<uint32_t>(wrapT);
    meta.minFilter = static_cast<uint32_t>(minFilter);
    meta.magFilter = static_cast<uint32_t>(magFilter);
    meta.maxAniso = static_cast<uint32_t>(maxAnisoSafe);
    meta.minLod = clampedMinLodSafe;
    meta.maxLod = maxLodSafe;
    meta.lodBias = lodBiasSafe;
    
    // Check if host texture object exists, create if needed
    GXTexObj* obj = TryGetHostTexObj(oa);
    const bool isPalette = IsPaletteTexFormat(meta.format);
    bool needsInit = (obj == nullptr);
    const auto metaIt = g_TexObjMeta.find(oa);
    if (!needsInit && metaIt != g_TexObjMeta.end()) {
        const TexObjMeta& cached = metaIt->second;
        if (cached.width != meta.width || cached.height != meta.height || cached.format != meta.format ||
            cached.wrapS != meta.wrapS || cached.wrapT != meta.wrapT || cached.mipmap != meta.mipmap ||
            cached.minFilter != meta.minFilter || cached.magFilter != meta.magFilter ||
            cached.minLod != meta.minLod || cached.maxLod != meta.maxLod ||
            cached.lodBias != meta.lodBias || cached.biasClamp != meta.biasClamp ||
            cached.edgeLod != meta.edgeLod || cached.maxAniso != meta.maxAniso ||
            cached.tlut != meta.tlut || cached.userData != meta.userData) {
            needsInit = true;
        }
    }
    bool tlutChanged = false;
    bool textureDataUploaded = false;
    try {
        if (needsInit) {
            if (!obj) {
                obj = CreateHostTexObj(oa);
            }
            void* dp = GuestToHostPtr(meta.dataAddr, size);
            if (dp) {
                if (isPalette) {
                    GXInitTexObjCI(obj, dp, meta.width, meta.height, (GXCITexFmt)meta.format,
                                   wrapS, wrapT,
                                   meta.mipmap ? GX_TRUE : GX_FALSE, meta.tlut);
                } else {
                    GXInitTexObj(obj, dp, meta.width, meta.height, (GXTexFmt)meta.format,
                                 wrapS, wrapT,
                                 meta.mipmap ? GX_TRUE : GX_FALSE);
                }
                ApplyHostTexObjLod(obj, meta, minFilter, magFilter, meta.lodBias, maxAnisoSafe);
                GXInitTexObjUserData(obj, GuestToHostPtr(meta.userData));
            }
            MarkHostTexObjConstructed(oa);
            // Write through GetTexObjMeta so the DCStoreRange interval index is
            // told this entry's backing may have moved.
            GetTexObjMeta(oa) = meta;
        } else if (isPalette && metaIt != g_TexObjMeta.end() && metaIt->second.tlut != meta.tlut) {
            GXInitTexObjTlut(obj, meta.tlut);
            GetTexObjMeta(oa).tlut = meta.tlut;
            tlutChanged = true;
        }

        const void* dp = GuestToHostPtr(meta.dataAddr, size);
        if (dp && meta.needsUpload) {
            GXInitTexObjData(obj, dp);
            meta.needsUpload = false;
            textureDataUploaded = true;
        }
        // Everything the binding contract compares apart from objAddr is the
        // shared sampler state, so copy that base wholesale; only dataAddr needs
        // to be canonicalized on the way in.
        BoundTexInfo newBound{};
        static_cast<GxTextureBindingContract::SamplerState&>(newBound) = meta;
        newBound.objAddr = oa;
        newBound.dataAddr = CanonicalizeGxMainRamAddress(meta.dataAddr);
        const BoundTexInfo& oldBound = g_boundTexMaps[tid];
        const bool canSkipHostLoad = GxTextureBindingContract::CanSkipHostLoad(
            oldBound, newBound, needsInit, tlutChanged, textureDataUploaded);

        g_boundTexMaps[tid] = newBound;
        GetTexObjMeta(oa) = meta;
        if (!canSkipHostLoad) {
            GXLoadTexObj(obj, (GXTexMapID)tid);
            // FIX: see NsmbwInvalidateMergeStateForTextureUpload's own comment
            // (aurora-main/lib/gx/command_processor.cpp) for the full reasoning. This upload path
            // never touches aurora's BP-register-driven stateDirty flag, so without this, aurora's
            // consecutive-draw merge optimization can silently batch this draw together with
            // whatever draw came before it, sharing that earlier draw's texture bind group instead
            // of this slot's newly-bound one.
            NsmbwInvalidateMergeStateForTextureUpload();
        }
    } catch (const std::exception& ex) {
        if (s_hostExceptionLogCount++ < 64) {
            RT_LOGF(RT_TAG_GX,
                    "GXLoadTexObj rejected reason=host-exception (%s) oa=0x%08X tid=%u fmt=0x%X %ux%u data=0x%08X\n",
                    ex.what(), oa, tid, meta.format, meta.width, meta.height, meta.dataAddr);
        }
        BindUnloadableTexturePlaceholder(tid);
        return;
    } catch (...) {
        if (s_hostExceptionLogCount++ < 64) {
            RT_LOGF(RT_TAG_GX,
                    "GXLoadTexObj rejected reason=host-exception-unknown oa=0x%08X tid=%u fmt=0x%X %ux%u data=0x%08X\n",
                    oa, tid, meta.format, meta.width, meta.height, meta.dataAddr);
        }
        BindUnloadableTexturePlaceholder(tid);
        return;
    }
    try {
        uint32_t gd = Memory::Read32(kGXDataPtrAddr);
        if (gd) {
#ifdef MKW_RUNTIME_PRODUCT_NSMBW
            // The SDK's GXLoadTexObj also caches the object's image0/mode0 words in __GXData
            // (tImage0[id] / tMode0[id]) for __GXSetSUTexRegs, which derives the SU_TS0/1
            // texture-size registers (BP 0x30/0x31) from them at the next dirty-state flush.
            // MKW HLEs __GXSetSUTexRegs natively so it never reads these; NSMBW runs the guest's
            // own (0x801C7A10 -> __SetSURegs 0x801C7980, which reads __gx+0x564+4*idx and
            // __gx+0x584+4*idx and writes __gx+0x108/+0x128). With this override replacing the
            // SDK body those words stayed zero, every 2D layout draw ran with SU scale 1x1
            // (NSMBW_LOG_DRAW_TEXGEN: "suScale=1/1" against 628x96 textures), and aurora - which
            // samples at uv * su_scale / texture_size - magnified a one-texel sliver across each
            // pane. Guest GXTexObj layout: word0 = mode0, word2 = image0 (see
            // ExtractTexObjMetaFromGuest).
            constexpr uint32_t kNsmbwGxTImage0Off = 0x564u;
            constexpr uint32_t kNsmbwGxTMode0Off = 0x584u;
            if (tid < 8u) {
                Memory::Write32(gd + kNsmbwGxTImage0Off + tid * 4u, Memory::Read32(oa + 0x08u));
                Memory::Write32(gd + kNsmbwGxTMode0Off + tid * 4u, Memory::Read32(oa + 0x00u));
            }
#endif
            Memory::Write32(gd + 0x5FCu, Memory::Read32(gd + 0x5FCu) | 1u);
            Memory::Write16(gd + 2, 0);
        }
    } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(80170f2c, GX__LoadTexObj_80170f2c, (uint32_t oa, uint32_t tid), (oa, tid));

extern "C" void GX__LoadTexObjPreLoaded_80170dc8(uint32_t oa, uint32_t tid) { GX__LoadTexObj_80170f2c(oa, tid); }
PPC_NATIVE_OVERRIDE_VOID(80170dc8, GX__LoadTexObjPreLoaded_80170dc8, (uint32_t oa, uint32_t tid), (oa, tid));

// ============================================================================
// Texture Object Getters
// ============================================================================

extern "C" void GX__GetTexObjAll_80170bf8(uint32_t oa, uint32_t dp, uint32_t wp, uint32_t hp, uint32_t fp, uint32_t wsp, uint32_t wtp, uint32_t mp) {
    std::lock_guard<std::mutex> guard(g_texObjMutex);
    TexObjMeta meta;
    if (!TryGetOrExtractTexObjMeta(oa, meta)) {
        RT_LOGF(RT_TAG_GX, "GXGetTexObjAll: failed to get metadata for GXTexObj @0x%08X\n", oa);
        // Return zeros on failure
        if (dp) Memory::Write32(dp, 0); if (wp) Memory::Write16(wp, 0); if (hp) Memory::Write16(hp, 0); 
        if (fp) Memory::Write32(fp, 0); if (wsp) Memory::Write32(wsp, 0); if (wtp) Memory::Write32(wtp, 0); if (mp) Memory::Write8(mp, 0);
        return;
    }
    if (dp) Memory::Write32(dp, meta.dataAddr); if (wp) Memory::Write16(wp, meta.width); if (hp) Memory::Write16(hp, meta.height); if (fp) Memory::Write32(fp, meta.format); if (wsp) Memory::Write32(wsp, meta.wrapS); if (wtp) Memory::Write32(wtp, meta.wrapT); if (mp) Memory::Write8(mp, meta.mipmap ? 1 : 0);
}
PPC_NATIVE_OVERRIDE_VOID(80170bf8, GX__GetTexObjAll_80170bf8, (uint32_t oa, uint32_t dp, uint32_t wp, uint32_t hp, uint32_t fp, uint32_t wsp, uint32_t wtp, uint32_t mp), (oa, dp, wp, hp, fp, wsp, wtp, mp));

extern "C" void GX__GetTexObjLODAll_80170cbc(uint32_t oa, uint32_t mifp, uint32_t mafp, uint32_t milp, uint32_t malp, uint32_t lbp, uint32_t bcp, uint32_t elp, uint32_t map) {
    std::lock_guard<std::mutex> guard(g_texObjMutex);
    TexObjMeta meta;
    if (!TryGetOrExtractTexObjMeta(oa, meta)) {
        RT_LOGF(RT_TAG_GX, "GXGetTexObjLODAll: failed to get metadata for GXTexObj @0x%08X\n", oa);
        // Return zeros on failure
        if (mifp) Memory::Write32(mifp, 0); if (mafp) Memory::Write32(mafp, 0); if (milp) Memory::WriteFloat32(milp, 0.0f);
        if (malp) Memory::WriteFloat32(malp, 0.0f); if (lbp) Memory::WriteFloat32(lbp, 0.0f); if (bcp) Memory::Write8(bcp, 0);
        if (elp) Memory::Write8(elp, 0); if (map) Memory::Write32(map, 0);
        return;
    }
    if (mifp) Memory::Write32(mifp, meta.minFilter); if (mafp) Memory::Write32(mafp, meta.magFilter); if (milp) Memory::WriteFloat32(milp, meta.minLod); if (malp) Memory::WriteFloat32(malp, meta.maxLod); if (lbp) Memory::WriteFloat32(lbp, meta.lodBias); if (bcp) Memory::Write8(bcp, meta.biasClamp ? 1 : 0); if (elp) Memory::Write8(elp, meta.edgeLod ? 1 : 0); if (map) Memory::Write32(map, meta.maxAniso);
}
PPC_NATIVE_OVERRIDE_VOID(80170cbc, GX__GetTexObjLODAll_80170cbc, (uint32_t oa, uint32_t mifp, uint32_t mafp, uint32_t milp, uint32_t malp, uint32_t lbp, uint32_t bcp, uint32_t elp, uint32_t map), (oa, mifp, mafp, milp, malp, lbp, bcp, elp, map));

// ============================================================================
// TLUT (Texture Lookup Table)
// ============================================================================

extern "C" void GX__InitTlutObj_80170f80(uint32_t oa, uint32_t da, uint32_t f, uint32_t e) {
    std::lock_guard<std::mutex> guard(g_tlutObjMutex); GXTlutObj* obj = CreateHostTlutObj(oa); TlutObjMeta& meta = GetTlutObjMeta(oa);
    meta.dataAddr=CanonicalizeGxMainRamAddress(da); meta.format=f; meta.entries=(u16)e; meta.dirty=false;
    // Leave the object unconstructed on a bad descriptor. A later GXLoadTlut
    // then takes GetHostTlutObj's soft-fail path and gets a fresh empty object
    // instead of aurora reading entries*2 bytes off an unvalidated pointer.
    if (!ValidateTlutData(oa, meta)) return;
    GXInitTlutObj(obj, GuestToHostPtr(da), (GXTlutFmt)f, (u16)e); MarkHostTlutObjConstructed(oa);
}
PPC_NATIVE_OVERRIDE_VOID(80170f80, GX__InitTlutObj_80170f80, (uint32_t oa, uint32_t da, uint32_t f, uint32_t e), (oa, da, f, e));

extern "C" void GX__LoadTlut_80170fa8(uint32_t oa, uint32_t tl) { std::lock_guard<std::mutex> guard(g_tlutObjMutex); if (tl >= kMaxTluts) { RT_LOGF(RT_TAG_GX, "GXLoadTlut: invalid TLUT index %u (oa=0x%08X)\n", tl, oa); return; } TlutObjMeta& meta = GetTlutObjMeta(oa); if (meta.dirty) { if (ValidateTlutData(oa, meta)) { GXTlutObj* rebuild = CreateHostTlutObj(oa); GXInitTlutObj(rebuild, GuestToHostPtr(meta.dataAddr), (GXTlutFmt)meta.format, meta.entries); MarkHostTlutObjConstructed(oa); } meta.dirty = false; } GXTlutObj* obj = GetHostTlutObj(oa); GXLoadTlut(obj, (GXTlut)tl); try { uint32_t gd = Memory::Read32(kGXDataPtrAddr); if (gd) Memory::Write16(gd + 2, 0); } catch (...) {} }
PPC_NATIVE_OVERRIDE_VOID(80170fa8, GX__LoadTlut_80170fa8, (uint32_t oa, uint32_t tl), (oa, tl));

// ============================================================================
// Texture Invalidation and Coordinate Control
// ============================================================================

extern "C" void GX__InvalidateTexAll_80171110() {
    // Real GX invalidates its internal texture cache here. Aurora forwards the
    // invalidate to the renderer, which keeps unchanged source uploads hot but
    // revalidates reused guest buffers before serving cached texture handles.
    GXInvalidateTexAll();
}
PPC_NATIVE_OVERRIDE_VOID(80171110, GX__InvalidateTexAll_80171110, (), ());

extern "C" void GX__SetTexCoordScaleManually_80171180(uint32_t c, uint32_t en, uint32_t ss, uint32_t ts) { GXSetTexCoordScaleManually((GXTexCoordID)c, (GXBool)en, (u16)ss, (u16)ts); try{ uint32_t gd=Memory::Read32(kGXDataPtrAddr); if(gd){ Memory::Write32(gd+0x5E4u, (Memory::Read32(gd+0x5E4u)&~(1u<<c))|((en&1u)<<c)); if(en){ uint32_t sa=gd+0x108u+c*4u, ta=gd+0x128u+c*4u; Memory::Write32(sa, (Memory::Read32(sa)&0xFFFF0000u)|((ss-1)&0xFFFFu)); Memory::Write32(ta, (Memory::Read32(ta)&0xFFFF0000u)|((ts-1)&0xFFFFu)); Memory::Write16(gd+2, 0); } } }catch(...){} }
PPC_NATIVE_OVERRIDE_VOID(80171180, GX__SetTexCoordScaleManually_80171180, (uint32_t c, uint32_t en, uint32_t ss, uint32_t ts), (c, en, ss, ts));

extern "C" void GX__SetTexCoordBias_801711fc(uint32_t c, uint32_t se, uint32_t te) { GXSetTexCoordBias((GXTexCoordID)c, (GXBool)se, (GXBool)te); try{ uint32_t gd=Memory::Read32(kGXDataPtrAddr); if(gd){ uint32_t sa=gd+0x108u+c*4u, ta=gd+0x128u+c*4u; Memory::Write32(sa, (Memory::Read32(sa)&0xFFFEFFFFu)|((se&1u)<<16)); Memory::Write32(ta, (Memory::Read32(ta)&0xFFFEFFFFu)|((te&1u)<<16)); if(Memory::Read32(gd+0x5E4u)&(1u<<c)) Memory::Write16(gd+2, 0); } }catch(...){} }
PPC_NATIVE_OVERRIDE_VOID(801711fc, GX__SetTexCoordBias_801711fc, (uint32_t c, uint32_t se, uint32_t te), (c, se, te));
