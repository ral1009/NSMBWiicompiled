// Native bindings for NSMBW's INDEXED matrix loads (GXLoadPosMtxIndx / GXLoadNrmMtxIndx3x3).
//
// Root cause of "the 3D scene draws ~277 times per frame and nothing appears": a per-draw dump
// (NSMBW_DUMP_DRAWS_SCENE) showed every 3D model draw transforming to view=(0,0,0) - every
// position matrix in aurora's matrix memory was all zeros, so every vertex collapsed to the origin
// and every triangle had zero area. 2D nw4r::lyt content was unaffected because it loads its
// matrices with GXLoadPosMtxImm (immediate data, handled by aurora's generic XF decode); skinned
// nw4r::g3d models load their matrix palettes with the INDEXED variants instead.
//
// On hardware the indexed loaders emit CP opcodes 0x20/0x28 that fetch the matrix from the array
// registered via GXSetArray(GX_POS_MTX_ARRAY / GX_NRM_MTX_ARRAY). This runtime's GXSetArray HLE
// (gx_vertex.cpp) records those arrays only in its own g_hleGxState.vtxArray table and never
// forwards them to aurora, so when the guest's auto-translated loaders emitted the raw opcode,
// aurora's LOAD_INDX handler found no source array and skipped every load ("Skipping indexed XF
// load with invalid source array" in the log). MKW never hits this because its addresses for
// these loaders are bound to the HLE wrappers below, which do the indexed fetch host-side from that
// same table and hand aurora an immediate load. NSMBW's addresses were never bound - the function
// map has no name for them; they were identified from this build's disassembly by the opcode each
// emits (0x801C9AD0 writes 0x20 / (mi<<16)|0xB000|id*4, 0x801C9B60 writes 0x28 /
// (mi<<16)|0x8000|(0x400+id*3)), sitting between GXLoadPosMtxImm (0x801C9A80) and GXSetCurrentMtx
// (0x801C9BA0) exactly as the SDK lays them out.
//
// New addresses -> shard manifest regen required (Translator.Cli emit-nsmbw-build-shards).
#include "hle_stubs.h"
#include "ppc_runtime.h"

extern "C" void GX__LoadPosMtxIndx_8017315c(uint32_t mi, uint32_t id);
extern "C" void GX__LoadNrmMtxIndx3x3_801731e0(uint32_t mi, uint32_t id);

PPC_NATIVE_OVERRIDE_VOID(801C9AD0, GX__LoadPosMtxIndx_8017315c, (uint32_t mi, uint32_t id), (mi, id));
PPC_NATIVE_OVERRIDE_VOID(801C9B60, GX__LoadNrmMtxIndx3x3_801731e0, (uint32_t mi, uint32_t id), (mi, id));
