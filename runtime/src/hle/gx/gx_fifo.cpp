#include "gx_internal.h"
#include <aurora/env.hpp>
#include "gx_stream_common.h"
#include "gx_cp_decode.h"
#include "isa/big_endian.h"
#include "abi_bridge.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
extern "C" void GxSyncVtxAttrFmtToAurora(uint32_t fmt);
extern "C" void GxSyncVtxDescToAurora();

// Set by projects/nsmbw/native/nsmbw_create_next_scene_diag.cpp on every successful scene
// transition (0=BOOT, per fProf::PROFILE_NAME_e) - same extern already declared/used for scene
// gating in aurora-main/lib/internal.hpp's NSMBW_GPU_PEEK_SCENE diagnostic. Declared again here
// (not by including that header) so this file's targeted alpha-patch experiment below can gate on
// it without pulling in the unrelated GPU-peek diagnostic.
extern "C" uint32_t g_nsmbwCurrentSceneProfile;
extern "C" uint32_t g_nsmbwXfLoadSource;

// DIAGNOSTIC (temporary): live TEV state mirrored by aurora-main's command_processor.cpp - see
// that file's g_nsmbwLiveTevAlpha*/g_nsmbwLiveTevColor* comments. Used by NSMBW_LOG_DRAW_TEV
// below to check what TEV routing is actually active for a real vertex-submission draw.
extern "C" {
extern uint32_t g_nsmbwLiveTevColorA[16];
extern uint32_t g_nsmbwLiveTevColorB[16];
extern uint32_t g_nsmbwLiveTevColorC[16];
extern uint32_t g_nsmbwLiveTevColorD[16];
extern uint32_t g_nsmbwLiveTevAlphaA[16];
extern uint32_t g_nsmbwLiveTevAlphaB[16];
extern uint32_t g_nsmbwLiveTevAlphaC[16];
extern uint32_t g_nsmbwLiveTevAlphaD[16];
extern uint32_t g_nsmbwLiveTevTexMap[16];
extern uint32_t g_nsmbwLiveNumTevStages;
extern bool g_nsmbwLiveDepthCompare;
extern uint32_t g_nsmbwLiveDepthFunc;
extern bool g_nsmbwLiveDepthUpdate;
extern float g_nsmbwLiveTevRegAlpha[4];
extern float g_nsmbwLiveTevRegColorR[4];
extern float g_nsmbwLiveTevRegColorG[4];
extern float g_nsmbwLiveTevRegColorB[4];
}

// Opcode constants and the stream helpers this file shares with gx_dl.cpp /
// gx_vertex.cpp; see gx_stream_common.h.
using namespace GxCmd;
using namespace GxStream;

HleGxState g_hleGxState;

// DIAGNOSTIC (temporary): NSMBW_LOG_FIFO_DESYNC. The intro cutscene crash (aurora FATAL
// "unmapped vtx attr 9", i.e. a draw whose VCD has no position) is preceded by dozens of
// "GX guest pointer: memory error at 0x<float-looking address>" lines from the CALL_DL arm
// below - the raw-FIFO parser is reading vertex float data as command bytes, so its idea of
// the vertex size for the preceding draw differs from what the guest wrote. Each raw draw is
// recorded here with the layout the shadow VCD/VAT produced and, for comparison, the guest's
// own __gx VCD/VAT (vcdLo @+0x14, vcdHi @+0x18, vatA/B/C[fmt] @+0x1C/0x3C/0x5C - offsets read
// off NSMBW's GXSetVtxDesc 0x801C3900 / GXSetVtxAttrFmt 0x801C41F0, one word less than the
// usual SDK __GXData layout).
// When the parser then hits an unknown command byte the ring and the next FIFO bytes are
// dumped, so the first mismatched draw is identifiable without guessing.
namespace {
struct FifoDesyncDrawRecord {
    uint32_t seq = 0;
    uint32_t streamPos = 0; // parse position: total bytes pushed minus bytes still buffered
    uint8_t cmd = 0;
    uint16_t count = 0;
    uint32_t rawVertexSize = 0;
    bool rawPath = false;
    uint32_t lr = 0;
    uint32_t pc = 0;
    uint32_t guestVcdLo = 0, guestVcdHi = 0, guestVatA = 0, guestVatB = 0, guestVatC = 0;
    char layout[160] = {};
};
constexpr int kFifoDesyncRing = 24;
FifoDesyncDrawRecord g_fifoDesyncRing[kFifoDesyncRing];
uint32_t g_fifoDesyncSeq = 0;
int g_fifoDesyncUnknownLogged = 0;
// Rolling copy of the last raw bytes the guest pushed (pre-parse), so a dump shows what was
// actually written ahead of the byte the parser could not place.
constexpr uint32_t kFifoDesyncHist = 480;
uint8_t g_fifoDesyncHist[kFifoDesyncHist];
uint32_t g_fifoDesyncHistPos = 0; // doubles as the total-bytes-pushed counter
void FifoDesyncHistPush(u32 value, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t shift = (count - 1u - i) * 8u;
        g_fifoDesyncHist[g_fifoDesyncHistPos++ % kFifoDesyncHist] = static_cast<uint8_t>((value >> shift) & 0xFFu);
    }
}

bool FifoDesyncEnabled() {
    static const bool enabled = AURORA_ENV("NSMBW_LOG_FIFO_DESYNC") != nullptr;
    return enabled;
}

void FifoDesyncRecordDraw(uint8_t cmd, uint16_t count, uint32_t rawVertexSize, bool rawPath) {
    auto& rec = g_fifoDesyncRing[g_fifoDesyncSeq % kFifoDesyncRing];
    rec = FifoDesyncDrawRecord{};
    rec.seq = g_fifoDesyncSeq++;
    rec.streamPos = g_fifoDesyncHistPos - g_hleGxState.fifoByteCount;
    rec.cmd = cmd;
    rec.count = count;
    rec.rawVertexSize = rawVertexSize;
    rec.rawPath = rawPath;
    if (CpuContext* cc = TryGetCpuContext()) {
        rec.lr = static_cast<uint32_t>(cc->lr);
        rec.pc = static_cast<uint32_t>(cc->pc);
    }
    const uint32_t fmt = cmd & GX_VAT_MASK_CMD;
    constexpr uint32_t kGxDataPtrAddr = 0x80433360u - 0x4EF8u; // *(r2 - 0x4EF8) = __gx (NSMBW)
    uint32_t gd = 0;
    if (Memory::TryRead32(kGxDataPtrAddr, gd) && gd != 0) {
        Memory::TryRead32(gd + 0x14u, rec.guestVcdLo);
        Memory::TryRead32(gd + 0x18u, rec.guestVcdHi);
        Memory::TryRead32(gd + 0x1Cu + fmt * 4u, rec.guestVatA);
        Memory::TryRead32(gd + 0x3Cu + fmt * 4u, rec.guestVatB);
        Memory::TryRead32(gd + 0x5Cu + fmt * 4u, rec.guestVatC);
    }
    size_t used = 0;
    for (int attr = 0; attr < 26 && used + 20 < sizeof(rec.layout); ++attr) {
        const GXAttrType type = g_hleGxState.vtxDesc[attr];
        if (type == GX_NONE) continue;
        const VtxAttrFmt& f = g_hleGxState.vtxAttrFmt[fmt][attr];
        const uint32_t bytes = (type == GX_DIRECT)
            ? DirectAttrByteSize(static_cast<GXAttr>(attr), f, true, 1u)
            : (type == GX_INDEX8 ? 1u : 2u);
        used += static_cast<size_t>(std::snprintf(rec.layout + used, sizeof(rec.layout) - used,
                                                  "%d:%s/c%d/t%d/%uB ", attr,
                                                  type == GX_DIRECT ? "D" : (type == GX_INDEX8 ? "I8" : "I16"),
                                                  static_cast<int>(f.cnt), static_cast<int>(f.type), bytes));
    }
}

void FifoDesyncDumpOnUnknown(const uint8_t* data, uint32_t avail) {
    // Boot alone produces a dozen of these (stray 0xFF padding), so the budget is per scene.
    static uint32_t lastScene = 0xFFFFFFFFu;
    if (lastScene != g_nsmbwCurrentSceneProfile) {
        lastScene = g_nsmbwCurrentSceneProfile;
        g_fifoDesyncUnknownLogged = 0;
    }
    if (g_fifoDesyncUnknownLogged >= 10) return;
    ++g_fifoDesyncUnknownLogged;
    uint32_t liveVcdLo = 0, liveVcdHi = 0, liveDirty = 0, gd = 0;
    if (Memory::TryRead32(0x80433360u - 0x4EF8u, gd) && gd != 0) {
        Memory::TryRead32(gd + 0x14u, liveVcdLo);
        Memory::TryRead32(gd + 0x18u, liveVcdHi);
        Memory::TryRead32(gd + 0x5FCu, liveDirty);
    }
    RT_LOGF(RT_TAG_GX, "FIFO_DESYNC scene=%u unknown cmd byte 0x%02X with %u bytes buffered (event %d); guest __gx now vcd=%08X/%08X dirty=%08X; last draws (cmd=0x08 rows are VCD reg writes: n=reg vtxBytes=value):\n",
            g_nsmbwCurrentSceneProfile, data[0], avail, g_fifoDesyncUnknownLogged, liveVcdLo, liveVcdHi, liveDirty);
    for (int i = kFifoDesyncRing; i > 0; --i) {
        if (g_fifoDesyncSeq < static_cast<uint32_t>(i)) continue;
        const auto& rec = g_fifoDesyncRing[(g_fifoDesyncSeq - static_cast<uint32_t>(i)) % kFifoDesyncRing];
        RT_LOGF(RT_TAG_GX, "  #%u @%u cmd=0x%02X n=%u vtxBytes=%u %s LR=0x%08X PC=0x%08X | guest vcd=%08X/%08X vat=%08X/%08X/%08X | shadow %s\n",
                rec.seq, rec.streamPos, rec.cmd, rec.count, rec.rawVertexSize, rec.rawPath ? "raw" : "incr", rec.lr, rec.pc,
                rec.guestVcdLo, rec.guestVcdHi, rec.guestVatA, rec.guestVatB, rec.guestVatC, rec.layout);
    }
    char hex[3 * 48 + 1] = {};
    const uint32_t n = avail < 48u ? avail : 48u;
    for (uint32_t i = 0; i < n; ++i) std::snprintf(hex + i * 3, 4, "%02X ", data[i]);
    RT_LOGF(RT_TAG_GX, "  next bytes: %s\n", hex);
    char hist[3 * kFifoDesyncHist + 1] = {};
    const uint32_t have = g_fifoDesyncHistPos < kFifoDesyncHist ? g_fifoDesyncHistPos : kFifoDesyncHist;
    for (uint32_t i = 0; i < have; ++i) {
        const uint32_t idx = (g_fifoDesyncHistPos - have + i) % kFifoDesyncHist;
        std::snprintf(hist + i * 3, 4, "%02X ", g_fifoDesyncHist[idx]);
    }
    RT_LOGF(RT_TAG_GX, "  last %u raw bytes written (oldest first, stream pos %u..%u): %s\n", have, g_fifoDesyncHistPos - have, g_fifoDesyncHistPos, hist);
}
} // namespace

