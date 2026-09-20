// Native bindings for NSMBW's locked-cache DMA functions (OS LC*).
//
// Root cause of "every 3D model's node matrices are zero": nw4r::g3d's CALC_WORLD pass (called
// per actor via m3d::scnLeaf_c::calc) does not compute the per-node world matrices in place. It
// invalidates ScnMdlSimple::mpWorldMtxArray (DCInvalidateRange, 0x801AC580), computes the matrices
// into the locked L1 cache at 0xE0000000, then DMAs the results back to main memory with
// LCStoreData/LCStoreBlocks and waits on LCQueueWait (visible in ScnMdlSimple's CALC_WORLD handler
// at 0x8025A728: `lis r27, -0x2000` right after the invalidate). The per-node attrib words are
// written straight to main memory, which is why they looked correct (0xF0000000) while every
// matrix next to them stayed zero: the attribs never went through the DMA, the matrices did.
//
// On hardware the DMA is triggered by writing the DMAU/DMAL special-purpose registers (mtspr
// 922/923); the translated code cannot perform that transfer, so the store-back never happened.
// The runtime already HLEs the whole LC family as synchronous copies for MKW
// (runtime/src/hle/os/os_cache.cpp) at MKW's addresses only - the same dead-address class as the
// GX fixes. NSMBW's addresses are not named in function_map.txt; identified from this build's
// disassembly by the DMA register writes each performs, in SDK order after DCStoreRangeNoSync
// (0x801AC640, which IS named):
//   0x801AC850 LCLoadBlocks(destTag, srcAddr, numBlocks)   mtspr DMAU/DMAL, DMAL |= 0x12 (load)
//   0x801AC880 LCStoreBlocks(destAddr, srcTag, numBlocks)  mtspr DMAU/DMAL, DMAL |= 0x02 (store)
//   0x801AC8B0 LCStoreData(destAddr, srcTag, nBytes)       128-block chunks via LCStoreBlocks
//   0x801AC950 LCQueueLength()                             (HID2 >> 4) & 0xF
//   0x801AC960 LCQueueWait(len)                            spins on LCQueueLength
// The HLE wrappers take the CpuContext and read r3..r5 themselves, so the same entry points are
// reused unchanged. New addresses -> shard manifest regen (Translator.Cli emit-nsmbw-build-shards).
#include "hle_stubs.h"
#include "ppc_runtime.h"

extern "C" void LCLoadBlocks_HLE_801a1894(CpuContext* ctx);
extern "C" void LCStoreBlocks_HLE_801a18b8(CpuContext* ctx);
extern "C" uint32_t LCStoreData_HLE_801a18dc(CpuContext* ctx);
extern "C" uint32_t LCQueueLength_HLE_801a197c(CpuContext* ctx);
extern "C" void LCQueueWait_HLE_801a1988(CpuContext* ctx);

PPC_NATIVE_OVERRIDE_VOID(801AC850, LCLoadBlocks_HLE_801a1894, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(801AC880, LCStoreBlocks_HLE_801a18b8, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE(801AC8B0, LCStoreData_HLE_801a18dc, uint32_t, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE(801AC950, LCQueueLength_HLE_801a197c, uint32_t, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(801AC960, LCQueueWait_HLE_801a1988, (CpuContext* ctx), (ctx));
