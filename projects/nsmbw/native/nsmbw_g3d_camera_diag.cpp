// DIAGNOSTIC (temporary): nw4r::g3d::Camera::GetCameraMtx (0x802541F0, function_map.txt).
//
// The 3D scene's skinning palettes read as all-zero in guest memory even though the calc pass
// runs (draws are gathered and issued every frame). Each palette entry is camera-view x world, so
// one zero camera matrix would zero every model at once - far more likely than every model's own
// world matrix being zero. This logs the view matrix the scene actually receives.
//
// Reimplemented exactly from this build's disassembly (an override replaces the translated body,
// so it cannot just call itself - see nsmbw_wipecircle_calc_diag.cpp for the crash that taught
// this):
//   if (!pMtx) return; data = *(this); if (!data) return;
//   if (!(data->flags /*+0x70*/ & 0x8)) UpdateCameraMtx()  [bl 0x80254630, r3 = this]
//   PSMTXCopy(data->cameraMtx /*+0x0*/, pMtx)              [bl 0x801C0640]
// CameraData layout per NSMBW-Decomp include/lib/nw4r/g3d/g3d_camera.h: cameraMtx @0x0,
// projMtx @0x30, flags @0x70, cameraPos @0x74, cameraTarget @0x8C, projType @0xA8.
// New address -> shard manifest regen required.
#include "hle_stubs.h"
#include <aurora/env.hpp>
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"

#include <cstdio>
#include <cstdlib>

extern "C" uint32_t g_nsmbwCurrentSceneProfile;

namespace {
constexpr uint32_t kUpdateCameraMtxAddr = 0x80254630u;
}

extern "C" void G3dCamera_GetCameraMtx_Diag_802541F0(uint32_t thisPtr, uint32_t pMtx) {
    if (pMtx == 0) return;
    uint32_t data = 0;
    Memory::TryRead32(thisPtr, data);
    if (data == 0) return;

    uint32_t flags = 0;
    Memory::TryRead32(data + 0x70u, flags);
    if ((flags & 0x8u) == 0) {
        auto& cpu = GetPersistentCpuContext();
        cpu.gpr[3] = thisPtr;
        InvokeIndirectCpu(kUpdateCameraMtxAddr, &cpu);
        Memory::TryRead32(data + 0x70u, flags);
    }

    float m[12];
    for (int i = 0; i < 12; ++i) {
        m[i] = Memory::ReadFloat32(data + static_cast<uint32_t>(i * 4));
        Memory::WriteFloat32(pMtx + static_cast<uint32_t>(i * 4), m[i]);
    }

    if (AURORA_ENV("NSMBW_LOG_MTX_LOADS") != nullptr && g_nsmbwCurrentSceneProfile == 5u) {
        static int logged = 0;
        if (logged < 10) {
            ++logged;
            const float px = Memory::ReadFloat32(data + 0x74u), py = Memory::ReadFloat32(data + 0x78u),
                        pz = Memory::ReadFloat32(data + 0x7Cu);
            const float tx = Memory::ReadFloat32(data + 0x8Cu), ty = Memory::ReadFloat32(data + 0x90u),
                        tz = Memory::ReadFloat32(data + 0x94u);
            uint32_t projType = 0;
            Memory::TryRead32(data + 0xA8u, projType);
            std::fprintf(stderr,
                         "[gx] NSMBW_MTX GetCameraMtx cam=0x%08X data=0x%08X flags=0x%X projType=%u pos=(%.1f,%.1f,%.1f) "
                         "target=(%.1f,%.1f,%.1f) row0=(%.3f,%.3f,%.3f,%.1f) row1=(%.3f,%.3f,%.3f,%.1f) "
                         "row2=(%.3f,%.3f,%.3f,%.1f)\n",
                         thisPtr, data, flags, projType, px, py, pz, tx, ty, tz, m[0], m[1], m[2], m[3], m[4], m[5],
                         m[6], m[7], m[8], m[9], m[10], m[11]);
            std::fflush(stderr);
        }
    }
}
PPC_NATIVE_OVERRIDE_VOID(802541F0, G3dCamera_GetCameraMtx_Diag_802541F0, (uint32_t thisPtr, uint32_t pMtx), (thisPtr, pMtx));
