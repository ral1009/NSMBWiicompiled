// Native bindings for NSMBW's EFB->texture copy functions.
//
// Root cause of the "narrow vertical strip in a black frame" symptom (found by pushing a hand-
// written test triangle through the pipeline and peeking the GPU at each stage: the triangle was
// correct in the EFB, but the display-copy resolve target came out 143px wide instead of 640).
//
// BP registers 0x49/0x4A (copy source rect) and 0x4D (copy destination stride) are shared between
// display copies and texture copies on real hardware; the SDK just rewrites them right before each
// copy from per-kind cached values. GXSetTexCopySrc/GXSetTexCopyDst cache their words in the __gx
// struct, and GXCopyTex flushes them to the FIFO immediately before issuing the copy (confirmed in
// this build's disassembly: 0x801C5AA0 stores to __gx+0x240/+0x244, 0x801C63D0 flushes them).
//
// None of these three were natively bound for NSMBW, so they ran as auto-translated guest code and
// those raw register writes reached aurora's BP decoder - which stores 0x49/0x4A into
// g_gxState.dispCopySrc and 0x4D into g_gxState.dispCopyDstWidth (it has no notion of the two copy
// kinds sharing registers). GXCopyDisp IS natively bound (nsmbw_gx_overrides.cpp), and aurora's
// GXCopyDisp reads that state directly instead of re-flushing the guest's cached display-copy
// words the way the real SDK function would. So from the first frame that does any texture copy
// (STAGE onward - the 32x32 HUD copies), every full-screen resolve used the last texture copy's
// 32x32 source rect and stride: dispCopySrc=(0,488,32x32) dstW=143, later (608,456,32x32) dstW=271.
//
// Binding them to the runtime's existing HLE wrappers (the same ones MKW uses) keeps those writes
// in aurora's texCopy* state where they belong, so the display-copy geometry is never touched.
// Same pattern as every other entry in nsmbw_gx_overrides.cpp. New addresses -> shard manifest
// regen required (Translator.Cli emit-nsmbw-build-shards).
#include "hle_stubs.h"
#include "ppc_runtime.h"

extern "C" void GX__SetTexCopySrc_8016f478(uint32_t l, uint32_t t, uint32_t w, uint32_t h);
extern "C" void GX__SetTexCopyDst_8016f4dc(uint32_t w, uint32_t h, uint32_t f, uint32_t m);
extern "C" void GX__CopyTex_8016fd74(uint32_t da, uint32_t c);

PPC_NATIVE_OVERRIDE_VOID(801C5AA0, GX__SetTexCopySrc_8016f478, (uint32_t l, uint32_t t, uint32_t w, uint32_t h), (l, t, w, h));
PPC_NATIVE_OVERRIDE_VOID(801C5B10, GX__SetTexCopyDst_8016f4dc, (uint32_t w, uint32_t h, uint32_t f, uint32_t m), (w, h, f, m));
PPC_NATIVE_OVERRIDE_VOID(801C63D0, GX__CopyTex_8016fd74, (uint32_t da, uint32_t c), (da, c));
