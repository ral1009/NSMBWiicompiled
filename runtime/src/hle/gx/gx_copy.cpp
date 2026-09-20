// gx_copy.cpp - Framebuffer Copy Operations
#include "gx_internal.h"

#include "settings_overlay.h"

#include <dolphin/gx/GXAurora.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <vector>

namespace {
// Copy destinations stay GPU-only until an explicit/lazy readback. Track the
// guest ranges that own those results so a later data-cache flush over a reused
// allocation can retire the stale GPU texture before it is considered by a
// subsequent GXLoadTexObj. There is at most one live range per destination;
// rewriting the destination replaces its previous extent.
std::mutex g_efbCopyDestinationsMutex;
std::map<uint32_t, uint32_t> g_efbCopyDestinations;
uint32_t g_largestEfbCopyDestination = 0;

void RememberEfbCopyDestination(uint32_t addr, uint32_t size) {
    if (size == 0) {
        return;
    }
    std::lock_guard<std::mutex> guard(g_efbCopyDestinationsMutex);
    g_efbCopyDestinations[CanonicalizeGxMainRamAddress(addr)] = size;
    // A conservative monotonic maximum lets invalidation jump directly to
    // the only map interval that could overlap instead of walking every copy
    // destination on every DCStoreRange.
    g_largestEfbCopyDestination = std::max(g_largestEfbCopyDestination, size);
}

} // namespace

void InvalidateEfbCopyDestinationsForRange(uint32_t addr, uint32_t size) {
    if (size == 0) {
        return;
    }

    const uint64_t dirtyStart = CanonicalizeGxMainRamAddress(addr);
    const uint64_t dirtyEnd = dirtyStart + size;
    std::vector<uint32_t> retired;
    {
        std::lock_guard<std::mutex> guard(g_efbCopyDestinationsMutex);
        const uint64_t earliestCandidate =
            dirtyStart > g_largestEfbCopyDestination ? dirtyStart - g_largestEfbCopyDestination : 0;
        for (auto it = g_efbCopyDestinations.lower_bound(static_cast<uint32_t>(earliestCandidate));
             it != g_efbCopyDestinations.end() && static_cast<uint64_t>(it->first) < dirtyEnd;) {
            const uint64_t copyStart = it->first;
            const uint64_t copyEnd = copyStart + it->second;
            if (dirtyEnd <= copyStart || dirtyStart >= copyEnd) {
                ++it;
                continue;
            }
            retired.push_back(it->first);
            it = g_efbCopyDestinations.erase(it);
        }
    }

    // Preserve FIFO ordering: the destroy command is emitted before any later
    // texture load that can consume the freshly flushed RAM bytes.
    for (const uint32_t copyAddr : retired) {
        GXDestroyCopyTex(GuestToHostPtr(copyAddr));
    }
}

// ============================================================================
// Display Copy Source/Destination
// ============================================================================

extern "C" void GX__SetDispCopySrc_8016f438(uint32_t l, uint32_t t, uint32_t w, uint32_t h) {
    GXSetDispCopySrc((u16)l, (u16)t, (u16)w, (u16)h);
}
PPC_NATIVE_OVERRIDE_VOID(8016f438, GX__SetDispCopySrc_8016f438, (uint32_t l, uint32_t t, uint32_t w, uint32_t h), (l, t, w, h));

extern "C" void GX__SetDispCopyDst_8016f4b8(uint32_t w, uint32_t h) { GXSetDispCopyDst((u16)w, (u16)h); }
PPC_NATIVE_OVERRIDE_VOID(8016f4b8, GX__SetDispCopyDst_8016f4b8, (uint32_t w, uint32_t h), (w, h));

// ============================================================================
// Texture Copy Source/Destination
// ============================================================================

extern "C" void GX__SetTexCopySrc_8016f478(uint32_t l, uint32_t t, uint32_t w, uint32_t h) {
    GXSetTexCopySrc((u16)l, (u16)t, (u16)w, (u16)h);
    g_texCopyState.srcLeft=(u16)l; g_texCopyState.srcTop=(u16)t;
    g_texCopyState.srcWidth=(u16)w; g_texCopyState.srcHeight=(u16)h;
}
PPC_NATIVE_OVERRIDE_VOID(8016f478, GX__SetTexCopySrc_8016f478, (uint32_t l, uint32_t t, uint32_t w, uint32_t h), (l, t, w, h));

