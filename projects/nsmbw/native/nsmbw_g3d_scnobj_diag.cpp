// DIAGNOSTIC (temporary): registry of every nw4r::g3d::ScnObj pushed into the scene root, plus a
// per-frame dump of their LOCAL and WORLD matrices.
//
// Chain of evidence so far: every 3D draw transforms to view=(0,0,0); the indexed matrix loader
// runs and the palettes it fetches are zero in guest RAM; the g3d cameras (incl. three perspective
// ones) have valid view matrices. Palette entry = view x world, so the models' WORLD matrices must
// be the zeros. This shows the local matrix each object was given (SetMtx from the actor's
// pos/rot/scale) and the world matrix CalcWorld produced from it, which is the remaining split:
// zero local -> the actor never set a usable transform (e.g. zero scale); non-zero local but zero
// world -> CalcWorld itself is not running / not writing.
//
// m3d::pushBack (0x80164F90) reimplemented from this build's disassembly (an override replaces
// the translated body):
//   scnRoot = *(r13 - 0x52D4)            [m3d::internal::l_scnRoot_p; r13 = 0x8042F980, this
//                                         project's sda_base per nsmbw_tick_read_pump.cpp]
//   idx     = *(scnRoot + 0xE4)          [current Size()]
//   (scnRoot->vtable[0x34/4])(scnRoot, idx, obj)   [ScnGroup::Insert(idx, obj), virtual]
// ScnObj layout per NSMBW-Decomp include/lib/nw4r/g3d/g3d_scnobj.h: mMtxArray @0xC (LOCAL 0xC,
// WORLD 0x3C, VIEW 0x6C, 0x30 bytes each), mScnObjFlags @0xCC. New address -> shard regen.
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"

#include <cstdio>
#include <cstdlib>

extern "C" uint32_t g_nsmbwCurrentSceneProfile;

namespace {
constexpr uint32_t kScnRootPtrAddr = 0x8042F980u - 0x52D4u; // l_scnRoot_p
constexpr int kMaxTracked = 64;
uint32_t g_tracked[kMaxTracked];
int g_trackedCount = 0;

void ReadMtx(uint32_t addr, float* m) {
    for (int i = 0; i < 12; ++i) m[i] = Memory::ReadFloat32(addr + static_cast<uint32_t>(i * 4));
}
} // namespace

extern "C" void M3dPushBack_Diag_80164F90(uint32_t obj) {
    uint32_t scnRoot = 0;
    Memory::TryRead32(kScnRootPtrAddr, scnRoot);
    if (scnRoot == 0) return;
    uint32_t idx = 0, vtbl = 0, fn = 0;
    Memory::TryRead32(scnRoot + 0xE4u, idx);
    Memory::TryRead32(scnRoot, vtbl);
    if (vtbl != 0) Memory::TryRead32(vtbl + 0x34u, fn);
    if (fn == 0) return;

    if (std::getenv("NSMBW_LOG_SCNOBJ") != nullptr) {
        bool known = false;
        for (int i = 0; i < g_trackedCount; ++i) if (g_tracked[i] == obj) { known = true; break; }
        if (!known && g_trackedCount < kMaxTracked) g_tracked[g_trackedCount++] = obj;
        static int logged = 0;
        if (logged < 40) {
            ++logged;
            std::fprintf(stderr, "[g3d] NSMBW_SCNOBJ pushBack obj=0x%08X idx=%u scene=%u (tracked=%d)\n",
                         obj, idx, g_nsmbwCurrentSceneProfile, g_trackedCount);
            std::fflush(stderr);
        }
    }

    auto& cpu = GetPersistentCpuContext();
    cpu.gpr[3] = scnRoot;
    cpu.gpr[4] = idx;
    cpu.gpr[5] = obj;
    InvokeIndirectCpu(fn, &cpu);
}
PPC_NATIVE_OVERRIDE_VOID(80164F90, M3dPushBack_Diag_80164F90, (uint32_t obj), (obj));

