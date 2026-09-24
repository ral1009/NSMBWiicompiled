// DIAGNOSTIC (temporary): m3d::calcView(int idx) (0x80164EA0), reimplemented exactly from this
// build's disassembly so it can be observed (an override replaces the translated body):
//   lightMgrs = *(r13-0x52CC); if (lightMgrs && lightMgrs[idx]) {
//       cam = m3d::getCurrentCamera()            [0x80164C70, handle returned in r3]
//       Camera::GetCameraMtx(&cam, &mtx)         [0x802541F0]
//       id = m3d::getCurrentCameraID()           [0x80164C80]
//       lightMgrs[idx]->vtable[0x24](mgr, &mtx, (u8)id, scnRoot)
//   }
//   fogMgrs = *(r13-0x52C4); if (fogMgrs && fogMgrs[idx]) fogMgr->CopyToG3D(scnRoot) [0x802C6950]
//   ScnRoot::CalcView(scnRoot)  [0x80259250]; GatherDrawScnObj [0x802592D0]; ZSort [0x802593C0]
// r13 = 0x8042F980 (sda_base), l_scnRoot_p = r13-0x52D4.
//
// Every model's per-node world matrix is valid in steady state but every view palette
// (ScnMdlSimple::mpViewPosMtxArray, the array bound for drawing) stays zero. This logs whether the
// scene's view pass runs at all and samples one palette before/after it.
#include "hle_stubs.h"
#include <aurora/env.hpp>
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"

#include <cstdio>
#include <cstdlib>

extern "C" uint32_t g_nsmbwCurrentSceneProfile;

namespace {
constexpr uint32_t kR13 = 0x8042F980u;
constexpr uint32_t kLightMgrsAddr = kR13 - 0x52CCu;
constexpr uint32_t kFogMgrsAddr = kR13 - 0x52C4u;
constexpr uint32_t kScnRootAddr = kR13 - 0x52D4u;
constexpr uint32_t kGetCurrentCamera = 0x80164C70u;
constexpr uint32_t kGetCameraMtx = 0x802541F0u;
constexpr uint32_t kGetCurrentCameraID = 0x80164C80u;
constexpr uint32_t kFogCopyToG3D = 0x802C6950u;
constexpr uint32_t kScnRootCalcView = 0x80259250u;
constexpr uint32_t kScnRootGather = 0x802592D0u;
constexpr uint32_t kScnRootZSort = 0x802593C0u;
// Scratch in the locked-cache region, only used transiently by g3d's own calc passes, never
// concurrently with this function.
constexpr uint32_t kScratchCamHandle = 0xE0003C00u;
constexpr uint32_t kScratchMtx = 0xE0003C10u;
} // namespace