extern "C" void GX__SetTexCopyDst_8016f4dc(uint32_t w, uint32_t h, uint32_t f, uint32_t m) {
    GXSetTexCopyDst((u16)w, (u16)h, (GXTexFmt)f, (GXBool)m);
    g_texCopyState.dstWidth=(u16)w; g_texCopyState.dstHeight=(u16)h;
    g_texCopyState.dstFormat=f; g_texCopyState.dstMipmap=m;
}
PPC_NATIVE_OVERRIDE_VOID(8016f4dc, GX__SetTexCopyDst_8016f4dc, (uint32_t w, uint32_t h, uint32_t f, uint32_t m), (w, h, f, m));

extern "C" void GX__SetCopyFilter_8016fa40(uint32_t aa, uint32_t spa, uint32_t vf, uint32_t vfa) {
    uint8_t sp[12][2]={}, vfb[7]={};
    if(spa) std::memcpy(sp, GuestToHostPtr(spa, 24), 24);
    if(vfa) std::memcpy(vfb, GuestToHostPtr(vfa, 7), 7);
    GXSetCopyFilter((GXBool)aa, sp, (GXBool)vf, vfb);
}
PPC_NATIVE_OVERRIDE_VOID(8016fa40, GX__SetCopyFilter_8016fa40, (uint32_t aa, uint32_t spa, uint32_t vf, uint32_t vfa), (aa, spa, vf, vfa));

extern "C" void GX__SetDispCopyGamma_8016fc24(uint32_t g) { GXSetDispCopyGamma((GXGamma)g); }
PPC_NATIVE_OVERRIDE_VOID(8016fc24, GX__SetDispCopyGamma_8016fc24, (uint32_t g), (g));

// ============================================================================
// Minimal pipeline bisection: NSMBW_TEST_TRIANGLE
// ============================================================================
//
// Every investigation so far has started from a guest draw and asked "why is THIS one wrong",
// which can't distinguish "the guest set up bad state" from "the host pipeline can't draw at all".
// This submits one hand-written, solid-colored triangle through the exact same aurora GX backend
// the guest uses - no textures, no lighting, no depth, no blending, identity model matrix, a
// known orthographic projection in raw pixel coordinates, and a known viewport/scissor. It runs
// at end-of-frame, immediately before the EFB->XFB resolve, so it composites on top of whatever
// the guest already drew and cannot be hidden by draw order.
//
// Reading the result:
//   magenta triangle visible  -> GX vertex submission, aurora decode, host draw, and the
//                                framebuffer resolve all work; the bug is in guest-side state.
//   nothing visible           -> the break is in the host pipeline itself, and the trace narrows
//                                to GX vertex submission -> aurora decode -> host draw -> resolve.
//
// Deliberately sets every piece of pixel state it depends on rather than inheriting whatever the
// guest left behind, so a stale cull mode / z-func / alpha-compare from an earlier draw cannot
// silently discard it and produce a false negative.
// Set by nsmbw_create_next_scene_diag.cpp on every successful scene transition; lets the periodic
// log below say which scene was active when the triangle was (or was not) submitted.
extern "C" uint32_t g_nsmbwCurrentSceneProfile;

