// DIAGNOSTIC (temporary): m3d::scnLeaf_c::calc(bool keepEnabledAfter) (0x8016A2E0).
//
// The per-node world matrices of every actor model (ScnMdlSimple::mpWorldMtxArray) read as zero,
// which zeroes the view palette and collapses every 3D draw to a point. NSMBW deliberately flags
// its models DISABLE_CALC_WORLD so the scene-root pass skips them, and instead each actor calls
// this function every frame to run the model's CALC_WORLD pass itself (NSMBW-Decomp
// source/dol/mLib/m_3d/scn_leaf.cpp). This reimplements it exactly from the disassembly and logs
// the node array before/after the pass, splitting "never called" from "called but the g3d
// bytecode pass writes nothing":
//   setOption(this, OPTID_DISABLE_CALC_WORLD=2, 0)          [bl 0x8016A230]
//   scn = *(this+4); scn->vtable[0xC](scn, G3DPROC_CALC_WORLD=1, 0, 0)
//   if (!keepEnabledAfter) setOption(this, 2, 1)
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
constexpr uint32_t kSetOptionAddr = 0x8016A230u;

void SetOption(uint32_t thisPtr, uint32_t option, uint32_t value) {
    auto& cpu = GetPersistentCpuContext();
    cpu.gpr[3] = thisPtr;
    cpu.gpr[4] = option;
    cpu.gpr[5] = value;
    InvokeIndirectCpu(kSetOptionAddr, &cpu);
}
} // namespace

