// Fix (not just a diagnostic): an eighth instance of the same dead-override bug already found and
// fixed for GXSetTevColor/GXSetViewport/GXSetScissor/GXSetAlphaCompare/GXSetTexCopySrc/
// GXSetTexCopyDst - GXLoadPosMtxImm's existing override in runtime/src/hle/gx/gx_transform.cpp
// lives at 0x8017310c, which is MKW's address for this SDK function, not NSMBW's. Confirmed
// against this project's own projects/nsmbw/function_map.txt: NSMBW's real GXLoadPosMtxImm is at
// 0x801C9A80.
//
// This one is a much higher-value target than the others found so far: GXLoadPosMtxImm loads the
// position/modelview matrix used to transform every vertex from local (model) space into
// world/view space - it's directly relevant to the "title screen renders almost entirely black
// despite correct, undistorted local-space vertex data and correctly-bound real textures" finding
// from the STAGE(5)/STAGE_TITLE investigation. A wrong or stale matrix would place perfectly
// correct geometry off-screen or behind the camera without there being anything wrong with the
// geometry or textures themselves.
//
// That said: aurora's own command_processor.cpp already has generic XF-register decode for
// position matrix loads ("XF: PosMtx copy" - confirmed present), and GXLoadPosMtxImm on real
// hardware works by writing the matrix to XF memory via ordinary FIFO commands, not through any
// concept of "call the SDK function" - so, like GXSetTevColor turned out to be, this dead override
// may or may not actually change anything; the auto-translated code's raw FIFO writes might
// already reach aurora correctly via that generic path. Fixing the address is correct regardless
// (dead code silently diverging from NSMBW's real behavior is a bug on its own merits), but
// whether it explains the black-screen finding needs the same kind of before/after empirical check
// already used for the other four fixes, not just this comment's reasoning.
//
// New address -> requires a shard-manifest regen (Translator.Cli emit-nsmbw-build-shards) same as
// every other new override this session.
#include "hle_stubs.h"
#include <aurora/env.hpp>
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include <dolphin/gx/GXTransform.h>
#include <cstdio>
#include <cstdlib>

extern "C" uint32_t g_nsmbwCurrentSceneProfile;
extern "C" uint32_t g_nsmbwXfLoadSource;
extern "C" void GXLoadPosMtxImm_Fixed_801C9A80(uint32_t ma, uint32_t id) {
    float m[3][4];
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) {
            m[row][col] = Memory::ReadFloat32(ma + static_cast<uint32_t>((row * 4 + col) * 4));
        }
    }
    // DIAGNOSTIC (temporary): NSMBW_LOG_MTX_LOADS - see runtime gx_transform.cpp. Shows what the
    // immediate loader is handed during the 3D scene, so "guest computed zeros" can be told apart
    // from "aurora lost a non-zero matrix".
    // NSMBW_LOG_ZERO_MTX: in steady-state STAGE most PNMTX0 loads carry an all-zero matrix
    // (aurora-side NSMBW_LOG_PNMTX0). Report the guest return address of such loads so the code
    // computing them can be identified; distinct callers only.
    if (AURORA_ENV("NSMBW_LOG_ZERO_MTX") != nullptr && g_nsmbwCurrentSceneProfile == 5u && id == 0u) {
        static uint64_t seen = 0;
        ++seen;
        const bool allZero = m[0][0] == 0.f && m[1][1] == 0.f && m[2][2] == 0.f && m[0][3] == 0.f && m[1][3] == 0.f;
        if (seen > 20000 && allZero) {
            static uint32_t callers[12] = {};
            static int nCallers = 0;
            const uint32_t lr = TryGetCpuContext() ? static_cast<uint32_t>(TryGetCpuContext()->lr) : 0u;
            bool known = false;
            for (int i = 0; i < nCallers; ++i) if (callers[i] == lr) { known = true; break; }
            if (!known && nCallers < 12) {
                callers[nCallers++] = lr;
                std::fprintf(stderr, "[gx] NSMBW_ZERO_MTX PosMtxImm id=0 all-zero matrix from LR=0x%08X ma=0x%08X\n", lr, ma);
                std::fflush(stderr);
            }
        }
    }
    if (AURORA_ENV("NSMBW_LOG_MTX_LOADS") != nullptr && g_nsmbwCurrentSceneProfile == 5u) {
        static int logged = 0;
        if (logged < 12) {
            ++logged;
            std::fprintf(stderr, "[gx] NSMBW_MTX PosMtxImm ma=0x%08X id=%u row0=(%.3f,%.3f,%.3f,%.3f) row1=(%.3f,%.3f,%.3f,%.3f)\n",
                         ma, id, m[0][0], m[0][1], m[0][2], m[0][3], m[1][0], m[1][1], m[1][2], m[1][3]);
            std::fflush(stderr);
        }
    }
    g_nsmbwXfLoadSource = 1u;
    GXLoadPosMtxImm(m, id);
    g_nsmbwXfLoadSource = 0u;
}
PPC_NATIVE_OVERRIDE_VOID(801C9A80, GXLoadPosMtxImm_Fixed_801C9A80, (uint32_t ma, uint32_t id), (ma, id));