namespace aurora::gx::fifo {
bool submit_raw_draw(GXPrimitive prim, GXVtxFmt fmt, const uint8_t* vertices, uint16_t vtxCount,
                     uint32_t vertexBytes);
}

void HleGxState::ResetVertex() {
    currentAttr = NextEnabledAttr(GX_VA_PNMTXIDX - 1);
    currentComp = 0;
}

GXAttr HleGxState::NextEnabledAttr(int startAttr) {
    for (int i = startAttr + 1; i < 26; ++i) {
        if (vtxDesc[i] != GX_NONE) {
            return static_cast<GXAttr>(i);
        }
    }
    return GX_VA_NULL;
}

int HleGxState::GetExpectedCompCount(GXAttr attr, const VtxAttrFmt& fmt) {
    // Attributes with no component layout (matrix indices, XF arrays, GX_VA_NBT)
    // count as one component here: the incremental parser advances one element
    // per raw stream item for them.
    return static_cast<int>(AttrCompCount(attr, fmt, 1u));
}

static void ApplyAuroraVtxStateForRawBegin(GXVtxFmt fmt) {
    // KNOWN DIVERGENCE: this is the only publish site with includeNbt=true, so it also
    // publishes GX_VA_NBT after GX_VA_NRM, which makes aurora's SETVAT fall-through clobber the
    // NRM VAT with the default. Raw-FIFO geometry renders correctly with it; do not "fix" without
    // an in-race A/B run.
    PublishAuroraVtxState(fmt, AuroraVtxPublishOptions{/*includeNbt=*/true,
                                                       /*fmtLoopFirst=*/0,
                                                       /*fmtLoopLast=*/25});
}

// There is deliberately no indexed-aurora path here. Immediate-mode indexed
// draws are parsed incrementally, and aurora's indexed-array upload has to
// precede the draw carrying the max-index bounds, which cannot be known without
// buffering the whole primitive. Indexed attributes are therefore expanded into
// the packed direct stream above (GX_INDEX8/16 -> GX_DIRECT).

static uint32_t GetDirectAttrByteSizeForFifo(GXAttr attr, const VtxAttrFmt& fmt) {
    return DirectAttrByteSize(attr, fmt, /*matrixAttrIsOneByte=*/true, /*fallbackComps=*/1u);
}

static bool TryGetRawDirectFifoVertexSize(GXVtxFmt fmt, uint32_t& vertexSize) {
    vertexSize = 0;
    if (fmt >= GX_MAX_VTXFMT) {
        return false;
    }

    for (int attr = 0; attr < 26; ++attr) {
        const GXAttrType type = g_hleGxState.vtxDesc[attr];
        if (type == GX_NONE) {
            continue;
        }
        if (type != GX_DIRECT) {
            return false;
        }

        const GXAttr gxAttr = static_cast<GXAttr>(attr);
        const uint32_t attrBytes =
            GetDirectAttrByteSizeForFifo(gxAttr, g_hleGxState.vtxAttrFmt[fmt][attr]);
        if (attrBytes == 0) {
            return false;
        }
        vertexSize += attrBytes;
    }

    return vertexSize != 0;
}

