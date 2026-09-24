// Fix (not just a diagnostic this time): three more GX state-setter overrides that were silently
// dead for NSMBW, found the same way GXSetTevColor was (see nsmbw_tevcolor_diag.cpp) - the
// existing override in runtime/src/hle/gx lives at an address copied from MKW's build of the same
// SDK function, which is not where NSMBW's own compiler happened to place it. Confirmed against
// this project's own projects/nsmbw/function_map.txt (not NSMBW-Maps/address-map.txt, which maps
// address DELTAS between game revisions, not real addresses for this build):
//
//   function          MKW address (dead override)   real NSMBW address (this file)
//   GXSetViewport      0x801733b4 (gx_transform.cpp)  0x801C9D50
//   GXSetScissor       0x80173430 (gx_transform.cpp)  0x801C9DA0
//   GXSetAlphaCompare  0x80172088 (gx_pixel.cpp)      0x801C8800
//
// Each dead override just means the auto-translated code at the REAL address ran instead - a
// faithful translation of Nintendo's original disassembly, which on real hardware only writes raw
// BP/XF FIFO registers (there is no "call aurora's C++ function" concept on real hardware). Aurora
// still has a generic FIFO/BP decode path that can pick some of that up, but the state these three
// functions are also supposed to leave behind for shared runtime code (g_viewportState for
// GX__GetViewportv's frustum-scale bypass; g_alphaCompareValid for
// gx_stream_common.h's EnsureDefaultGxAlphaCompare, which otherwise force-resets alpha-compare to
// GX_ALWAYS on every raw-FIFO/layout draw because it never sees this flag go true) never got set,
// since only the hand-written override bodies (not the generic FIFO decode) touch those globals.
//
// This does NOT edit the MKW-addressed overrides in gx_transform.cpp/gx_pixel.cpp (they're
// correct for MKW's build and harmless-dead for NSMBW's) - it adds new overrides at NSMBW's own
// real addresses instead, following the same reason gx_tev.cpp's override couldn't just be
// readdressed in place: that file is part of nsmbw_runtime_common (shared with MKW), and the NSMBW
// shard generator only excludes addresses it finds under projects/nsmbw/native/ - readdressing the
// shared override would leave 0x801C9D50/0x801C9DA0/0x801C8800 un-excluded, and the translator's
// own auto-generated functions at those addresses would collide with it at link time
// (ld.lld: duplicate symbol). Requires a shard-manifest regen
// (Translator.Cli emit-nsmbw-build-shards) after adding this file, same as nsmbw_tevcolor_diag.cpp.
//
// g_viewportState/g_alphaCompareValid are plain (non-extern-"C") globals defined in
// runtime/src/hle/gx/gx_utils.cpp and declared in that tree's private gx_internal.h, which isn't
// on this target's include path (nsmbw_native is a separate CMake object library from
// nsmbw_runtime_common - see runtime/cmake/NsmbwProduct.cmake). Redeclaring them here with
// matching types is enough to link against the same symbols: the Itanium C++ ABI does not
// name-mangle plain global-namespace data the way it mangles functions, so no extern "C" is
// needed (unlike g_nsmbwCurrentSceneProfile, which nsmbw_create_next_scene_diag.cpp DOES define
// extern "C", so gx_fifo.cpp's own redeclaration matches that instead).
#include "hle_stubs.h"
#include <aurora/env.hpp>
#include "ppc_runtime.h"
#include "memory.h"
#include <dolphin/gx/GXTransform.h>
#include <dolphin/gx/GXCull.h>
#include <dolphin/gx/GXTev.h>

#include <cstdio>
#include <cstdlib>

extern float g_viewportState[6];
extern bool g_alphaCompareValid;
extern "C" uint32_t g_nsmbwCurrentSceneProfile;

// TEMPORARY (title-screen investigation): the STAGE(5)/STAGE_TITLE render comes out as a narrow,
// static, ~134px-wide vertical bar centered in the 640x480 frame instead of the level+Mario -
// exactly the shape a too-narrow GXSetViewport/GXSetScissor width would produce. Logging every
// real call during that scene to see the actual width/height values the game is submitting,
// gated on NSMBW_LOG_VIEWPORT_SCISSOR so this stays silent otherwise.
namespace {
bool ShouldLog() {
    return AURORA_ENV("NSMBW_LOG_VIEWPORT_SCISSOR") != nullptr && g_nsmbwCurrentSceneProfile == 5u;
}

// Guest-side bookkeeping the SDK bodies leave in __GXData (bug class 3 in CLAUDE.md: an override
// that replaces an SDK function also replaces its stores to guest globals, and other still-
// translated SDK code reads them). Found 2026-09-20 from the level scene: NSMBW_LOG_DRAW_TEXGEN
// showed every tile/model draw running with vp=(0,0 0x0) scissor=(-342,-342 1x1), i.e. raw-zero
// XF viewport and BP scissor registers, while HUD draws that set their viewport directly were
// fine. The zeros come from two translated consumers of fields these overrides never wrote:
//   - GXGetViewportv (0x801C9D80) reads __gx+0x544..0x558 (vp l,t,w,h,near,far), and
//     __GXSetViewport (0x801C9C80, run from __GXSetDirtyState 0x801C5430 whenever dirty bit
//     0x10000000 is set) re-emits XF 0x101A..0x101F from the same floats;
//   - GXGetScissor (0x801C9E10, inlined into d2d::Multi_c::draw 0x80007010,
//     LytBase_c::SetScissorMask 0x800C9770 and the save/restore helper 0x8008A230) decodes
//     __gx+0x148/0x14C (suScis0/1, BP 0x20/0x21).
// __GXData lives at *(0x8042E468) (lwz rX,-0x4EF8(r2) with r2=0x80433360 - see gx_internal.h).
constexpr uint32_t kGxDataPtrAddr = 0x8042E468u;
constexpr uint32_t kGxVpOff = 0x544u;          // float vpLeft, vpTop, vpWd, vpHt, vpNearz, vpFarz
constexpr uint32_t kGxSuScis0Off = 0x148u;     // BP 0x20 word: (y0+342)<<12 | (x0+342)
constexpr uint32_t kGxSuScis1Off = 0x14Cu;     // BP 0x21 word: (y1+342)<<12 | (x1+342)
constexpr uint32_t kGxBpSentOff = 0x2u;        // u16 bpSentNot, cleared after every BP write

uint32_t GxData() {
    return Memory::Read32(kGxDataPtrAddr);
}
}