extern "C" void M3dCalcView_Diag_80164EA0(uint32_t idx) {
    static const bool logEnabled = AURORA_ENV("NSMBW_LOG_SCNOBJ") != nullptr;
    static uint64_t calls = 0;
    ++calls;
    const bool doLog = logEnabled && g_nsmbwCurrentSceneProfile == 5u && (calls <= 4 || (calls % 120) == 0);

    auto& cpu = GetPersistentCpuContext();
    uint32_t scnRoot = 0;
    Memory::TryRead32(kScnRootAddr, scnRoot);

    uint32_t lightMgrs = 0;
    Memory::TryRead32(kLightMgrsAddr, lightMgrs);
    uint32_t lightMgr = 0;
    if (lightMgrs != 0) Memory::TryRead32(lightMgrs + idx * 4u, lightMgr);
    if (lightMgr != 0) {
        InvokeIndirectCpu(kGetCurrentCamera, &cpu);
        Memory::Write32(kScratchCamHandle, cpu.gpr[3]);
        cpu.gpr[3] = kScratchCamHandle;
        cpu.gpr[4] = kScratchMtx;
        InvokeIndirectCpu(kGetCameraMtx, &cpu);
        InvokeIndirectCpu(kGetCurrentCameraID, &cpu);
        const uint32_t camId = cpu.gpr[3] & 0xFFu;
        uint32_t vtbl = 0, fn = 0;
        Memory::TryRead32(lightMgr, vtbl);
        if (vtbl != 0) Memory::TryRead32(vtbl + 0x24u, fn);
        if (fn != 0) {
            cpu.gpr[3] = lightMgr;
            cpu.gpr[4] = kScratchMtx;
            cpu.gpr[5] = camId;
            cpu.gpr[6] = scnRoot;
            InvokeIndirectCpu(fn, &cpu);
        }
    }

    uint32_t fogMgrs = 0;
    Memory::TryRead32(kFogMgrsAddr, fogMgrs);
    uint32_t fogMgr = 0;
    if (fogMgrs != 0) Memory::TryRead32(fogMgrs + idx * 4u, fogMgr);
    if (fogMgr != 0) {
        cpu.gpr[3] = fogMgr;
        cpu.gpr[4] = scnRoot;
        InvokeIndirectCpu(kFogCopyToG3D, &cpu);
    }

    // Sample: the first tracked ScnMdlSimple's palette (obj 0x811E51E0 in this scene, palette
    // pointer at +0xF4) before and after ScnRoot::CalcView.
    float before[4] = {}, after[4] = {};
    uint32_t pal = 0;
    if (doLog) {
        Memory::TryRead32(0x811E51E0u + 0xF4u, pal);
        if (pal >= 0x80000000u && pal < 0x94000000u) for (int i = 0; i < 4; ++i) before[i] = Memory::ReadFloat32(pal + i * 4);
    }

    // ScnGroup child list at CalcView time: mpScnObjArray @0xE0, mSize @0xE4 (Size() read at
    // +0xE4 by m3d::pushBack). Shows whether the models are in the root when the pass runs.
    uint32_t childArr = 0, childCount = 0;
    if (doLog) {
        Memory::TryRead32(scnRoot + 0xE0u, childArr);
        Memory::TryRead32(scnRoot + 0xE4u, childCount);
        char list[256] = {};
        int off = 0;
        for (uint32_t k = 0; k < childCount && k < 8 && childArr != 0; ++k) {
            uint32_t c = 0;
            Memory::TryRead32(childArr + k * 4u, c);
            off += std::snprintf(list + off, sizeof(list) - off, " %08X", c);
        }
        std::fprintf(stderr, "[g3d] NSMBW_CALCVIEW   root children=%u first:%s\n", childCount, list);
    }

    cpu.gpr[3] = scnRoot;
    InvokeIndirectCpu(kScnRootCalcView, &cpu);
    if (doLog && pal >= 0x80000000u && pal < 0x94000000u) {
        for (int i = 0; i < 4; ++i) after[i] = Memory::ReadFloat32(pal + i * 4);
        std::fprintf(stderr,
                     "[g3d] NSMBW_CALCVIEW call#%llu idx=%u lightMgr=0x%08X fogMgr=0x%08X scnRoot=0x%08X pal=0x%08X "
                     "row0 before=(%.3f,%.3f,%.3f,%.1f) after=(%.3f,%.3f,%.3f,%.1f)\n",
                     static_cast<unsigned long long>(calls), idx, lightMgr, fogMgr, scnRoot, pal, before[0], before[1],
                     before[2], before[3], after[0], after[1], after[2], after[3]);
        std::fflush(stderr);
    }
    // Isolation test: drive obj 0x811E51E0's own G3dProc(CALC_VIEW=4, 0, &identityCamera) and see
    // whether ITS handler writes the palette. Splits "the scene pass never reaches this object's
    // handler" from "the handler computes zeros".
    if (doLog && (calls % 240) == 0) {
        const uint32_t obj = 0x811E51E0u, camMtx = 0xE0003C40u;
        const float ident[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        for (int i = 0; i < 12; ++i) Memory::WriteFloat32(camMtx + i * 4, ident[i]);
        uint32_t vtbl = 0, fn = 0;
        Memory::TryRead32(obj, vtbl);
        if (vtbl != 0) Memory::TryRead32(vtbl + 0xCu, fn);
        float p0[4] = {};
        if (fn != 0) {
            cpu.gpr[3] = obj;
            cpu.gpr[4] = 4u; // G3DPROC_CALC_VIEW
            cpu.gpr[5] = 0u;
            cpu.gpr[6] = camMtx;
            InvokeIndirectCpu(fn, &cpu);
            if (pal >= 0x80000000u && pal < 0x94000000u) for (int i = 0; i < 4; ++i) p0[i] = Memory::ReadFloat32(pal + i * 4);
        }
        uint32_t flags = 0, mdlFlags = 0;
        Memory::TryRead32(obj + 0xCCu, flags);
        Memory::TryRead32(obj + 0x104u, mdlFlags);
        std::fprintf(stderr,
                     "[g3d] NSMBW_CALCVIEW   isolation G3dProc(CALC_VIEW) on obj=0x%08X fn=0x%08X flags=0x%X mdlFlags=0x%X -> pal row0=(%.3f,%.3f,%.3f,%.1f)\n",
                     obj, fn, flags, mdlFlags, p0[0], p0[1], p0[2], p0[3]);
        std::fflush(stderr);
    }

    cpu.gpr[3] = scnRoot;
    InvokeIndirectCpu(kScnRootGather, &cpu);
    cpu.gpr[3] = scnRoot;
    InvokeIndirectCpu(kScnRootZSort, &cpu);
}
PPC_NATIVE_OVERRIDE_VOID(80164EA0, M3dCalcView_Diag_80164EA0, (uint32_t idx), (idx));
