// World-map camera limits for wide ratios (see nsmbw_wm_camera_clamp.cpp for the camera side).
//
// kLimits: the camera target x range each map allows in free-look at 16:9, i.e. how far the 16:9
// game lets the camera go, measured per world with NSMBW_DUMP_WMCAM=1 (the "[nsmbw][wmlimits]"
// line, logged when free-look starts; 2026-11-20). At a wider ratio the camera may only go to
// [left + margin, right - margin], margin = the extra half-width of the view at the map's camera
// distance (2241 units from the target, horizontal 1/tan = 3.8998 at 16:9: 2241 / 3.8998 * (k - 1)).
// A map not in the table is not clamped, in free-look either.
#include "nsmbw_va.h"
#include "nsmbw_wm_camera_limits.h"
#include <cstdint>
#include <cstdlib>
#include "memory.h"
#include <dolphin/gx/GXAurora.h>

extern "C" float g_nsmbwAspectWidthFactor; // nsmbw_product.cpp

namespace {
struct Limits { uint8_t world, scene; float left, right, zMin = 0.0f, zMax = 0.0f; }; // z range 0/0 = not clamped
// world / scene are dScWMap_c::m_WorldNo / m_SceneNo (0-based world).
constexpr Limits kLimits[] = {
    {0, 0, 210.0f, 2200.0f}, // World 1
    {2, 0, 510.0f, 1800.0f}, // World 3 (free-look z -310..-230; not clamped, the developer found it fine)
    {3, 0, -140.0f, 1290.0f, -2460.0f, -380.0f}, // World 4 (left pulled in 100: its far top-left corner showed, developer 2026-11-21)
    {4, 0, 420.0f, 3220.0f}, /* free-look z -160..120, not clamped (fine as is) */ // World 5 (right pulled in 140: its far top-right corner showed)
    // Developer, 2026-11-21: Worlds 2, 6, 7, 8, 9 show no edge at 21:9, so they are not clamped.
};

bool Enabled() {
    static const bool off = [] { const char* v = std::getenv("NSMBW_WMCAM_CLAMP"); return v != nullptr && v[0] == '0'; }();
    return !off && g_nsmbwAspectWidthFactor > 1.001f;
}

const Limits* Current() {
    uint32_t wm = 0;
    if (!Memory::TryRead32(NSMBW_VA(0x8042A52Cu), wm)) return nullptr; // m_WorldNo 0x8042A52D, m_SceneNo 0x8042A52E
    const uint8_t world = (wm >> 16) & 0xFFu, scene = (wm >> 8) & 0xFFu;
    for (const auto& l : kLimits) if (l.world == world && l.scene == scene) return &l;
    return nullptr;
}
} // namespace

float NsmbwWmCameraMargin() {
    if (!Enabled()) return 0.0f;
    static const float over = [] { const char* v = std::getenv("NSMBW_WMCAM_MARGIN"); return v ? static_cast<float>(std::atof(v)) : -1.0f; }();
    if (over >= 0.0f) return over;
    return 2241.0f / 3.8998f * (g_nsmbwAspectWidthFactor - 1.0f);
}

// Depth (z, up/down on the map): the same range free-look allows (the [nsmbw][wmlimits] "z" values),
// so walking can't take the camera where free-look never goes. World 4's top-left node showed the
// map's corner while walking but not in free-look (developer, 2026-11-21).
double NsmbwWmClampCameraZ(double z) {
    if (!Enabled()) return z;
    const Limits* l = Current();
    if (l == nullptr || l->zMin == l->zMax) return z;
    return z < l->zMin ? l->zMin : (z > l->zMax ? l->zMax : z);
}

double NsmbwWmClampCameraX(double x) {
    if (!Enabled()) return x;
    const Limits* l = Current();
    if (l == nullptr) return x;
    const double m = NsmbwWmCameraMargin();
    const double lo = l->left + m, hi = l->right - m;
    if (lo > hi) return (l->left + l->right) * 0.5; // map narrower than the view: centre it
    return x < lo ? lo : (x > hi ? hi : x);
}

// Free-look: 0x8092D620 stores its allowed offset range (+0x658 min x, +0x664 max x) once when
// free-look starts, and 0x8092DF30 clamps the offset to it every frame. Called every frame from the
// VI tick: pull a newly stored range in by the margin on each side (or to its middle when that
// leaves nothing), once per range the game stores.
extern "C" void NsmbwWmShrinkFreeLook() {
    const float m = NsmbwWmCameraMargin();
    if (m <= 0.0f || Current() == nullptr) return; // only maps in kLimits
    uint32_t cam = 0;
    if (!Memory::TryRead32(NSMBW_VA(0x8042A564u), cam) || cam == 0) return; // dWCamera_c::m_instance
    static float wroteMin = 0.0f, wroteMax = 0.0f;
    const float mn = Memory::ReadFloat32(cam + 1624), mx = Memory::ReadFloat32(cam + 1636);
    if ((mn == 0.0f && mx == 0.0f) || (mn == wroteMin && mx == wroteMax)) return;
    float nmn = mn + m, nmx = mx - m;
    if (nmn > nmx) nmn = nmx = (mn + mx) * 0.5f;
    Memory::WriteFloat32(cam + 1624, nmn);
    Memory::WriteFloat32(cam + 1636, nmx);
    wroteMin = nmn;
    wroteMax = nmx;
}

// Maps that barely scroll sideways and are only as wide as the 16:9 view (World 3's castle half:
// camera x 120..180), where no clamp can hide both edges: zoom in by the aspect factor instead, so
// the horizontal view is 16:9's (developer's choice 2026-11-21; the top and bottom are cropped).
// Called every VI tick; scene 3 only.
extern "C" void NsmbwWmZoomTick(bool onMap) {
    static float current = 1.0f;
    float want = 1.0f;
    if (onMap && Enabled()) {
        uint32_t wm = 0;
        if (Memory::TryRead32(NSMBW_VA(0x8042A52Cu), wm)) {
            const uint8_t world = (wm >> 16) & 0xFFu, scene = (wm >> 8) & 0xFFu;
            if (world == 2 && scene == 1) want = g_nsmbwAspectWidthFactor; // World 3, castle half
        }
    }
    if (want != current) {
        current = want;
        AuroraSetPerspectiveZoom(want);
    }
}