static bool TrySubmitRawDirectFifoDraw(const uint8_t* packet, uint32_t packetBytes, GXPrimitive prim,
                                       GXVtxFmt vtxFmt, uint16_t vtxCount) {
    if (packet == nullptr || packetBytes == 0 || vtxCount == 0) {
        return false;
    }

    EnsureAuroraFrameActive();

    g_hleGxState.currentVtxFmt = vtxFmt;
    g_hleGxState.currentPrim = prim;
    g_hleGxState.vertsRemaining = vtxCount;
    g_hleGxState.inBegin = false;
    g_hleGxState.auroraBeginCalled = false;
    g_hleGxState.ResetVertex();

    ApplyAuroraVtxStateForRawBegin(vtxFmt);
    EnsureDefaultGxAlphaCompare();

    if (AURORA_ENV("NSMBW_LOG_VERTS") != nullptr && g_hleGxState.vtxDesc[11] != GX_NONE) {
        static int logged = 0;
        if (logged < 60000) {
            ++logged;
            const uint8_t* v = packet + 3;
            const auto& posFmt = g_hleGxState.vtxAttrFmt[vtxFmt][GX_VA_POS];
            const auto& texFmt = g_hleGxState.vtxAttrFmt[vtxFmt][GX_VA_TEX0];
            const auto& clr0Fmt = g_hleGxState.vtxAttrFmt[vtxFmt][GX_VA_CLR0];
            // REDIRECTED TRACE: static .brlyt data for the strap/text panes and their vertex
            // corner colors is confirmed fully opaque (alpha=255 everywhere, no ancestor pane has
            // reduced alpha, no .brlan track touches them) - so a live alpha=0 in this exact
            // vertex byte stream must come from a runtime computation, not authored data. This
            // quad-emission call doesn't itself call further guest functions while writing raw
            // FIFO bytes (no bl seen inside the immediate-mode loop), so ctx->lr here should still
            // hold the return address into whatever guest function submitted THIS quad - i.e. the
            // Pane::Draw()-equivalent caller. Logging position too, to correlate against the
            // known-from-.brlyt static x/y/width/height of P_strap_00 and friends.
            const uint32_t lr = GetPersistentCpuContext().lr;
            // Disassembly of the caller at LR=0x8022DA28 (offline) shows the vertex color is
            // written via `lwz r0,8(r27); stw r0,-0x8000(r3)` - a direct, unconditional word copy
            // straight to the gather pipe, no transform in between. r27 is set once at function
            // entry (`mr r27,r3`) and never reassigned before this point, so ctx->gpr[27] here
            // should still hold that same "this" pointer, and *(r27+8) should equal the emitted
            // color word exactly. Reading both directly to find out what object holds this value
            // and confirm the emission site adds no corruption of its own.
            const uint32_t r27 = GetPersistentCpuContext().gpr[27];
            uint32_t colorAtR27Plus8 = 0;
            bool colorReadOk = false;
            if (r27 != 0) {
                try {
                    colorAtR27Plus8 = Memory::Read32(r27 + 8u);
                    colorReadOk = true;
                } catch (const Memory::AccessViolation&) {
                    colorReadOk = false;
                }
            }
            // A second caller (LR=0x802B6DF8, disassembled offline) uses a DIFFERENT calling
            // convention: r31 itself is the color pointer directly (not object+8), passed in from
            // yet another function via 0x802dd064's return values, and is null-checked before use
            // (`cmpwi r31,0; ...; lwz r0,0(r31); stw r0,gatherpipe`) - meaning a null r31 would
            // skip color emission entirely for that vertex. Reading it too since the r27+8 guess
            // above doesn't apply to this caller.
            const uint32_t r31 = GetPersistentCpuContext().gpr[31];
            uint32_t colorAtR31 = 0;
            bool colorAtR31Ok = false;
            if (r31 != 0) {
                try {
                    colorAtR31 = Memory::Read32(r31);
                    colorAtR31Ok = true;
                } catch (const Memory::AccessViolation&) {
                    colorAtR31Ok = false;
                }
            }
            // Confirmed: colorAtR31 == 0xFFFFFF00 (alpha=0) is a permanently-fixed, never-varying
            // value at a specific stable address, unlike the (legitimately animating) material
            // color traced earlier. Dumping DiagRecentCalls the first couple of times this exact
            // broken value is seen, to find who last touched this address.
            if (colorAtR31Ok && colorAtR31 == 0xFFFFFF00u) {
                static int brokenDumpCount = 0;
                if (brokenDumpCount < 3) {
                    ++brokenDumpCount;
                    RT_LOGF(RT_TAG_GX, "NSMBW_BROKEN_VERTCOLOR r31=0x%08X colorAtR31=0x%08X\n", r31, colorAtR31);
                    const uint32_t next = DiagRecentCalls::g_next.load(std::memory_order_relaxed);
                    const uint32_t cap = static_cast<uint32_t>(DiagRecentCalls::kCapacity);
                    for (uint32_t i = 0; i < 80; ++i) {
                        const uint32_t slot = (next - 1u - i) % cap;
                        RT_LOGF(RT_TAG_GX, "  [-%u] addr=0x%08X lr=0x%08X\n", i,
                            DiagRecentCalls::g_addrs[slot], DiagRecentCalls::g_lrs[slot]);
                    }
                }
            }
            float posX = 0, posY = 0;
            if (posFmt.type == GX_F32 && (posFmt.cnt == GX_POS_XY || posFmt.cnt == GX_POS_XYZ)) {
                uint32_t xb, yb;
                std::memcpy(&xb, v, 4);
                std::memcpy(&yb, v + 4, 4);
                xb = __builtin_bswap32(xb);
                yb = __builtin_bswap32(yb);
                std::memcpy(&posX, &xb, 4);
                std::memcpy(&posY, &yb, 4);
            }
            RT_LOGF(RT_TAG_GX,
                    "NSMBW_VERT fmt=%u prim=%u vtxCount=%u posDesc=%u nrmDesc=%u clr0Desc=%u tex0Desc=%u "
                    "posCnt=%u posType=%u posFrac=%u clr0Cnt=%u clr0Type=%u tex0Cnt=%u tex0Type=%u tex0Frac=%u "
                    "pos=(%.1f,%.1f) callerLR=0x%08X r27=0x%08X colorAtR27p8_ok=%d colorAtR27p8=0x%08X "
                    "r31=0x%08X colorAtR31_ok=%d colorAtR31=0x%08X "
                    "rawBytes=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                    (unsigned)vtxFmt, (unsigned)prim, (unsigned)vtxCount, (unsigned)g_hleGxState.vtxDesc[9],
                    (unsigned)g_hleGxState.vtxDesc[10], (unsigned)g_hleGxState.vtxDesc[11],
                    (unsigned)g_hleGxState.vtxDesc[13], (unsigned)posFmt.cnt, (unsigned)posFmt.type,
                    (unsigned)posFmt.frac, (unsigned)clr0Fmt.cnt, (unsigned)clr0Fmt.type,
                    (unsigned)texFmt.cnt, (unsigned)texFmt.type, (unsigned)texFmt.frac,
                    posX, posY, lr, r27, colorReadOk ? 1 : 0, colorAtR27Plus8,
                    r31, colorAtR31Ok ? 1 : 0, colorAtR31,
                    v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11],
                    v[12], v[13], v[14], v[15], v[16], v[17], v[18], v[19]);
        }
    }
    // DIAGNOSTIC (temporary): NSMBW_LOG_DRAW_POS decodes every vertex's POS+UV (F32 XY only) for
    // textured quad draws (vtxCount<=8) - all sampled textures are confirmed correct on their own
    // (NSMBW_DUMP_TEXTURES), so this checks whether the quad's own width/height and UV span match
    // the source texture's size, or whether it's being stretched/tiled onto a much larger quad.
    // Remove once resolved.
    if (AURORA_ENV("NSMBW_LOG_DRAW_POS") != nullptr && vtxCount <= 8 &&
        g_hleGxState.vtxDesc[13] != GX_NONE) {
        static int posLogged = 0;
        if (posLogged < 200) {
            const auto& posFmt = g_hleGxState.vtxAttrFmt[vtxFmt][GX_VA_POS];
            const auto& texFmt = g_hleGxState.vtxAttrFmt[vtxFmt][GX_VA_TEX0];
            if (posFmt.type == GX_F32 && posFmt.cnt == GX_POS_XY) {
                ++posLogged;
                uint32_t vertexSize = 0;
                if (TryGetRawDirectFifoVertexSize(vtxFmt, vertexSize) && vertexSize != 0) {
                    char line[512];
                    int off = std::snprintf(line, sizeof(line), "NSMBW_DRAW_POS prim=%u vtxCount=%u vtxSize=%u",
                                             (unsigned)prim, (unsigned)vtxCount, vertexSize);
                    const uint8_t* base = packet + 3;
                    for (uint16_t vi = 0; vi < vtxCount; ++vi) {
                        const uint8_t* vp = base + vi * vertexSize;
                        uint32_t xb, yb;
                        std::memcpy(&xb, vp, 4);
                        std::memcpy(&yb, vp + 4, 4);
                        xb = __builtin_bswap32(xb);
                        yb = __builtin_bswap32(yb);
                        float x, y;
                        std::memcpy(&x, &xb, 4);
                        std::memcpy(&y, &yb, 4);
                        float u = 0.f, v2 = 0.f;
                        if (texFmt.type == GX_F32) {
                            const uint8_t* tp = vp + 8;
                            uint32_t ub, vb;
                            std::memcpy(&ub, tp, 4);
                            std::memcpy(&vb, tp + 4, 4);
                            ub = __builtin_bswap32(ub);
                            vb = __builtin_bswap32(vb);
                            std::memcpy(&u, &ub, 4);
                            std::memcpy(&v2, &vb, 4);
                        }
                        off += std::snprintf(line + off, sizeof(line) - off, " v%u=(xy:%.1f,%.1f uv:%.3f,%.3f)",
                                              vi, x, y, u, v2);
                    }
                    RT_LOGF(RT_TAG_GX, "%s\n", line);
                    // DIAGNOSTIC (temporary): "every screen shows flat color instead of real
                    // texture content" investigation. This is the draw call that's actually live
                    // for NSMBW (confirmed by this exact log tag firing while an equivalent probe
                    // placed in aurora-main's command_processor.cpp raw-FIFO draw handler never
                    // fired at all - NSMBW's vertex submission goes through this HLE immediate-mode
                    // path, not that one). Dumps the TEV color/alpha routing active RIGHT NOW, at
                    // the moment this exact quad is submitted, via the g_nsmbwLiveTev* mirrors
                    // (populated by aurora-main/lib/gx/command_processor.cpp's BP-register decode,
                    // which - unlike vertex submission - genuinely is how NSMBW's GXSetTevColorIn/
                    // AlphaIn/Order calls reach aurora, since those don't have their own HLE
                    // override on this project's addresses). Remove once resolved.
                    if (AURORA_ENV("NSMBW_LOG_DRAW_TEV") != nullptr) {
                        // NSMBW_DRAW_TEVREG: the TEV constant color registers (C0-C3, set via
                        // GXSetTevColor) are separate from per-vertex CLR0 - if a combiner's alpha
                        // input references a TEV reg (not vertex/texture alpha), a zeroed register
                        // here would suppress final alpha independently of vertex color, which the
                        // NSMBW_FORCE_ALPHA_255_V2 test (vertex-color-only) would never touch.
                        // idx 0=TEVPREV(not a real constant reg), 1=GX_TEVREG0 ("tevreg0" in shader),
                        // 2=GX_TEVREG1 ("tevreg1" in shader), 3=GX_TEVREG2. Print all 4 - an earlier
                        // version of this log only printed idx 0/1 and mislabeled idx1 as "reg1",
                        // which is actually tevreg0, not tevreg1.
                        RT_LOGF(RT_TAG_GX,
                            "NSMBW_DRAW_TEVREG draw#%d prev=(%.3f,%.3f,%.3f,%.3f) tevreg0=(%.3f,%.3f,%.3f,%.3f) "
                            "tevreg1=(%.3f,%.3f,%.3f,%.3f) tevreg2=(%.3f,%.3f,%.3f,%.3f)\n",
                            posLogged,
                            g_nsmbwLiveTevRegColorR[0], g_nsmbwLiveTevRegColorG[0], g_nsmbwLiveTevRegColorB[0], g_nsmbwLiveTevRegAlpha[0],
                            g_nsmbwLiveTevRegColorR[1], g_nsmbwLiveTevRegColorG[1], g_nsmbwLiveTevRegColorB[1], g_nsmbwLiveTevRegAlpha[1],
                            g_nsmbwLiveTevRegColorR[2], g_nsmbwLiveTevRegColorG[2], g_nsmbwLiveTevRegColorB[2], g_nsmbwLiveTevRegAlpha[2],
                            g_nsmbwLiveTevRegColorR[3], g_nsmbwLiveTevRegColorG[3], g_nsmbwLiveTevRegColorB[3], g_nsmbwLiveTevRegAlpha[3]);
                        for (uint32_t st = 0; st < g_nsmbwLiveNumTevStages && st < 16; ++st) {
                            RT_LOGF(RT_TAG_GX,
                                "NSMBW_DRAW_TEV draw#%d stage=%u texMap=%u colorIn a=%u b=%u c=%u d=%u "
                                "alphaIn a=%u b=%u c=%u d=%u\n",
                                posLogged, st, g_nsmbwLiveTevTexMap[st],
                                g_nsmbwLiveTevColorA[st], g_nsmbwLiveTevColorB[st],
                                g_nsmbwLiveTevColorC[st], g_nsmbwLiveTevColorD[st],
                                g_nsmbwLiveTevAlphaA[st], g_nsmbwLiveTevAlphaB[st],
                                g_nsmbwLiveTevAlphaC[st], g_nsmbwLiveTevAlphaD[st]);
                            // "Is the big instructional-text/illustration texture ever actually
                            // drawn, or only loaded?" g_boundTexMaps[tid] (gx_internal.h) is the
                            // real, currently-bound GXTexObj info for that slot right now - not a
                            // load-time snapshot, so this identifies the ACTUAL texture (by its
                            // real width/height/format/objAddr) behind whatever this draw's TEV
                            // stage samples, directly comparable against the confirmed-correct
                            // NSMBW_DUMP_TEXTURES dumps (e.g. 608x112 fmt5 = the strap
                            // instruction text, 256x368 fmt4 = the hand illustration).
                            const uint32_t tm = g_nsmbwLiveTevTexMap[st];
                            if (tm < 8) {
                                const auto& bound = g_boundTexMaps[tm];
                                RT_LOGF(RT_TAG_GX,
                                    "NSMBW_DRAW_BOUND_TEX draw#%d stage=%u tid=%u objAddr=0x%08X "
                                    "%ux%u fmt=%u dataAddr=0x%08X\n",
                                    posLogged, st, tm, bound.objAddr, bound.width, bound.height,
                                    bound.format, bound.dataAddr);
                            }
                        }
                        // NSMBW_DRAW_BLEND: the confirmed-correct draws (real strap-text/hand-
                        // illustration textures, sane TEV routing, correct quad geometry) still
                        // don't show on screen - the remaining untested piece of per-draw state
                        // is the actual blend mode active for THIS draw. type=0(NONE) draws fully
                        // opaque; if it's BLEND with src/dst combined to make the result always
                        // equal the destination (e.g. src=ZERO), the correctly-rendered content
                        // would be composited as fully invisible despite everything upstream of
                        // it being right. g_nsmbwLastBlendDiag (gx_internal.h) is updated on every
                        // real GXSetBlendMode call, so this reads it live, right at the draw.
                        RT_LOGF(RT_TAG_GX,
                            "NSMBW_DRAW_BLEND draw#%d type=%u(0=NONE,1=BLEND,2=LOGIC,3=SUBTRACT) "
                            "src=%u dst=%u op=%u setCount=%u\n",
                            posLogged, g_nsmbwLastBlendDiag.type, g_nsmbwLastBlendDiag.src,
                            g_nsmbwLastBlendDiag.dst, g_nsmbwLastBlendDiag.op,
                            g_nsmbwLastBlendDiag.setCount);
                        // NSMBW_DRAW_DEPTH: a GPU-level frame capture proved these draws never
                        // produce pixels - the final image is a flat, uniform clear color with
                        // zero variation. Depth testing is the one piece of state that can discard
                        // every fragment silently (compare_enable=1 with a Z-buffer that never got
                        // cleared to the far plane, or the wrong compare function, would fail every
                        // fragment's depth test regardless of correct TEV/blend/vertex data).
                        RT_LOGF(RT_TAG_GX,
                            "NSMBW_DRAW_DEPTH draw#%d compareEnable=%d func=%u(0=NEVER,1=LESS,2=EQUAL,"
                            "3=LEQUAL,4=GREATER,5=NEQUAL,6=GEQUAL,7=ALWAYS) updateEnable=%d\n",
                            posLogged, g_nsmbwLiveDepthCompare ? 1 : 0, g_nsmbwLiveDepthFunc,
                            g_nsmbwLiveDepthUpdate ? 1 : 0);
                    }
                }
            }
        }
    }
    // TEMPORARY DIAGNOSTIC (v2 - redo of an inconclusive Sep-4 test): forces every direct CLR0/CLR1
    // RGBA8 vertex's alpha byte to 0xFF in the EXACT buffer handed to submit_raw_draw (not a
    // logging-only copy - the prior test's own writeup admits it patched "a local host-side copy
    // ... never touching the guest-memory-adjacent fifoBytes staging array itself", which is
    // consistent with the patch never actually reaching the real draw despite the log claiming it
    // fired). Remove once resolved.
    const uint8_t* vtxData = packet + 3;
    uint32_t vtxDataBytes = packetBytes - 3u;
    std::vector<uint8_t> patchedVtxData;
    // TARGETED SURVEY (temporary): NSMBW_FORCE_WIISTRAP_ALPHA only tests one specific CLR0
    // fingerprint, (255,255,255,0) - and that test turned out inconclusive, because the BOOT
    // scene's own background fades in to solid WHITE (confirmed via GPU_PEEK captures at several
    // frame offsets), so a WHITE quad's alpha can never be visually distinguished from the
    // background regardless of whether it's forced opaque. The actual missing content (text/
    // illustration) must use a non-white, contrasting color to be readable at all - this logs
    // every DISTINCT CLR0 corner color seen during the BOOT scene (deduplicated, first-seen order)
    // so the real candidate (a dark/colored quad, not this white one) can be identified by RGBA
    // instead of guessing. Remove once resolved.
    if (AURORA_ENV("NSMBW_LOG_CLR0_HIST") != nullptr &&
        (g_nsmbwCurrentSceneProfile == 0 || g_nsmbwCurrentSceneProfile == 5)) {
        uint32_t vertexSize = 0;
        if (TryGetRawDirectFifoVertexSize(vtxFmt, vertexSize) && vertexSize != 0 &&
            static_cast<uint64_t>(vertexSize) * vtxCount <= vtxDataBytes) {
            uint32_t clr0Off = 0;
            bool haveClr0 = false;
            uint32_t clr0Size = 0;
            uint32_t posOff = 0;
            bool havePos = false;
            uint32_t running = 0;
            for (int attr = 0; attr < 26; ++attr) {
                const GXAttrType type = g_hleGxState.vtxDesc[attr];
                if (type == GX_NONE) continue;
                const uint32_t attrBytes =
                    GetDirectAttrByteSizeForFifo(static_cast<GXAttr>(attr), g_hleGxState.vtxAttrFmt[vtxFmt][attr]);
                if (attr == GX_VA_CLR0) {
                    haveClr0 = true;
                    clr0Off = running;
                    clr0Size = attrBytes;
                }
                if (attr == GX_VA_POS) {
                    havePos = true;
                    posOff = running;
                }
                running += attrBytes;
            }
            const auto& posFmt = g_hleGxState.vtxAttrFmt[vtxFmt][GX_VA_POS];
            if (haveClr0 && clr0Size == 4) {
                static uint32_t seen[64] = {0};
                static int seenCount = 0;
                // Reset the seen-cache on every scene change - it filled up during scene 0
                // (WiiStrap) the first time this was used, and with no reset a later scene (e.g.
                // 5/STAGE) never got a chance to log anything of its own once all 64 slots were
                // already taken.
                static uint32_t lastScene = 0xFFFFFFFFu;
                if (g_nsmbwCurrentSceneProfile != lastScene) {
                    lastScene = g_nsmbwCurrentSceneProfile;
                    seenCount = 0;
                }
                for (uint16_t vi = 0; vi < vtxCount; ++vi) {
                    const uint8_t* cp = vtxData + vi * vertexSize + clr0Off;
                    const uint32_t word = (uint32_t(cp[0]) << 24) | (uint32_t(cp[1]) << 16) |
                                          (uint32_t(cp[2]) << 8) | uint32_t(cp[3]);
                    bool isNew = true;
                    for (int i = 0; i < seenCount; ++i) {
                        if (seen[i] == word) { isNew = false; break; }
                    }
                    if (isNew && seenCount < 64) {
                        seen[seenCount++] = word;
                        float x = 0.f, y = 0.f, z = 0.f;
                        bool posOk = false;
                        if (havePos && posFmt.type == GX_F32) {
                            const uint8_t* pp = vtxData + vi * vertexSize + posOff;
                            uint32_t xb, yb, zb = 0;
                            std::memcpy(&xb, pp, 4);
                            std::memcpy(&yb, pp + 4, 4);
                            xb = __builtin_bswap32(xb);
                            yb = __builtin_bswap32(yb);
                            std::memcpy(&x, &xb, 4);
                            std::memcpy(&y, &yb, 4);
                            if (posFmt.cnt == GX_POS_XYZ) {
                                std::memcpy(&zb, pp + 8, 4);
                                zb = __builtin_bswap32(zb);
                                std::memcpy(&z, &zb, 4);
                            }
                            posOk = true;
                        }
                        RT_LOGF(RT_TAG_GX,
                                "NSMBW_CLR0_HIST new rgba=(%u,%u,%u,%u) vtxFmt=%u posType=%u posOk=%d pos=(%.1f,%.1f,%.1f) callerLR=0x%08X\n",
                                cp[0], cp[1], cp[2], cp[3], (unsigned)vtxFmt, (unsigned)posFmt.type, posOk ? 1 : 0,
                                x, y, z, GetPersistentCpuContext().lr);
                    }
                }
            }
        }
    }
    if (AURORA_ENV("NSMBW_FORCE_ALPHA_255_V2") != nullptr) {
        uint32_t vertexSize = 0;
        if (TryGetRawDirectFifoVertexSize(vtxFmt, vertexSize) && vertexSize != 0 &&
            static_cast<uint64_t>(vertexSize) * vtxCount <= vtxDataBytes) {
            uint32_t clr0Off = 0;
            bool haveClr0 = false;
            uint32_t clr0Size = 0;
            uint32_t running = 0;
            for (int attr = 0; attr < 26; ++attr) {
                const GXAttrType type = g_hleGxState.vtxDesc[attr];
                if (type == GX_NONE) continue;
                const uint32_t attrBytes =
                    GetDirectAttrByteSizeForFifo(static_cast<GXAttr>(attr), g_hleGxState.vtxAttrFmt[vtxFmt][attr]);
                if (attr == GX_VA_CLR0) {
                    haveClr0 = true;
                    clr0Off = running;
                    clr0Size = attrBytes;
                }
                running += attrBytes;
            }
            if (haveClr0 && clr0Size == 4) {
                patchedVtxData.assign(vtxData, vtxData + vtxDataBytes);
                uint32_t patched = 0;
                for (uint16_t vi = 0; vi < vtxCount; ++vi) {
                    patchedVtxData[vi * vertexSize + clr0Off + 3] = 0xFF;
                    ++patched;
                }
                vtxData = patchedVtxData.data();
                static int logged = 0;
                if (logged < 10) {
                    ++logged;
                    RT_LOGF(RT_TAG_GX, "NSMBW_FORCE_ALPHA_V2 draw patched=%u/%u vtxCount=%u vtxFmt=%u vertexSize=%u clr0Off=%u\n",
                            patched, (unsigned)vtxCount, (unsigned)vtxCount, (unsigned)vtxFmt, vertexSize, clr0Off);
                }
            }
        }
    }
    // TARGETED EXPERIMENT (temporary): unlike NSMBW_FORCE_ALPHA_255_V2 above (which blanket-forces
    // alpha=0xFF for every CLR0 vertex, and is known to break correct fade-out end-states / other
    // legitimate alpha=0 content elsewhere - an earlier, cruder version of this same idea), this
    // patches alpha ONLY for vertices matching the EXACT broken-pane fingerprint (RGB=(255,255,255),
    // A=0) that nsmbw_wiistrap_execute_diag.cpp / nsmbw_wiistrap_draw_diag.cpp's watch-address
    // tracing confirmed for the stuck WiiStrap pane, AND only while the BOOT/WiiStrap scene is
    // active (g_nsmbwCurrentSceneProfile==0) - so it can't touch some other scene's vertices even if
    // they happen to hit the same exact byte pattern for a legitimate reason. Purely a diagnostic to
    // see whether forcing alpha for just this narrow pattern reveals the WiiStrap boot text/
    // illustration; not a proposed fix either way. Remove once resolved.
    if (AURORA_ENV("NSMBW_FORCE_WIISTRAP_ALPHA") != nullptr && g_nsmbwCurrentSceneProfile == 0) {
        uint32_t vertexSize = 0;
        if (TryGetRawDirectFifoVertexSize(vtxFmt, vertexSize) && vertexSize != 0 &&
            static_cast<uint64_t>(vertexSize) * vtxCount <= vtxDataBytes) {
            uint32_t clr0Off = 0;
            bool haveClr0 = false;
            uint32_t clr0Size = 0;
            uint32_t running = 0;
            for (int attr = 0; attr < 26; ++attr) {
                const GXAttrType type = g_hleGxState.vtxDesc[attr];
                if (type == GX_NONE) continue;
                const uint32_t attrBytes =
                    GetDirectAttrByteSizeForFifo(static_cast<GXAttr>(attr), g_hleGxState.vtxAttrFmt[vtxFmt][attr]);
                if (attr == GX_VA_CLR0) {
                    haveClr0 = true;
                    clr0Off = running;
                    clr0Size = attrBytes;
                }
                running += attrBytes;
            }
            if (haveClr0 && clr0Size == 4) {
                uint32_t matched = 0;
                for (uint16_t vi = 0; vi < vtxCount; ++vi) {
                    const uint8_t* cp = vtxData + vi * vertexSize + clr0Off;
                    if (cp[0] == 0xFF && cp[1] == 0xFF && cp[2] == 0xFF && cp[3] == 0x00) {
                        if (patchedVtxData.empty()) {
                            patchedVtxData.assign(vtxData, vtxData + vtxDataBytes);
                            vtxData = patchedVtxData.data();
                        }
                        patchedVtxData[vi * vertexSize + clr0Off + 3] = 0xFF;
                        ++matched;
                    }
                }
                if (matched > 0) {
                    static int logged = 0;
                    if (logged < 30) {
                        ++logged;
                        RT_LOGF(RT_TAG_GX,
                                "NSMBW_FORCE_WIISTRAP_ALPHA draw patched=%u/%u vtxFmt=%u vertexSize=%u clr0Off=%u\n",
                                matched, (unsigned)vtxCount, (unsigned)vtxFmt, vertexSize, clr0Off);
                    }
                }
            }
        }
    }
    if (!aurora::gx::fifo::submit_raw_draw(prim, vtxFmt, vtxData, vtxCount, vtxDataBytes)) {
        return false;
    }
    GXMarkFrameWork();
    return true;
}