// Called once per tick from nsmbw_tick_read_pump.cpp; dumps a few frames' worth during STAGE.
extern "C" void NsmbwDumpScnObjs() {
    if (std::getenv("NSMBW_LOG_SCNOBJ") == nullptr || g_nsmbwCurrentSceneProfile != 5u) return;
    static int tick = 0;
    static int dumps = 0;
    ++tick;
    if ((tick % 120) != 0 || dumps >= 4) return;
    ++dumps;
    std::fprintf(stderr, "[g3d] NSMBW_SCNOBJ dump #%d tracked=%d\n", dumps, g_trackedCount);
    for (int i = 0; i < g_trackedCount && i < 40; ++i) {
        const uint32_t obj = g_tracked[i];
        uint32_t flags = 0;
        if (!Memory::TryRead32(obj + 0xCCu, flags)) continue;
        float L[12], W[12];
        ReadMtx(obj + 0x0Cu, L);
        ReadMtx(obj + 0x3Cu, W);
        // ScnMdlSimple (NSMBW-Decomp g3d_scnmdlsmpl.h): mpWorldMtxArray @0xEC (per-node world,
        // filled by the CALC_WORLD bytecode pass), mpViewPosMtxArray @0xF4 (the palette handed to
        // GXSetArray(GX_POS_MTX_ARRAY), filled by CALC_VIEW), mNumViewMtx @0x102.
        uint32_t nodeWorld = 0, viewPos = 0, numViewMtx = 0;
        Memory::TryRead32(obj + 0xECu, nodeWorld);
        Memory::TryRead32(obj + 0xF4u, viewPos);
        numViewMtx = Memory::Read16(obj + 0x102u);
        float NW[12] = {}, VP[12] = {}, VP1[12] = {};
        if (nodeWorld >= 0x80000000u && nodeWorld < 0x94000000u) ReadMtx(nodeWorld, NW);
        if (viewPos >= 0x80000000u && viewPos < 0x94000000u) ReadMtx(viewPos, VP);
        // The CALC_VIEW handler writes buffer mCurView (@0x101) of mNumView (@0x100) buffers, each
        // numMtx*0x30 rounded up to 32 bytes; the draw binds the base. Read buffer 1 too.
        const uint32_t numView = Memory::Read8(obj + 0x100u), curView = Memory::Read8(obj + 0x101u);
        const uint32_t bufStride = (numViewMtx * 0x30u + 0x1Fu) & ~0x1Fu;
        if (numView > 1 && viewPos >= 0x80000000u && viewPos < 0x94000000u) ReadMtx(viewPos + bufStride, VP1);
        if (i < 8) {
            std::fprintf(stderr, "[g3d]     (views: num=%u cur=%u bufStride=%u buf1[0]=(%.3f,%.3f,%.3f|%.1f,%.1f,%.1f))\n", numView,
                         curView, bufStride, VP1[0], VP1[5], VP1[10], VP1[3], VP1[7], VP1[11]);
        }
        std::fprintf(stderr,
                     "[g3d]   obj=0x%08X flags=0x%X LOCAL diag=(%.3f,%.3f,%.3f) t=(%.1f,%.1f,%.1f) | "
                     "WORLD diag=(%.3f,%.3f,%.3f) t=(%.1f,%.1f,%.1f) | nodeWorld=0x%08X [0]=(%.3f,%.3f,%.3f|%.1f,%.1f,%.1f) "
                     "viewPos=0x%08X n=%u [0]=(%.3f,%.3f,%.3f|%.1f,%.1f,%.1f)\n",
                     obj, flags, L[0], L[5], L[10], L[3], L[7], L[11], W[0], W[5], W[10], W[3], W[7], W[11],
                     nodeWorld, NW[0], NW[5], NW[10], NW[3], NW[7], NW[11], viewPos, numViewMtx, VP[0], VP[5], VP[10],
                     VP[3], VP[7], VP[11]);
    }
    std::fflush(stderr);
}
