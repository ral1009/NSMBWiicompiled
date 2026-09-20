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
#include "ppc_runtime.h"
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
    return std::getenv("NSMBW_LOG_VIEWPORT_SCISSOR") != nullptr && g_nsmbwCurrentSceneProfile == 5u;
}
}

extern "C" void GXSetViewport_Fixed_801C9D50(float l, float t, float w, float h, float nz, float fz) {
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