static void DecodeColorFromArray(uint32_t addr, GXCompType type, GXCompCnt cnt, GXColor& out) {
    // Gather exactly the bytes the shared decoder consumes. GX_RGBX8 is the one
    // format whose stream footprint (4 bytes) exceeds what is decoded (3), and
    // the pre-dedup code read only those 3 from the guest array - keep it that
    // way so a 3-byte tail at the end of an array cannot start faulting.
    const uint32_t byteCount =
        (type == GX_RGBX8) ? 3u : ColorByteSize(type, cnt);
    uint8_t bytes[4] = {0, 0, 0, 0};
    for (uint32_t i = 0; i < byteCount && i < 4u; ++i) {
        bytes[i] = Memory::Read8(addr + i);
    }
    DecodeColorBytes(bytes, type, cnt, out);
}

void SubmitIndexedAttribute(GXAttr attr, uint32_t index) {
    const auto& fmt = g_hleGxState.vtxAttrFmt[g_hleGxState.currentVtxFmt][attr];
    const auto& arr = g_hleGxState.vtxArray[attr];
    if (arr.base == 0 || arr.stride == 0) {
        float comps[9]{};
        switch (attr) {
        case GX_VA_CLR0:
        case GX_VA_CLR1:
            if (fmt.cnt == GX_CLR_RGB) {
                GXColor3u8(0, 0, 0);
            } else {
                GXColor4u8(0, 0, 0, 0xFF);
            }
            break;
        case GX_VA_POS:
        case GX_VA_NRM:
        case GX_VA_TEX0:
        case GX_VA_TEX1:
        case GX_VA_TEX2:
        case GX_VA_TEX3:
        case GX_VA_TEX4:
        case GX_VA_TEX5:
        case GX_VA_TEX6:
        case GX_VA_TEX7:
            SubmitAttribute(attr, comps, fmt);
            break;
        default:
            break;
        }
        return;
    }
    const uint32_t baseAddr = arr.base + index * arr.stride;
    float comps[9]{};
    u32 rawComps[9]{};
    switch (attr) {
    case GX_VA_POS: {
        const int count = static_cast<int>(AttrCompCount(GX_VA_POS, fmt, 0u));
        const int step = GetCompSizeBytes(fmt.type);
        uint32_t addr = baseAddr;
        for (int i = 0; i < count; ++i, addr += step) {
            comps[i] = ReadArrayComp(addr, fmt.type, fmt.frac);
            rawComps[i] = ReadArrayRawComp(addr, fmt.type);
        }
        SubmitAttribute(attr, comps, fmt, rawComps);
        break;
    }
    case GX_VA_NRM: {
        const int count = static_cast<int>(AttrCompCount(GX_VA_NRM, fmt, 0u));
        const int step = GetCompSizeBytes(fmt.type);
        uint32_t addr = baseAddr;
        for (int i = 0; i < count; ++i, addr += step) {
            comps[i] = ReadArrayComp(addr, fmt.type, fmt.frac);
            rawComps[i] = ReadArrayRawComp(addr, fmt.type);
        }
        SubmitAttribute(attr, comps, fmt, rawComps);
        break;
    }
    case GX_VA_CLR0:
    case GX_VA_CLR1: {
        GXColor color{};
        DecodeColorFromArray(baseAddr, fmt.type, fmt.cnt, color);
        if (fmt.cnt == GX_CLR_RGB) {
            GXColor3u8(color.r, color.g, color.b);
        } else {
            GXColor4u8(color.r, color.g, color.b, color.a);
        }
        break;
    }
    case GX_VA_TEX0: case GX_VA_TEX1: case GX_VA_TEX2: case GX_VA_TEX3:
    case GX_VA_TEX4: case GX_VA_TEX5: case GX_VA_TEX6: case GX_VA_TEX7: {
        // Every GX_VA_TEXn shares one component layout, so the constant here is
        // exact for whichever of them `attr` is.
        const int count = static_cast<int>(AttrCompCount(GX_VA_TEX0, fmt, 0u));
        const int step = GetCompSizeBytes(fmt.type);
        uint32_t addr = baseAddr;
        for (int i = 0; i < count; ++i, addr += step) {
            comps[i] = ReadArrayComp(addr, fmt.type, fmt.frac);
            rawComps[i] = ReadArrayRawComp(addr, fmt.type);
        }
        SubmitAttribute(attr, comps, fmt, rawComps);
        break;
    }
    default:
        break;
    }
}