namespace {
void NsmbwDrawTestTriangle() {
    static const bool s_enabled = std::getenv("NSMBW_TEST_TRIANGLE") != nullptr;
    if (!s_enabled) {
        return;
    }

    // Feature step 1 (NSMBW_TEST_TRIANGLE_Z): the plain triangle renders; every guest screen that
    // renders correctly is 2D layout drawn with depth testing OFF, and the one that comes out black
    // (the 3D STAGE scene) draws WITH it. Enabling a real depth test here - the way the 3D scene
    // does - isolates whether the depth buffer's contents (its clear value) are what's rejecting
    // every z-tested draw. The triangle is placed at mid-depth so it must pass against a buffer
    // cleared to "far" and must fail against one cleared to "near".
    static const bool s_zTest = std::getenv("NSMBW_TEST_TRIANGLE_Z") != nullptr;
    const float triZ = s_zTest ? -0.5f : 0.0f;

    // Raster state: nothing may discard this triangle.
    GXSetCullMode(GX_CULL_NONE);                                  // winding cannot matter
    if (s_zTest) {
        GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);                  // depth test on, like the 3D scene
    } else {
        GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);                // no depth test/write
    }
    GXSetZCompLoc(GX_TRUE);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY); // no blending
    GXSetColorUpdate(GX_TRUE);
    GXSetAlphaUpdate(GX_TRUE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);    // no alpha kill
    GXSetDither(GX_FALSE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, GXColor{0, 0, 0, 0});

    // Color comes straight from the vertex, with lighting disabled.
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);                         // output = vertex color, verbatim

    // Known viewport/scissor over the full 640x480 frame. The box offset is reset explicitly: it
    // is separate global state from the scissor rect itself, and a stale non-zero offset left by
    // the guest would shift this scissor off-screen and discard the triangle for a reason that has
    // nothing to do with the pipeline being tested.
    GXSetViewport(0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f);
    GXSetScissorBoxOffset(0, 0);
    GXSetScissor(0, 0, 640, 480);

    // Orthographic projection mapping x:0..640 left-to-right and y:0..480 top-to-bottom, so the
    // vertex coordinates below are literal screen pixels. Row-major 4x4, matching what aurora's
    // GXSetProjection reads for GX_ORTHOGRAPHIC ([0][0], [0][3], [1][1], [1][3], [2][2], [2][3]).
    const float ortho[4][4] = {
        {2.0f / 640.0f, 0.0f, 0.0f, -1.0f},   // x: 0 -> -1, 640 -> +1
        {0.0f, -2.0f / 480.0f, 0.0f, 1.0f},   // y: 0 -> +1 (top), 480 -> -1 (bottom)
        {0.0f, 0.0f, -1.0f, -1.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    };
    GXSetProjection(ortho, GX_ORTHOGRAPHIC);

    // Identity model/view matrix (3x4 row-major).
    const float identity[3][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
    };
    GXLoadPosMtxImm(identity, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);

    // Direct (inline) position + color, nothing indexed or array-sourced.
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);

    // Large, centered, and magenta - a color nothing else on these screens uses, so it cannot be
    // confused with existing content.
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(320.0f, 60.0f, triZ);
    GXColor4u8(255, 0, 255, 255);
    GXPosition3f32(80.0f, 420.0f, triZ);
    GXColor4u8(255, 0, 255, 255);
    GXPosition3f32(560.0f, 420.0f, triZ);
    GXColor4u8(255, 0, 255, 255);
    GXEnd();

    // The triangle is confirmed to render during early scenes and to vanish exactly when the
    // STAGE/WipeCircle "blue rectangle" screen begins. Logging periodically (with the active scene
    // profile) rather than only the first few calls answers the question that actually splits the
    // problem: if these lines keep appearing while nothing is on screen, the triangle is still
    // being SUBMITTED and something after submission is discarding it; if they stop, this present
    // path is no longer running at all for that scene.
    static uint64_t submitCount = 0;
    ++submitCount;
    if (submitCount <= 3 || (submitCount % 60) == 0) {
        RT_LOGF(RT_TAG_GX, "NSMBW_TEST_TRIANGLE submitted #%llu scene=%u\n",
                static_cast<unsigned long long>(submitCount), g_nsmbwCurrentSceneProfile);
    }
}
} // namespace

// ============================================================================
// Copy Execution
// ============================================================================

