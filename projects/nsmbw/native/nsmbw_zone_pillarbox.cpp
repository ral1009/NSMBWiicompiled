// Ultrawide: pillarbox zones narrower than the view (developer's choice 2026-11-19, after repeating,
// mirroring and tile-filling the out-of-zone area all looked wrong in World 3-3's pipe room).
//
// The level has no art past a zone's edges, so in a zone narrower than the 21:9 view the space
// beside it shows bare background. Each frame in a level this works out where the zone's left and
// right edges fall in the 640-unit frame and tells aurora (AuroraSetLevelClipX) to clip level draws
// to that range; layout (HUD) draws are not clipped, so the HUD stays at the screen edges.
//   zone edges: dBg_c floats at m_bg_p (0x8042A0B0) + 0x90000 + 48 / + 52 (the values
//               getLeftLimit 0x80078A70 / getRightLimit 0x80078C10 start from; 3-3's pipe room:
//               2304 / 2880, its zone in Stage/03-03.arc area 3)
//   visible:    dBgParameter_c::ms_Instance_p (0x8042A0E0) mPos.x +0x38, mSize.x +0x40
// Nothing is clipped when the zone is at least as wide as the view, so 16:9 and normal zones are
// untouched. NSMBW_ZONE_PILLARBOX=0 turns it off; NSMBW_LOG_ZONE_PILLARBOX=1 logs the range.
#include "nsmbw_va.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "memory.h"
#include <dolphin/gx/GXAurora.h>

extern "C" uint32_t g_nsmbwCurrentSceneProfile;
extern "C" uint32_t g_nsmbwCurrentViTick;

extern "C" void NsmbwZonePillarboxTick() {
    static const bool off = [] { const char* v = std::getenv("NSMBW_ZONE_PILLARBOX"); return v != nullptr && v[0] == '0'; }();
    static const bool log = std::getenv("NSMBW_LOG_ZONE_PILLARBOX") != nullptr;
    static bool active = false;
    float x0 = 0.0f, x1 = 0.0f;
    if (!off && g_nsmbwCurrentSceneProfile == 5u) {
        const uint32_t bg = MemoryInline::FlatRead32(NSMBW_VA(0x8042A0B0u));
        const uint32_t bgp = MemoryInline::FlatRead32(NSMBW_VA(0x8042A0E0u));
        if (bg != 0 && bgp != 0) {
            const float zl = MemoryInline::FlatReadFloat32(bg + 0x90000u + 48u);
            const float zr = MemoryInline::FlatReadFloat32(bg + 0x90000u + 52u);
            const float cx = MemoryInline::FlatReadFloat32(bgp + 0x38u);
            const float cw = MemoryInline::FlatReadFloat32(bgp + 0x40u);
            if (std::isfinite(zl) && std::isfinite(zr) && std::isfinite(cx) && std::isfinite(cw) && cw > 16.0f &&
                zr - zl > 16.0f && zr - zl < cw - 1.0f) {
                x0 = std::fmax(0.0f, (zl - cx) / cw * 640.0f);
                x1 = std::fmin(640.0f, (zr - cx) / cw * 640.0f);
                if (x1 - x0 < 64.0f) x0 = x1 = 0.0f; // implausible: never black out most of the screen
            }
            if (log) {
                static uint32_t last = 0;
                if (g_nsmbwCurrentViTick - last >= 60u) {
                    last = g_nsmbwCurrentViTick;
                    std::fprintf(stderr, "[nsmbw][pillarbox] zone %.1f..%.1f visible %.1f..%.1f -> clip %.1f..%.1f%c", zl, zr, cx,
                                 cx + cw, x0, x1, 10);
                }
            }
        }
    }
    if (x1 > x0 || active) {
        // Tiles stop at the zone's edge (developer's choice 2026-11-19, over a fade into the
        // background). NSMBW_ZONE_FADE=<logical units> tries the fade: aurora then fades tile draws
        // out over that width past each edge, but nothing draws tiles there now (the 2026-11-19
        // fade test also extended the tile drawer, nsmbw_zone_edge_*.cpp, since removed).
        static const float fade = [] { const char* v = std::getenv("NSMBW_ZONE_FADE"); return v ? static_cast<float>(std::atof(v)) : 0.0f; }();
        AuroraSetLevelFadeWidth(fade);
        AuroraSetLevelClipX(x0, x1);
        active = x1 > x0;
    }
}