// Mirrors the raw-integer fast paths in SubmitAttribute below: for those
// (attr, type) pairs the float component buffer is never read, so the direct
// FIFO path can skip building it.
static bool SubmitAttributeReadsFloatComps(GXAttr attr, const VtxAttrFmt& fmt) {
    switch (attr) {
    case GX_VA_POS:
    case GX_VA_NRM:
    case GX_VA_TEX0: case GX_VA_TEX1: case GX_VA_TEX2: case GX_VA_TEX3:
    case GX_VA_TEX4: case GX_VA_TEX5: case GX_VA_TEX6: case GX_VA_TEX7:
        break;
    default:
        return true;
    }
    switch (fmt.type) {
    case GX_U8:
    case GX_S8:
    case GX_U16:
    case GX_S16:
        return false;
    default:
        return true;
    }
}

void SubmitAttribute(GXAttr attr, float* comps, const VtxAttrFmt& fmt, const u32* rawComps) {
    auto r8 = [rawComps](int idx) { return static_cast<u8>(rawComps[idx]); };
    auto rs8 = [rawComps](int idx) { return static_cast<s8>(rawComps[idx]); };
    auto r16 = [rawComps](int idx) { return static_cast<u16>(rawComps[idx]); };
    auto rs16 = [rawComps](int idx) { return static_cast<s16>(rawComps[idx]); };

    switch (attr) {
    case GX_VA_POS:
        if (rawComps) {
            if (fmt.cnt == GX_POS_XY) {
                switch (fmt.type) {
                case GX_U8: GXPosition2u8(r8(0), r8(1)); return;
                case GX_S8: GXPosition2s8(rs8(0), rs8(1)); return;
                case GX_U16: GXPosition2u16(r16(0), r16(1)); return;
                case GX_S16: GXPosition2s16(rs16(0), rs16(1)); return;
                default: break;
                }
            } else {
                switch (fmt.type) {
                case GX_U8: GXPosition3u8(r8(0), r8(1), r8(2)); return;
                case GX_S8: GXPosition3s8(rs8(0), rs8(1), rs8(2)); return;
                case GX_U16: GXPosition3u16(r16(0), r16(1), r16(2)); return;
                case GX_S16: GXPosition3s16(rs16(0), rs16(1), rs16(2)); return;
                default: break;
                }
            }
        }
        if (fmt.cnt == GX_POS_XY) {
            GXPosition2f32(comps[0], comps[1]);
        } else {
            GXPosition3f32(comps[0], comps[1], comps[2]);
        }
        break;
    case GX_VA_NRM:
        if (rawComps) {
            const bool nbt = fmt.cnt == GX_NRM_NBT || fmt.cnt == GX_NRM_NBT3;
            const int groups = nbt ? 3 : 1;
            switch (fmt.type) {
            case GX_U8:
                for (int i = 0; i < groups; ++i) {
                    GXNormal3u8(r8(i * 3), r8(i * 3 + 1), r8(i * 3 + 2));
                }
                return;
            case GX_S8:
                for (int i = 0; i < groups; ++i) {
                    GXNormal3s8(rs8(i * 3), rs8(i * 3 + 1), rs8(i * 3 + 2));
                }
                return;
            case GX_U16:
                for (int i = 0; i < groups; ++i) {
                    GXNormal3u16(r16(i * 3), r16(i * 3 + 1), r16(i * 3 + 2));
                }
                return;
            case GX_S16:
                for (int i = 0; i < groups; ++i) {
                    GXNormal3s16(rs16(i * 3), rs16(i * 3 + 1), rs16(i * 3 + 2));
                }
                return;
            default: break;
            }
        }
        if (fmt.cnt == GX_NRM_NBT || fmt.cnt == GX_NRM_NBT3) {
            for (int i = 0; i < 3; ++i) {
                GXNormal3f32(comps[i * 3], comps[i * 3 + 1], comps[i * 3 + 2]);
            }
        } else {
            GXNormal3f32(comps[0], comps[1], comps[2]);
        }
        break;
    case GX_VA_CLR0:
    case GX_VA_CLR1:
        if (fmt.cnt == GX_CLR_RGB) {
            GXColor3u8(static_cast<u8>(comps[0]), static_cast<u8>(comps[1]),
                       static_cast<u8>(comps[2]));
        } else {
            GXColor4u8(static_cast<u8>(comps[0]), static_cast<u8>(comps[1]),
                       static_cast<u8>(comps[2]), static_cast<u8>(comps[3]));
        }
        break;
    case GX_VA_TEX0: case GX_VA_TEX1: case GX_VA_TEX2: case GX_VA_TEX3:
    case GX_VA_TEX4: case GX_VA_TEX5: case GX_VA_TEX6: case GX_VA_TEX7:
        if (rawComps) {
            if (fmt.cnt == GX_TEX_S) {
                switch (fmt.type) {
                case GX_U8: GXTexCoord1u8(r8(0)); return;
                case GX_S8: GXTexCoord1s8(rs8(0)); return;
                case GX_U16: GXTexCoord1u16(r16(0)); return;
                case GX_S16: GXTexCoord1s16(rs16(0)); return;
                default: break;
                }
            } else {
                switch (fmt.type) {
                case GX_U8: GXTexCoord2u8(r8(0), r8(1)); return;
                case GX_S8: GXTexCoord2s8(rs8(0), rs8(1)); return;
                case GX_U16: GXTexCoord2u16(r16(0), r16(1)); return;
                case GX_S16: GXTexCoord2s16(rs16(0), rs16(1)); return;
                default: break;
                }
            }
        }
        if (fmt.cnt == GX_TEX_S) {
            GXTexCoord1f32(comps[0]);
        } else {
            GXTexCoord2f32(comps[0], comps[1]);
        }
        break;
    default: break;
    }
}