extern "C" void GX__CopyDisp_8016fc38(uint32_t da, uint32_t c) {
    EnsureAuroraFrameActive();
    // Submitted before the drain/resolve below so it lands in this frame's EFB, on top of
    // everything the guest drew.
    NsmbwDrawTestTriangle();
    // GX copies are FIFO-ordered on hardware. Drain submitted draws before
    // resolving the EFB so high-level copies see the same contents.
    GXDrawDone();
    // Paired with the triangle log above: if the triangle keeps submitting but stops appearing,
    // the next suspect is this resolve - a changed destination address (a different XFB than the
    // one VI actually scans out) would drop an otherwise-correctly-rendered frame on the floor.
    if (std::getenv("NSMBW_TEST_TRIANGLE") != nullptr) {
        static uint64_t copyCount = 0;
        ++copyCount;
        if (copyCount <= 3 || (copyCount % 60) == 0) {
            RT_LOGF(RT_TAG_GX, "NSMBW_TEST_TRIANGLE copydisp #%llu da=0x%08X clear=%u scene=%u\n",
                    static_cast<unsigned long long>(copyCount), da, c, g_nsmbwCurrentSceneProfile);
        }
    }
    GXCopyDisp(GuestToHostPtr(da), (GXBool)c);
    // DIAGNOSTIC (temporary): "every screen shows flat color instead of real texture content"
    // investigation. All the individual draws for this screen were confirmed correct (right
    // textures bound, sane TEV routing, correct quad geometry) - this checks whether the actual
    // resolved pixels landing in the XFB right after this copy already show that content, or are
    // already blank/flat here (before VI/present even runs), which would mean the bug is in this
    // copy or its timing rather than anything after it. Dumps a small sample from the middle of
    // the XFB (not corner 0,0, which a border/background element could legitimately leave at a
    // uniform color even in a working frame). Remove once resolved.
    if (std::getenv("NSMBW_LOG_XFB_SAMPLE") != nullptr) {
        static int xfbSampleLogged = 0;
        if (xfbSampleLogged < 10) {
            ++xfbSampleLogged;
            // YUV422 (2 bytes/pixel), 640-wide XFB: sample a row near vertical center, starting
            // partway across the row rather than column 0.
            const uint32_t sampleOffset = (240u * 640u + 100u) * 2u;
            const uint8_t* p = static_cast<const uint8_t*>(GuestToHostPtr(da + sampleOffset, 32));
            char hex[3 * 32 + 1] = {};
            char* hp = hex;
            for (uint32_t i = 0; i < 32; ++i) {
                const uint8_t b = p ? p[i] : 0xFF;
                hp += std::snprintf(hp, 4, "%02x ", b);
            }
            RT_LOGF(RT_TAG_GX, "NSMBW_XFB_SAMPLE call#%d da=0x%08X hostPtrNull=%d bytes@row240,col100: %s\n",
                    xfbSampleLogged, da, p == nullptr, hex);
        }
    }
    // No second GXDrawDone here: the frame-worker wait below is for the DONE
    // phase, which strictly subsumes the drain this call would perform.
    ++g_gxFrameCount;
    // g_alphaCompareValid used to never go true at all for NSMBW (the guest's real
    // GXSetAlphaCompare override lived at a dead MKW address - see
    // projects/nsmbw/native/nsmbw_gx_setters_diag.cpp), so EnsureDefaultGxAlphaCompare
    // (gx_stream_common.h) unconditionally forced GX_ALWAYS before every raw-FIFO/layout draw,
    // every frame. Now that the real address is fixed and can set this flag true, it needs a
    // per-frame reset here - otherwise the FIRST real guest GXSetAlphaCompare call of the whole
    // run would latch it true forever, and any later frame whose own layout draws rely on that
    // safe GX_ALWAYS default (because THIS frame's guest code never touches alpha-compare itself)
    // would silently inherit whatever restrictive compare state a much earlier, unrelated draw
    // left behind instead.
    g_alphaCompareValid = false;
    VI_HLE_SetXfbReady(da);
    // Present immediately so post-copy draws don't leak into this frame. Join at the DONE phase
    // (not the cheaper SEALED phase GXDrawDone waits for) because ImGui's draw lists, owned by
    // Aurora's render worker, replay during encode; aurora_end_frame would join here anyway.
    aurora_wait_for_frame_worker();
    settings_overlay::Draw();
    // Seal, pace to the VI retrace boundary (Aurora renders the sealed frame
    // during the wait), and pre-warm the next frame.
    VI_HLE_PresentFrame(/*presentedXfb=*/true, /*paceToRetrace=*/true);
}

PPC_NATIVE_OVERRIDE_VOID(8016fc38, GX__CopyDisp_8016fc38, (uint32_t da, uint32_t c), (da, c));


extern "C" void GX__CopyTex_8016fd74(uint32_t da, uint32_t c) {
    EnsureAuroraFrameActive();
    // Match GX FIFO ordering: texture copies observe all prior draws.
    GXDrawDone();
    const uint16_t rawSrcLeft = g_texCopyState.srcLeft;
    const uint16_t rawSrcTop = g_texCopyState.srcTop;
    const uint16_t rawSrcWidth = g_texCopyState.srcWidth;
    const uint16_t rawSrcHeight = g_texCopyState.srcHeight;

    // Keep the source in guest EFB coordinates. Aurora maps it to the scaled
    // EFB exactly once, matching Dolphin's ConvertEFBRectangle path.
    GXSetTexCopySrc(rawSrcLeft, rawSrcTop, rawSrcWidth, rawSrcHeight);
    // EFB copies stay GPU-only except probe-sized ones (e.g. the 4x4 lens-flare depth probe),
    // which Aurora reads back asynchronously via efb_ram::schedule and land in guest RAM a frame
    // later. RISK: copies above the probe threshold, or on the offscreen list, are not
    // auto-downloaded, so guest reads see stale RAM; call aurora_flush_efb_copies_to_ram if a
    // copy needs reading back.
    GXCopyTex(GuestToHostPtr(da), (GXBool)c);
    RememberEfbCopyDestination(
        da, GXGetTexBufferSize(g_texCopyState.dstWidth, g_texCopyState.dstHeight,
                               g_texCopyState.dstFormat, GX_FALSE, 0));
    GXSetTexCopySrc(rawSrcLeft, rawSrcTop, rawSrcWidth, rawSrcHeight);
}
PPC_NATIVE_OVERRIDE_VOID(8016fd74, GX__CopyTex_8016fd74, (uint32_t da, uint32_t c), (da, c));