extern "C" void GXSetViewport_Fixed_801C9D50(float l, float t, float w, float h, float nz, float fz) {
    if (const uint32_t gd = GxData()) {
        // The six float stores from the translated body at 0x801C9D50 - but NOT its
        // GX_DIRTY_VIEWPORT bit. Setting it made the guest's __GXSetViewport (0x801C9C80) re-emit
        // XF 0x101A..0x101F on the next flush with the SDK's +342 centre offset, while aurora
        // encodes and decodes its own viewport with +340 (GXTransform.cpp / apply_xf_viewport),
        // so every frame alternated between left/top=0 and left/top=2 (NSMBW_LOG_VIEWPORT:
        // ox=660 and ox=662 interleaved). Visible as wrong/black patches on the world map, title
        // and levels (2026-09-20 evening regression). The HLE call below already applied the
        // viewport, so the re-emit adds nothing; GXGetViewportv still sees the right floats.
        Memory::WriteFloat32(gd + kGxVpOff + 0u, l);
        Memory::WriteFloat32(gd + kGxVpOff + 4u, t);
        Memory::WriteFloat32(gd + kGxVpOff + 8u, w);
        Memory::WriteFloat32(gd + kGxVpOff + 12u, h);
        Memory::WriteFloat32(gd + kGxVpOff + 16u, nz);
        Memory::WriteFloat32(gd + kGxVpOff + 20u, fz);
    }
    g_viewportState[0] = l;
    g_viewportState[1] = t;
    g_viewportState[2] = w;
    g_viewportState[3] = h;
    g_viewportState[4] = nz;
    g_viewportState[5] = fz;
    if (ShouldLog()) {
        static int logged = 0;
        if (logged < 40) {
            ++logged;
            std::fprintf(stderr, "[nsmbw][diag] GXSetViewport left=%.2f top=%.2f w=%.2f h=%.2f near=%.4f far=%.4f\n",
                l, t, w, h, nz, fz);
            std::fflush(stderr);
        }
    }
    GXSetViewport(l, t, w, h, nz, fz);
}
PPC_NATIVE_OVERRIDE_VOID(801C9D50, GXSetViewport_Fixed_801C9D50,
                         (float l, float t, float w, float h, float nz, float fz), (l, t, w, h, nz, fz));

extern "C" void GXSetScissor_Fixed_801C9DA0(uint32_t l, uint32_t t, uint32_t w, uint32_t h) {
    if (const uint32_t gd = GxData()) {
        // Same encoding as the translated body at 0x801C9DA0 (and GXGetScissor's decode).
        const uint32_t x0 = l + 342u, y0 = t + 342u;
        const uint32_t x1 = x0 + w - 1u, y1 = y0 + h - 1u;
        uint32_t s0 = Memory::Read32(gd + kGxSuScis0Off);
        s0 = (s0 & ~0x7FFu) | (y0 & 0x7FFu);
        s0 = (s0 & ~0x7FF000u) | ((x0 << 12) & 0x7FF000u);
        uint32_t s1 = Memory::Read32(gd + kGxSuScis1Off);
        s1 = (s1 & ~0x7FFu) | (y1 & 0x7FFu);
        s1 = (s1 & ~0x7FF000u) | ((x1 << 12) & 0x7FF000u);
        Memory::Write32(gd + kGxSuScis0Off, s0);
        Memory::Write32(gd + kGxSuScis1Off, s1);
        Memory::Write16(gd + kGxBpSentOff, 0);
    }
    if (ShouldLog()) {
        static int logged = 0;
        if (logged < 40) {
            ++logged;
            std::fprintf(stderr, "[nsmbw][diag] GXSetScissor left=%u top=%u w=%u h=%u\n", l, t, w, h);
            std::fflush(stderr);
        }
    }
    GXSetScissor(l, t, w, h);
}
PPC_NATIVE_OVERRIDE_VOID(801C9DA0, GXSetScissor_Fixed_801C9DA0, (uint32_t l, uint32_t t, uint32_t w, uint32_t h),
                         (l, t, w, h));

extern "C" void GXSetAlphaCompare_Fixed_801C8800(uint32_t c0, uint32_t r0, uint32_t op, uint32_t c1, uint32_t r1) {
    g_alphaCompareValid = true;
    GXSetAlphaCompare(static_cast<GXCompare>(c0), static_cast<u8>(r0), static_cast<GXAlphaOp>(op),
                       static_cast<GXCompare>(c1), static_cast<u8>(r1));
}
PPC_NATIVE_OVERRIDE_VOID(801C8800, GXSetAlphaCompare_Fixed_801C8800,
                         (uint32_t c0, uint32_t r0, uint32_t op, uint32_t c1, uint32_t r1), (c0, r0, op, c1, r1));