// `val` is a raw big-endian bit pattern: the FIFO stream is type-agnostic, and
// the float entry point converts before it gets here.
void HleFifoWrite(u32 val, uint32_t sizeBytes) {
    const bool recordOnly = IsDisplayListActive();
    if (recordOnly) {
        WriteDisplayListData(val, sizeBytes);
        return;
    }

    auto resetFifoBuffer = [&]() {
        g_hleGxState.fifoReadOffset = 0;
        g_hleGxState.fifoByteCount = 0;
    };

    auto compactFifoBuffer = [&]() {
        if (g_hleGxState.fifoByteCount == 0) {
            g_hleGxState.fifoReadOffset = 0;
            return;
        }
        if (g_hleGxState.fifoReadOffset == 0) {
            return;
        }
        std::memmove(g_hleGxState.fifoBytes.data(),
                     g_hleGxState.fifoBytes.data() + g_hleGxState.fifoReadOffset,
                     g_hleGxState.fifoByteCount);
        g_hleGxState.fifoReadOffset = 0;
    };

    auto fifoData = [&]() -> uint8_t* {
        return g_hleGxState.fifoBytes.data() + g_hleGxState.fifoReadOffset;
    };

    auto pushBytes = [&](u32 value, uint32_t count) {
        if (count == 0) return;
        if (count > 4) count = 4;
        if (g_hleGxState.fifoReadOffset + g_hleGxState.fifoByteCount + count > g_hleGxState.fifoBytes.size()) {
            compactFifoBuffer();
            if (g_hleGxState.fifoByteCount + count > g_hleGxState.fifoBytes.size()) {
                // Grow rather than drop. This used to resetFifoBuffer(), which discarded a
                // partially received draw packet whenever one exceeded the buffer; the rest of
                // that draw's vertex floats then arrived into an empty queue and were decoded as
                // opcodes (NSMBW intro cutscene, item rain: "GX guest pointer: memory error at
                // 0x58000004", "XF: PosMtx sub-copy unsupported: offs=0, len=1", then a crash).
                const size_t needed = g_hleGxState.fifoByteCount + count;
                size_t newSize = g_hleGxState.fifoBytes.size() * 2u;
                while (newSize < needed) newSize *= 2u;
                g_hleGxState.fifoBytes.resize(newSize);
                static int grownLogs = 0;
                if (grownLogs < 8) {
                    ++grownLogs;
                    RT_LOGF(RT_TAG_GX, "HleFifoWrite: staging buffer grown to %zu bytes (%zu buffered)\n",
                            newSize, g_hleGxState.fifoByteCount);
                }
            }
        }
        const size_t writeOffset = g_hleGxState.fifoReadOffset + g_hleGxState.fifoByteCount;
        switch (count) {
        case 4:
            BigEndian::Write32(g_hleGxState.fifoBytes.data() + writeOffset, value);
            break;
        case 2:
            BigEndian::Write16(g_hleGxState.fifoBytes.data() + writeOffset,
                               static_cast<uint16_t>(value));
            break;
        default:
            g_hleGxState.fifoBytes[writeOffset] = static_cast<uint8_t>(value & 0xFF);
            break;
        }
        g_hleGxState.fifoByteCount += count;
    };

    auto consumeBytes = [&](uint32_t count, u32& out) -> bool {
        if (count == 0 || g_hleGxState.fifoByteCount < count) return false;
        u32 value = 0;
        const uint32_t foldCount = (count > 4) ? 4 : count;
        uint8_t* data = fifoData();
        for (uint32_t i = 0; i < foldCount; ++i) {
            value = (value << 8) | data[i];
        }
        g_hleGxState.fifoReadOffset += count;
        g_hleGxState.fifoByteCount -= count;
        if (g_hleGxState.fifoByteCount == 0) {
            g_hleGxState.fifoReadOffset = 0;
        }
        out = value;
        return true;
    };

    pushBytes(val, sizeBytes == 0 ? 4 : sizeBytes);
    if (FifoDesyncEnabled()) FifoDesyncHistPush(val, sizeBytes == 0 ? 4 : sizeBytes);

    // Parse raw FIFO command packets written directly to the gather pipe
    // (e.g. NW4R/G3D paths that do not call the GXBegin wrapper function).
    while (!g_hleGxState.inBegin) {
        if (g_hleGxState.fifoByteCount < 1) {
            break;
        }

        uint8_t* data = fifoData();
        const uint8_t cmd = data[0];
        const uint8_t opcode = cmd & GX_OPCODE_MASK_CMD;
        u32 sink = 0;

        if (cmd == GX_NOP_CMD || opcode == GX_CMD_INVL_VC_CMD) {
            if (!consumeBytes(1, sink)) break;
            continue;
        }

        if (cmd == GX_LOAD_BP_REG_CMD) {
            if (g_hleGxState.fifoByteCount < 5) break;
            const uint32_t bpWord = ReadBE32(data + 1);
            GXApplyBPReg(static_cast<uint8_t>(bpWord >> 24), bpWord & 0x00FFFFFFu);
            if (FifoDesyncEnabled()) FifoDesyncRecordDraw(GX_LOAD_BP_REG_CMD, static_cast<uint16_t>(bpWord >> 24), bpWord & 0x00FFFFFFu, false);
            if (!consumeBytes(5, sink)) break;
            continue;
        }

        if (opcode == GX_LOAD_CP_REG_CMD) {
            if (g_hleGxState.fifoByteCount < 6) break;
            const uint8_t reg = data[1];
            const uint32_t cpValue = ReadBE32(data + 2);
            GxCpDecode::ApplyCpRegWrite(reg, cpValue);
            if (FifoDesyncEnabled()) FifoDesyncRecordDraw(GX_LOAD_CP_REG_CMD, reg, cpValue, false); // pseudo-record: CP write
            // VCD (0x50/0x60) and VAT (0x70-0x97) only reached the HLE's own shadow state.
            // Aurora needs them too or it cannot size the vertices of the draws that follow.
            if (reg == 0x50u || reg == 0x60u) {
                GxSyncVtxDescToAurora();
            } else if (reg >= 0x70u && reg <= 0x97u) {
                GxSyncVtxAttrFmtToAurora((reg - 0x70u) & 0x07u);
            }
            if (!consumeBytes(6, sink)) break;
            continue;
        }

        if (opcode == GX_LOAD_XF_REG_CMD) {
            if (g_hleGxState.fifoByteCount < 5) break;
            const uint16_t countWords = ReadBE16(data + 1);
            const uint32_t packetBytes = 1u + 4u + (static_cast<uint32_t>(countWords) + 1u) * 4u;
            if (g_hleGxState.fifoByteCount < packetBytes) break;
            // DIAGNOSTIC (temporary): NSMBW_LOG_ZERO_MTX - raw guest XF packets that load an all-zero
            // 3x4 matrix into PNMTX0 (XF addr 0, 12 words), with the guest return address, to find
            // the code writing them straight to the gather pipe. Distinct callers only.
            if (AURORA_ENV("NSMBW_LOG_ZERO_MTX") != nullptr && g_nsmbwCurrentSceneProfile == 5u &&
                ReadBE16(data + 3) < 0x78u) {
                const uint32_t nWords = static_cast<uint32_t>(countWords) + 1u;
                bool allZero = true;
                for (uint32_t w = 0; w < nWords && w < 12u && allZero; ++w) {
                    if (ReadBE32(data + 5 + w * 4) != 0u) allZero = false;
                }
                static uint64_t rawSeen = 0;
                ++rawSeen;
                if (rawSeen > 20000 && allZero) {
                    static uint32_t callers[12] = {};
                    static int nCallers = 0;
                    CpuContext* cc = TryGetCpuContext();
                    const uint32_t lr = cc ? static_cast<uint32_t>(cc->lr) : 0u;
                    const uint32_t pc = cc ? static_cast<uint32_t>(cc->pc) : 0u;
                    bool known = false;
                    for (int i = 0; i < nCallers; ++i) if (callers[i] == lr) { known = true; break; }
                    if (!known && nCallers < 12) {
                        callers[nCallers++] = lr;
                        RT_LOGF(RT_TAG_GX, "NSMBW_ZERO_MTX raw XF all-zero matrix packet: words=%u addr=0x%04X LR=0x%08X PC=0x%08X\n",
                                nWords, ReadBE16(data + 3), lr, pc);
                    }
                }
            }
            if (FifoDesyncEnabled()) FifoDesyncRecordDraw(GX_LOAD_XF_REG_CMD, ReadBE16(data + 3), packetBytes, false); // pseudo-record: XF load
            g_nsmbwXfLoadSource = 4u;
            GXCallDisplayList(data, packetBytes);
            g_nsmbwXfLoadSource = 0u;
            GXMarkFrameWork();
            if (!consumeBytes(packetBytes, sink)) break;
            continue;
        }

        if (opcode >= GX_LOAD_INDX_A_CMD && opcode <= GX_LOAD_INDX_D_CMD) {
            if (g_hleGxState.fifoByteCount < 5) break;
            const uint32_t xfValue = ReadBE32(data + 1);
            ApplyIndexedXfArrayForPacket(cmd, xfValue);
            GXCallDisplayList(data, 5);
            GXMarkFrameWork();
            if (!consumeBytes(5, sink)) break;
            continue;
        }

        if (opcode == GX_CMD_CALL_DL_CMD) {
            if (g_hleGxState.fifoByteCount < 9) break;
            const uint32_t listAddr = ReadBE32(data + 1);
            const uint32_t listSize = ReadBE32(data + 5);
            if (!consumeBytes(9, sink)) break;
            if (listAddr != 0 && listSize > 0) {
                GX__CallDisplayList_80172f64(listAddr, listSize);
            }
            continue;
        }

        if (IsDrawOpcode(opcode)) {
            if (g_hleGxState.fifoByteCount < 3) break;
            const uint16_t vtxCount = ReadBE16(data + 1);
            const GXVtxFmt vtxFmt = static_cast<GXVtxFmt>(cmd & GX_VAT_MASK_CMD);
            const GXPrimitive prim = OpcodeToGXPrimitive(cmd);
            uint32_t rawVertexSize = 0;
            if (TryGetRawDirectFifoVertexSize(vtxFmt, rawVertexSize)) {
                const uint32_t packetBytes = 3u + static_cast<uint32_t>(vtxCount) * rawVertexSize;
                if (g_hleGxState.fifoByteCount < packetBytes) {
                    break;
                }
                if (FifoDesyncEnabled()) FifoDesyncRecordDraw(cmd, vtxCount, rawVertexSize, true);
                // DIAGNOSTIC (temporary): NSMBW_LOG_TEXFMT[=<scene>] - interleaves each raw draw with
                // the GXLoadTexObj lines gx_texture.cpp prints under the same variable, so the guest's
                // real load->draw order is visible (is the texture bound before or after the draw?).
                {
                    static const char* s_env = AURORA_ENV("NSMBW_LOG_TEXFMT");
                    static const long s_scene = s_env && *s_env ? std::strtol(s_env, nullptr, 10) : -1L;
                    static int s_logged = 0;
                    if (s_env && (s_scene < 0 || g_nsmbwCurrentSceneProfile == static_cast<uint32_t>(s_scene)) && s_logged < 400) {
                        ++s_logged;
                        float x0 = 0.f, y0 = 0.f;
                        if (rawVertexSize >= 8) {
                            const uint32_t xb = ReadBE32(data + 3), yb = ReadBE32(data + 7);
                            std::memcpy(&x0, &xb, 4); std::memcpy(&y0, &yb, 4);
                        }
                        uint32_t lr = 0;
                        if (CpuContext* cc = TryGetCpuContext()) lr = static_cast<uint32_t>(cc->lr);
                        RT_LOGF(RT_TAG_GX, "NSMBW_RAWDRAW scene=%u prim=0x%02X n=%u vtxBytes=%u v0=(%.1f,%.1f) LR=0x%08X\n",
                                g_nsmbwCurrentSceneProfile, cmd & GX_OPCODE_MASK_CMD, vtxCount, rawVertexSize, x0, y0, lr);
                    }
                }
                if (TrySubmitRawDirectFifoDraw(data, packetBytes, prim, vtxFmt, vtxCount)) {
                    if (!consumeBytes(packetBytes, sink)) break;
                    continue;
                }
            }
            if (FifoDesyncEnabled()) FifoDesyncRecordDraw(cmd, vtxCount, rawVertexSize, false);
            if (!consumeBytes(3, sink)) break;

            // A zero-vertex primitive carries no vertex data, so there is nothing to stream and
            // nothing that would ever clear inBegin: finishVertexIfNeeded only leaves begin mode
            // from inside `if (vertsRemaining > 0)`. Entering it here wedged the decoder
            // permanently - and because this whole command loop runs `while (!inBegin)`, every
            // guest FIFO byte after that point was consumed as vertex data instead of commands.
            // Observed live: inBegin still true 24s into the frame loop, stuck on vtxFmt 7,
            // matching the `fmt=7 n=0` draw NSMBW issues during GX bring-up; only 2 draws in an
            // entire run ever reached the rasteriser.
            if (vtxCount == 0) {
                continue;
            }

            g_hleGxState.currentVtxFmt = vtxFmt;
            g_hleGxState.currentPrim = prim;
            g_hleGxState.vertsRemaining = vtxCount;
            g_hleGxState.inBegin = true;
            g_hleGxState.auroraBeginCalled = false;
            g_hleGxState.ResetVertex();

            break;
        }

        // Unknown FIFO command byte outside a begin packet; discard it so stream parsing can recover.
        if (FifoDesyncEnabled()) FifoDesyncDumpOnUnknown(data, g_hleGxState.fifoByteCount);
        if (!consumeBytes(1, sink)) break;
    }

    if (!g_hleGxState.inBegin) {
        return;
    }

    if (!recordOnly && !g_hleGxState.auroraBeginCalled) {
        EnsureAuroraFrameActive();
        ApplyAuroraVtxStateForRawBegin(g_hleGxState.currentVtxFmt);
        EnsureDefaultGxAlphaCompare();
        GXBegin(g_hleGxState.currentPrim, g_hleGxState.currentVtxFmt, static_cast<u16>(g_hleGxState.vertsRemaining));
        g_hleGxState.auroraBeginCalled = true;
        GXMarkFrameWork();
    }

    auto finishVertexIfNeeded = [&](GXAttr prevAttr) {
        g_hleGxState.currentAttr = g_hleGxState.NextEnabledAttr(prevAttr);
        g_hleGxState.currentComp = 0;
        if (g_hleGxState.currentAttr == GX_VA_NULL) {
            g_hleGxState.ResetVertex();
            if (g_hleGxState.vertsRemaining > 0) {
                g_hleGxState.vertsRemaining--;
                if (g_hleGxState.vertsRemaining == 0) {
                    g_hleGxState.inBegin = false;
                    g_hleGxState.auroraBeginCalled = false;
                    if (!recordOnly) {
                        GXEnd();
                    }
                }
            }
        }
    };

    while (true) {
        if (!g_hleGxState.inBegin) {
            break;
        }
        GXAttr attr = g_hleGxState.currentAttr;
        if (attr == GX_VA_NULL) {
            resetFifoBuffer();
            return;
        }

        GXAttrType inputType = g_hleGxState.vtxDesc[attr];
        const VtxAttrFmt& fmt = g_hleGxState.vtxAttrFmt[g_hleGxState.currentVtxFmt][attr];

        if (IsMatrixIndexAttr(attr)) {
            if (g_hleGxState.fifoByteCount < 1) break;
            u32 raw = 0;
            if (!consumeBytes(1, raw)) break;
            if (!recordOnly) {
                GXMatrixIndex1u8(attr, static_cast<u8>(raw));
            }
            finishVertexIfNeeded(attr);
            continue;
        }

        if (inputType == GX_DIRECT) {
            if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
                const uint32_t colorSize = ColorByteSize(fmt.type, fmt.cnt);

                if (g_hleGxState.fifoByteCount < colorSize) break;

                uint8_t colorBytes[4] = {0, 0, 0, 0};
                uint8_t* data = fifoData();
                for (uint32_t i = 0; i < colorSize && i < 4; ++i) {
                    colorBytes[i] = data[i];
                }
                g_hleGxState.fifoReadOffset += colorSize;
                g_hleGxState.fifoByteCount -= colorSize;
                if (g_hleGxState.fifoByteCount == 0) {
                    g_hleGxState.fifoReadOffset = 0;
                }

                GXColor color{};
                DecodeColorBytes(colorBytes, fmt.type, fmt.cnt, color);

                if (!recordOnly) {
                    if (fmt.cnt == GX_CLR_RGB) {
                        GXColor3u8(color.r, color.g, color.b);
                    } else {
                        GXColor4u8(color.r, color.g, color.b, color.a);
                    }
                }
                finishVertexIfNeeded(attr);
                continue;
            }
            
            const int compSize = GetCompSizeBytes(fmt.type);
            if (compSize <= 0 || g_hleGxState.fifoByteCount < static_cast<size_t>(compSize)) break;
            u32 raw = 0;
            if (!consumeBytes(static_cast<uint32_t>(compSize), raw)) break;
            constexpr int kMaxComps = 9;
            if (g_hleGxState.currentComp < kMaxComps) {
                if (SubmitAttributeReadsFloatComps(attr, fmt)) {
                    g_hleGxState.compBuffer[g_hleGxState.currentComp] =
                        ConvertCompToFloat(raw, fmt.type, fmt.frac);
                }
                g_hleGxState.rawCompBuffer[g_hleGxState.currentComp] = raw;
            }
            g_hleGxState.currentComp++;
            const int expected = g_hleGxState.GetExpectedCompCount(attr, fmt);
            if (g_hleGxState.currentComp >= expected) {
                if (!recordOnly) {
                    SubmitAttribute(attr, g_hleGxState.compBuffer, fmt, g_hleGxState.rawCompBuffer);
                }
                finishVertexIfNeeded(attr);
            }
            continue;
        }

        if (inputType == GX_INDEX8 || inputType == GX_INDEX16) {
            const uint32_t idxSize = (inputType == GX_INDEX8) ? 1u : 2u;
            const uint32_t indexCount = (attr == GX_VA_NRM) ? NormalIndexCount(fmt) : 1u;
            if (g_hleGxState.fifoByteCount < idxSize * indexCount) break;
            u32 raw[3]{};
            for (uint32_t i = 0; i < indexCount; ++i) {
                if (!consumeBytes(idxSize, raw[i])) break;
            }
            if (!recordOnly) {
                if (attr == GX_VA_NRM && indexCount == 3u) {
                    SubmitIndexedNormalNBT3(raw, fmt);
                } else {
                    SubmitIndexedAttribute(attr, raw[0]);
                }
            }
            finishVertexIfNeeded(attr);
            continue;
        }

        g_hleGxState.currentAttr = g_hleGxState.NextEnabledAttr(attr);
        g_hleGxState.currentComp = 0;
        if (g_hleGxState.currentAttr == GX_VA_NULL) {
            g_hleGxState.ResetVertex();
        }
        break;
    }
}

