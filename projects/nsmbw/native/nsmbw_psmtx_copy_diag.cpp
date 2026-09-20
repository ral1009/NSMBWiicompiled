// DIAGNOSTIC / candidate fix: native PSMTXCopy (0x801C0640, function_map.txt).
//
// nw4r::g3d::CalcWorld (0x80250FB0) opens with PSMTXCopy(pRootMtx -> pWorldMtxArray[0]) and every
// per-node fast path in its helpers (0x80256490 etc.) reduces to PSMTXCopy / PSMTXConcat of valid
// inputs - yet every slot of the node array reads zero afterwards (NSMBW_LOG_SCNOBJ). The SDK's
// PSMTXCopy is 6 psq_l/psq_st pairs (paired-single quantized load/store through GQR0). Replacing
// only this leaf with a plain 12-float copy is the narrowest possible test of whether the
// translated paired-single memory ops are what zero the matrices. Semantics: dst = src, 3x4 floats,
// big-endian in guest memory. Requires a shard-manifest regen (new override address).
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "memory.h"

extern "C" void PSMTXCopy_Native_801C0640(uint32_t src, uint32_t dst) {
    for (int i = 0; i < 12; ++i) {
        Memory::WriteFloat32(dst + static_cast<uint32_t>(i * 4),
                             Memory::ReadFloat32(src + static_cast<uint32_t>(i * 4)));
    }
}
PPC_NATIVE_OVERRIDE_VOID(801C0640, PSMTXCopy_Native_801C0640, (uint32_t src, uint32_t dst), (src, dst));