extern "C" void M3dScnLeafCalc_Diag_8016A2E0(uint32_t thisPtr, uint32_t keepEnabledAfter) {
    static const bool logEnabled = AURORA_ENV("NSMBW_LOG_SCNOBJ") != nullptr;
    static int logged = 0;
    // Log a burst of early calls, then one in every 300 (about every 100 frames for a 3-model
    // scene) so the steady state after the first few frames is visible too.
    static uint64_t callsSeen = 0;
    ++callsSeen;
    const bool doLog = logEnabled && g_nsmbwCurrentSceneProfile == 5u &&
                       (logged < 6 || (callsSeen % 300) == 0) && logged < 40;
    if (doLog) {
        // Who called calc()? The bridge leaves the guest LR intact, so this names the actor code
        // responsible for the ordering of setLocalMtx() vs calc().
        CpuContext* cc = TryGetCpuContext();
        const uint32_t lr = cc ? static_cast<uint32_t>(cc->lr) : 0u;
        std::fprintf(stderr, "[g3d] NSMBW_SCNLEAF calc called from LR=0x%08X leaf=0x%08X keep=%u r2=0x%08X r13=0x%08X\n", lr,
                     thisPtr, keepEnabledAfter, cc ? static_cast<uint32_t>(cc->gpr[2]) : 0u,
                     cc ? static_cast<uint32_t>(cc->gpr[13]) : 0u);
        // Caller 0x800D37D0 (dPyMdlBase_c::_calc) keeps its `this` in r31 (non-volatile, untouched
        // by the callee at entry): mMtx @0xD8 is what it just passed to setLocalMtx, the scale
        // copy @0x144 is what dPyMdlBase_c::calc stored from its scale argument.
        if (cc && lr == 0x800D3828u) {
            const uint32_t py = static_cast<uint32_t>(cc->gpr[31]);
            float m[12], sc[3], rs[12];
            for (int i = 0; i < 12; ++i) m[i] = Memory::ReadFloat32(py + 0xD8u + i * 4);
            for (int i = 0; i < 12; ++i) rs[i] = Memory::ReadFloat32(py + 0x108u + i * 4);
            for (int i = 0; i < 3; ++i) sc[i] = Memory::ReadFloat32(py + 0x144u + i * 4);
            // 0x800D58E0 builds M = rot*scale into this+0x108 and then Trans(pos) x M into
            // this+0xD8 (mMtx); logging both separates a bad rotation build from a bad translate/
            // concat.
            std::fprintf(stderr,
                         "[g3d] NSMBW_SCNLEAF   dPyMdlBase=0x%08X mMtx@D8 row0=(%.3f,%.3f,%.3f,%.1f) row1=(%.3f,%.3f,%.3f,%.1f) "
                         "| M@108 row0=(%.3f,%.3f,%.3f,%.1f) row1=(%.3f,%.3f,%.3f,%.1f) row2=(%.3f,%.3f,%.3f,%.1f) scaleCopy=(%.3f,%.3f,%.3f)\n",
                         py, m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], rs[0], rs[1], rs[2], rs[3], rs[4], rs[5], rs[6],
                         rs[7], rs[8], rs[9], rs[10], rs[11], sc[0], sc[1], sc[2]);
        }
        std::fflush(stderr);
    }

    SetOption(thisPtr, 2u, 0u);

    // The translator inlines PSMTXCopy into the g3d helpers and routes every psq_l/psq_st through
    // ctx->gqr[0] (GqrHoisting) - a non-float GQR0 would quantize every matrix copy to garbage.
    if (doLog && logged == 0) {
        if (CpuContext* live = TryGetCpuContext()) {
            std::fprintf(stderr, "[g3d] NSMBW_SCNLEAF live GQR: %08X %08X %08X %08X %08X %08X %08X %08X\n", live->gqr[0],
                         live->gqr[1], live->gqr[2], live->gqr[3], live->gqr[4], live->gqr[5], live->gqr[6], live->gqr[7]);
            std::fflush(stderr);
        }
        // Unit test of the translated node-matrix helper 0x80256490 with crafted inputs, using the
        // locked-cache scratch (only transiently used by CalcWorld itself, and we are outside it
        // here). Signature from CalcWorld's call site: (dstWorld, dstScale, parentWorld, parentScale,
        // parentAttrib, &result). result.flags bit 1 (0x2) selects the plain parent-copy path
        // (PSMTXCopy inlined by the translator). Expected: dst == parent.
        {
            const uint32_t parent = 0xE0002000u, dst = 0xE0002400u, dstScale = 0xE0002800u, parentScale = 0xE0002810u,
                           result = 0xE0003000u;
            const float P[12] = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f, 10.f, 11.f, 12.f};
            for (int i = 0; i < 12; ++i) {
                Memory::WriteFloat32(parent + i * 4, P[i]);
                Memory::WriteFloat32(dst + i * 4, -1.f);
            }
            for (int i = 0; i < 3; ++i) {
                Memory::WriteFloat32(parentScale + i * 4, 1.f);
                Memory::WriteFloat32(dstScale + i * 4, 0.f);
            }
            for (int i = 0; i < 0x80; i += 4) Memory::Write32(result + i, 0u);
            Memory::Write32(result, 0x2u); // flags: identity -> copy parent
            auto& cpu3 = GetPersistentCpuContext();
            cpu3.gpr[3] = dst; cpu3.gpr[4] = dstScale; cpu3.gpr[5] = parent; cpu3.gpr[6] = parentScale;
            cpu3.gpr[7] = 0xF0000000u; cpu3.gpr[8] = result;
            InvokeIndirectCpu(0x80256490u, &cpu3);
            float D[12];
            for (int i = 0; i < 12; ++i) D[i] = Memory::ReadFloat32(dst + i * 4);
            std::fprintf(stderr,
                         "[g3d] NSMBW_SCNLEAF unit-test helper 0x80256490 (copy path): ret=0x%X dst=(%.1f,%.1f,%.1f,%.1f | "
                         "%.1f,%.1f,%.1f,%.1f | %.1f,%.1f,%.1f,%.1f) expected 1..12\n",
                         cpu3.gpr[3], D[0], D[1], D[2], D[3], D[4], D[5], D[6], D[7], D[8], D[9], D[10], D[11]);
            std::fflush(stderr);

            // Replay CalcWorld's real per-node step for node 0 of this model, step by step:
            //   nd = ResMdl::GetResNode(&mResMdl, 0)                      [0x8023B440]
            //   0x8023EFD0(&ndHandle, &result)   result from node SRT     (no anim object)
            //   result.flags |= 0x200; 0x8023EE30(&ndHandle, &result)     build result.mtx
            //   helper(dst, dstScale, root, rootScale, 0xF0000000, &result)
            uint32_t scnNow = 0;
            Memory::TryRead32(thisPtr + 4u, scnNow);
            if (scnNow != 0) {
                const uint32_t ndHandle = 0xE0003800u, res = 0xE0003000u;
                auto& cpu4 = GetPersistentCpuContext();
                cpu4.gpr[3] = scnNow + 0xE8u;
                cpu4.gpr[4] = 0u;
                InvokeIndirectCpu(0x8023B440u, &cpu4);
                const uint32_t nd = cpu4.gpr[3];
                Memory::Write32(ndHandle, nd);
                for (int i = 0; i < 0x80; i += 4) Memory::Write32(res + i, 0u);
                cpu4.gpr[3] = ndHandle; cpu4.gpr[4] = res;
                InvokeIndirectCpu(0x8023EFD0u, &cpu4);
                const uint32_t flagsA = Memory::Read32(res);
                float sA[3], tA[3];
                for (int i = 0; i < 3; ++i) { sA[i] = Memory::ReadFloat32(res + 4 + i * 4); tA[i] = Memory::ReadFloat32(res + 0x1C + i * 4); }
                Memory::Write32(res, flagsA | 0x200u);
                cpu4.gpr[3] = ndHandle; cpu4.gpr[4] = res;
                InvokeIndirectCpu(0x8023EE30u, &cpu4);
                const uint32_t flagsB = Memory::Read32(res);
                float M[12];
                for (int i = 0; i < 12; ++i) M[i] = Memory::ReadFloat32(res + 0x28 + i * 4);
                std::fprintf(stderr,
                             "[g3d] NSMBW_SCNLEAF replay node0: nd=0x%08X flagsA=0x%X scale=(%.2f,%.2f,%.2f) trans=(%.2f,%.2f,%.2f) "
                             "flagsB=0x%X result.mtx row0=(%.2f,%.2f,%.2f,%.2f) row1=(%.2f,%.2f,%.2f,%.2f)\n",
                             nd, flagsA, sA[0], sA[1], sA[2], tA[0], tA[1], tA[2], flagsB, M[0], M[1], M[2], M[3], M[4], M[5],
                             M[6], M[7]);
                // Now the helper with the real root (this object's WORLD matrix @0x3C).
                const uint32_t dst2 = 0xE0002400u, dstScale2 = 0xE0002800u, rootScale = 0xE0002810u;
                for (int i = 0; i < 12; ++i) Memory::WriteFloat32(dst2 + i * 4, -1.f);
                for (int i = 0; i < 3; ++i) { Memory::WriteFloat32(rootScale + i * 4, 1.f); Memory::WriteFloat32(dstScale2 + i * 4, 0.f); }
                cpu4.gpr[3] = dst2; cpu4.gpr[4] = dstScale2; cpu4.gpr[5] = scnNow + 0x3Cu; cpu4.gpr[6] = rootScale;
                cpu4.gpr[7] = 0xF0000000u; cpu4.gpr[8] = res;
                InvokeIndirectCpu(0x80256490u, &cpu4);
                float D2[12];
                for (int i = 0; i < 12; ++i) D2[i] = Memory::ReadFloat32(dst2 + i * 4);
                std::fprintf(stderr,
                             "[g3d] NSMBW_SCNLEAF replay helper: ret=0x%X dst row0=(%.3f,%.3f,%.3f,%.1f) row1=(%.3f,%.3f,%.3f,%.1f) "
                             "(root row0=(%.3f,%.3f,%.3f,%.1f))\n",
                             cpu4.gpr[3], D2[0], D2[1], D2[2], D2[3], D2[4], D2[5], D2[6], D2[7],
                             Memory::ReadFloat32(scnNow + 0x3Cu), Memory::ReadFloat32(scnNow + 0x40u),
                             Memory::ReadFloat32(scnNow + 0x44u), Memory::ReadFloat32(scnNow + 0x48u));
                std::fflush(stderr);
            }
        }
    }

    uint32_t scn = 0;
    Memory::TryRead32(thisPtr + 4u, scn);
    uint32_t nodeWorld = 0;
    float before[4] = {}, after[4] = {};
    float rootBefore[4] = {}, rootAfter[4] = {}, localBefore[4] = {};
    if (doLog && scn != 0) {
        Memory::TryRead32(scn + 0xECu, nodeWorld);
        if (nodeWorld >= 0x80000000u && nodeWorld < 0x94000000u) {
            for (int i = 0; i < 4; ++i) before[i] = Memory::ReadFloat32(nodeWorld + i * 4);
        }
        for (int i = 0; i < 4; ++i) {
            rootBefore[i] = Memory::ReadFloat32(scn + 0x3Cu + i * 4);
            localBefore[i] = Memory::ReadFloat32(scn + 0x0Cu + i * 4);
        }
    }

    if (scn != 0) {
        // EXPERIMENT (NSMBW_EXP_NO_CWCB): run the real CALC_WORLD pass with the model's
        // ICalcWorldCallback (ScnMdlSimple::mpCalcWorldCallback @0x11C, m3d::mdl_c's blend
        // callback) temporarily nulled, so the handler takes its no-callback branch. If the node
        // array then fills in, the callback's blend math is what zeroes it; if still zero, the core
        // pass is. Restored right after the call.
        static const bool expNoCallback = AURORA_ENV("NSMBW_EXP_NO_CWCB") != nullptr;
        uint32_t savedCb = 0;
        if (expNoCallback) {
            Memory::TryRead32(scn + 0x11Cu, savedCb);
            Memory::Write32(scn + 0x11Cu, 0u);
        }
        uint32_t vtbl = 0, fn = 0;
        Memory::TryRead32(scn, vtbl);
        if (vtbl != 0) Memory::TryRead32(vtbl + 0xCu, fn);
        if (fn != 0) {
            auto& cpu = GetPersistentCpuContext();
            cpu.gpr[3] = scn;
            cpu.gpr[4] = 1u; // G3DPROC_CALC_WORLD
            cpu.gpr[5] = 0u;
            cpu.gpr[6] = 0u;
            InvokeIndirectCpu(fn, &cpu);
            if (expNoCallback) {
                Memory::Write32(scn + 0x11Cu, savedCb);
            }
            if (doLog) {
                ++logged;
                uint32_t flags = 0;
                Memory::TryRead32(scn + 0xCCu, flags);
                if (nodeWorld >= 0x80000000u && nodeWorld < 0x94000000u) {
                    for (int i = 0; i < 4; ++i) after[i] = Memory::ReadFloat32(nodeWorld + i * 4);
                }
                for (int i = 0; i < 4; ++i) rootAfter[i] = Memory::ReadFloat32(scn + 0x3Cu + i * 4);
                std::fprintf(stderr,
                             "[g3d] NSMBW_SCNLEAF   LOCAL row0=(%.3f,%.3f,%.3f,%.1f) WORLD before=(%.3f,%.3f,%.3f,%.1f) "
                             "after=(%.3f,%.3f,%.3f,%.1f)\n",
                             localBefore[0], localBefore[1], localBefore[2], localBefore[3], rootBefore[0], rootBefore[1],
                             rootBefore[2], rootBefore[3], rootAfter[0], rootAfter[1], rootAfter[2], rootAfter[3]);
                // ScnMdlSimple (g3d_scnmdlsmpl.h): mResMdl @0xE8, mpWorldMtxAttribArray @0xF0,
                // mFlagScnMdlSimple @0x104, mpByteCodeCalc @0x108 - the NodeTree bytecode the
                // CALC_WORLD pass interprets; null/zeroed bytecode means the pass has nothing to do.
                uint32_t resMdl = 0, attribArr = 0, mdlFlags = 0, byteCode = 0;
                Memory::TryRead32(scn + 0xE8u, resMdl);
                Memory::TryRead32(scn + 0xF0u, attribArr);
                Memory::TryRead32(scn + 0x104u, mdlFlags);
                Memory::TryRead32(scn + 0x108u, byteCode);
                uint8_t bc[12] = {};
                if (byteCode >= 0x80000000u && byteCode < 0x94000000u) {
                    for (int i = 0; i < 12; ++i) bc[i] = Memory::Read8(byteCode + i);
                }
                std::fprintf(stderr,
                             "[g3d] NSMBW_SCNLEAF calc leaf=0x%08X scn=0x%08X G3dProc=0x%08X flags=0x%X nodeWorld=0x%08X "
                             "row0 before=(%.3f,%.3f,%.3f,%.1f) after=(%.3f,%.3f,%.3f,%.1f) | resMdl=0x%08X attribArr=0x%08X "
                             "mdlFlags=0x%X byteCodeCalc=0x%08X bytes=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
                             thisPtr, scn, fn, flags, nodeWorld, before[0], before[1], before[2], before[3], after[0],
                             after[1], after[2], after[3], resMdl, attribArr, mdlFlags, byteCode, bc[0], bc[1], bc[2], bc[3],
                             bc[4], bc[5], bc[6], bc[7], bc[8], bc[9], bc[10], bc[11]);
                // Every slot, not just [0]: CalcWorld indexes the array by each node's matrix ID,
                // so an unused slot 0 would make "nothing written" indistinguishable from "written
                // elsewhere". Prints each slot's diagonal + translation, and the attrib word
                // CalcWorld stores alongside it.
                const uint32_t numMtx = Memory::Read16(scn + 0x102u);
                // Raw ResNodeData for node 0 via the real ResMdl::GetResNode (0x8023B440, a different
                // function so no self-recursion): r3 = &mResMdl handle (scn+0xE8), r4 = nodeID.
                // nw4r ResNodeData: mtxID @0x10, flags @0x14, scale @0x20, rot @0x2C, trans @0x38,
                // nodeMtx (precomputed local) @0x70. If these read as zero the input resource is the
                // problem; if they are sane, the matrix math downstream is.
                {
                    auto& cpu2 = GetPersistentCpuContext();
                    cpu2.gpr[3] = scn + 0xE8u;
                    cpu2.gpr[4] = 0u;
                    InvokeIndirectCpu(0x8023B440u, &cpu2);
                    const uint32_t nd = cpu2.gpr[3];
                    if (nd >= 0x80000000u && nd < 0x94000000u) {
                        uint32_t ndMtxId = 0, ndFlags = 0;
                        Memory::TryRead32(nd + 0x10u, ndMtxId);
                        Memory::TryRead32(nd + 0x14u, ndFlags);
                        float s[3], r[3], t[3], nm[4];
                        for (int i = 0; i < 3; ++i) {
                            s[i] = Memory::ReadFloat32(nd + 0x20u + i * 4);
                            r[i] = Memory::ReadFloat32(nd + 0x2Cu + i * 4);
                            t[i] = Memory::ReadFloat32(nd + 0x38u + i * 4);
                        }
                        for (int i = 0; i < 4; ++i) nm[i] = Memory::ReadFloat32(nd + 0x70u + i * 4);
                        std::fprintf(stderr,
                                     "[g3d]     resNode0=0x%08X mtxID=%u flags=0x%X scale=(%.3f,%.3f,%.3f) rot=(%.2f,%.2f,%.2f) "
                                     "trans=(%.2f,%.2f,%.2f) nodeMtx row0=(%.3f,%.3f,%.3f,%.2f) anmChr=0x%08X\n",
                                     nd, ndMtxId, ndFlags, s[0], s[1], s[2], r[0], r[1], r[2], t[0], t[1], t[2], nm[0], nm[1],
                                     nm[2], nm[3], Memory::Read32(scn + 0x124u));
                    }
                }
                // mFlagScnMdlSimple bit0 == 1 means CALC_WORLD computed into the locked cache at
                // 0xE0000000 and relied on LCStoreData to copy back. Reading the LC copy directly
                // separates "CalcWorld produced zeros" from "the store-back never landed".
                if ((mdlFlags & 1u) != 0) {
                    for (uint32_t k = 0; k < numMtx && k < 3; ++k) {
                        float m[12];
                        for (int i = 0; i < 12; ++i) m[i] = Memory::ReadFloat32(0xE0000000u + k * 48u + i * 4u);
                        std::fprintf(stderr, "[g3d]     LC[%u] diag=(%.3f,%.3f,%.3f) t=(%.1f,%.1f,%.1f)\n", k, m[0], m[5],
                                     m[10], m[3], m[7], m[11]);
                    }
                }
                if (nodeWorld >= 0x80000000u && nodeWorld < 0x94000000u) {
                    for (uint32_t k = 0; k < numMtx && k < 16; ++k) {
                        float m[12];
                        for (int i = 0; i < 12; ++i) m[i] = Memory::ReadFloat32(nodeWorld + k * 48u + i * 4u);
                        uint32_t attrib = 0;
                        if (attribArr >= 0x80000000u && attribArr < 0x94000000u) Memory::TryRead32(attribArr + k * 4u, attrib);
                        std::fprintf(stderr, "[g3d]     node[%u] attrib=0x%X diag=(%.3f,%.3f,%.3f) t=(%.1f,%.1f,%.1f)\n", k, attrib,
                                     m[0], m[5], m[10], m[3], m[7], m[11]);
                    }
                }
                std::fflush(stderr);
            }
        } else if (doLog) {
            ++logged;
            std::fprintf(stderr, "[g3d] NSMBW_SCNLEAF calc leaf=0x%08X scn=0x%08X: no G3dProc vtable entry\n", thisPtr, scn);
            std::fflush(stderr);
        }
    }

    if (keepEnabledAfter == 0) {
        SetOption(thisPtr, 2u, 1u);
    }
}
PPC_NATIVE_OVERRIDE_VOID(8016A2E0, M3dScnLeafCalc_Diag_8016A2E0, (uint32_t thisPtr, uint32_t keepEnabledAfter), (thisPtr, keepEnabledAfter));