// Must behave exactly like GX_HLE_FIFO_Write32/16/8 run byte by byte. The ring is not
// bulk-appended ahead of a single parse because the parser can re-enter GX HLE (nested display
// lists, XF/draw calls), which would misorder that work relative to the stream.
static void HleFifoWriteBurstChunked(const uint8_t* data, uint32_t sizeBytes) {
    uint32_t offset = 0;
    for (; offset + 4u <= sizeBytes; offset += 4u) {
        HleFifoWrite(ReadBE32(data + offset), 4);
    }
    if (offset + 2u <= sizeBytes) {
        HleFifoWrite(static_cast<u32>(ReadBE16(data + offset)), 2);
        offset += 2u;
    }
    if (offset < sizeBytes) {
        HleFifoWrite(static_cast<u32>(data[offset]), 1);
    }
}

// Applies complete register-load packets straight to the parser's own entry points, skipping the
// serialize/ring/re-parse round trip; returns bytes consumed, caller hands the rest to
// HleFifoWriteBurstChunked. Safe (per gd_fifo_hle.cpp's GdCanApplyDirect) only on a packet
// boundary with nothing buffered, outside a GXBegin packet and outside display-list recording;
// any other opcode or a truncated packet just stops the walk.
static uint32_t ApplyFifoPacketsDirect(const uint8_t* data, uint32_t sizeBytes) {
    uint32_t offset = 0;

    while (offset < sizeBytes) {
        // Re-tested per packet, not once per burst: nothing currently re-enters GX HLE mid-walk,
        // but if it ever does, breaking here just hands the remainder to the ring.
        if (IsDisplayListActive() || g_hleGxState.inBegin || g_hleGxState.fifoByteCount != 0) {
            break;
        }

        const uint8_t* packet = data + offset;
        const uint32_t avail = sizeBytes - offset;
        const uint8_t cmd = packet[0];
        const uint8_t opcode = cmd & GX_OPCODE_MASK_CMD;

        if (cmd == GX_NOP_CMD) {
            offset += 1u;
            continue;
        }

        if (cmd == GX_LOAD_BP_REG_CMD) {
            if (avail < 5u) break;
            const uint32_t bpWord = ReadBE32(packet + 1);
            GXApplyBPReg(static_cast<uint8_t>(bpWord >> 24), bpWord & 0x00FFFFFFu);
            offset += 5u;
            continue;
        }

        if (opcode == GX_LOAD_CP_REG_CMD) {
            if (avail < 6u) break;
            const uint8_t reg = packet[1];
            const uint32_t cpValue = ReadBE32(packet + 2);
            // Same function the parser calls, so the CP registers it does not
            // decode (0x30/0x40 among them) are dropped here identically.
            GxCpDecode::ApplyCpRegWrite(reg, cpValue);
            offset += 6u;
            continue;
        }

        if (opcode == GX_LOAD_XF_REG_CMD) {
            if (avail < 5u) break;
            const uint16_t countWords = ReadBE16(packet + 1);
            const uint32_t packetBytes = 1u + 4u + (static_cast<uint32_t>(countWords) + 1u) * 4u;
            if (avail < packetBytes) break;
            // GXCallDisplayList unconditionally calls aurora::gx::fifo::drain(), which blocks
            // waiting for the frame worker's sealed phase. Every other GX entry point in this
            // file (e.g. TrySubmitRawDirectFifoDraw, HleFifoWrite's own begin-packet handling)
            // calls this guard first to lazily start a frame if the boot harness's initial
            // begin/end pairing left the worker idle with none active; this call site was
            // missing it, so the very first GX_LOAD_XF_REG_CMD packet issued before any guest
            // frame had begun deadlocked here forever - drain() waiting on the worker, the
            // worker waiting on a begin_frame() that no code path was ever going to send.
            EnsureAuroraFrameActive();
            GXCallDisplayList(packet, packetBytes);
            GXMarkFrameWork();
            offset += packetBytes;
            continue;
        }

        break;
    }

    return offset;
}

// Display-list recording just copies bytes into the guest list buffer and advances the shadow
// cursor, so a burst is one logical append as long as it doesn't cross the list end; a burst that
// would wrap falls back to the per-write path instead. Returns false to signal that fallback.
static bool WriteDisplayListBurst(const uint8_t* data, uint32_t sizeBytes) {
    GxDisplayListState& dl = g_dlRecordState;
    if (!dl.active || dl.base == 0 || dl.size == 0 || dl.writePtr == 0) {
        // Same guard WriteDisplayListData applies; let the per-write path
        // reproduce its (silent) drop.
        return false;
    }

    const uint32_t end = dl.base + dl.size;
    if (dl.writePtr > end || (end - dl.writePtr) < sizeBytes) {
        return false;
    }

    // Guest-visible bytes are written through the ordinary store path, so
    // unaligned cursors, MMIO policy and executable-write guards all behave as
    // they do for a single store. The cursor and count advance per element so a
    // failed write in the middle leaves exactly the completed prefix recorded.
    try {
        uint32_t offset = 0;
        for (; offset + 4u <= sizeBytes; offset += 4u) {
            Memory::Write32(dl.writePtr, ReadBE32(data + offset));
            dl.writePtr += 4u;
            dl.count += 4u;
        }
        if (offset + 2u <= sizeBytes) {
            Memory::Write16(dl.writePtr, ReadBE16(data + offset));
            dl.writePtr += 2u;
            dl.count += 2u;
            offset += 2u;
        }
        if (offset < sizeBytes) {
            Memory::Write8(dl.writePtr, data[offset]);
            dl.writePtr += 1u;
            dl.count += 1u;
        }
    } catch (const Memory::AccessViolation&) {
    }
    return true;
}

extern "C" void GX_HLE_FIFO_WriteBurst(const uint8_t* data, uint32_t sizeBytes) {
    if (data == nullptr || sizeBytes == 0) {
        return;
    }

    if (IsDisplayListActive() && WriteDisplayListBurst(data, sizeBytes)) {
        return;
    }

    const uint32_t applied = ApplyFifoPacketsDirect(data, sizeBytes);
    if (applied < sizeBytes) {
        HleFifoWriteBurstChunked(data + applied, sizeBytes - applied);
    }
}
