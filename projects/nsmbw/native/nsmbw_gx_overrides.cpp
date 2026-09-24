// NSMBW address bindings for the GX HLE in runtime/src/hle/gx/, which registers at Mario Kart
// Wii's addresses. Deliberately small: most of MKW's 341 GX registrations are NOT needed here.
//
// Why: runtime/include/memory_access.h's IsGpuFifoAddress routes every guest store into
// 0xCC008000-0xCC0080FF straight to GX_HLE_FIFO_Write*, and HleFifoWrite (gx_fifo.cpp) is a
// real GP command-stream decoder - BP/CP/XF register loads, indexed XF, CALL_DL and draw
// primitives. NSMBW's translated GX library (0x801C1D40-0x801CA708, 141 functions that write
// the gather pipe) therefore already drives Aurora correctly without any per-function binding.
// That is why boot reached GXDrawDone with zero GX overrides bound.
//
// Only the functions whose behaviour is NOT expressible in the FIFO stream need binding:
//
//   0x801C4FE0  GXDrawDone. Writes BP register 0x45 (PE_DONE, the literal 0x45000002 at
//               0x801C5008), clears the finish flag at r13-0x4DC0, then loops
//               `while (!flag) OSSleepThread(r13-0x4DC8)`. handle_bp in Aurora's command
//               processor has no case for BP 0x45, and nothing in this runtime delivers the
//               GP finish interrupt, so the guest would sleep forever. The HLE models the GP
//               draining and raising that interrupt, which is what the hardware does.
//
//   0x801C5340  GXFinishInterruptHandler. Sets gd->0x0A |= 8 and the finish flag at
//               r13-0x4DC0, then invokes the guest's registered finish callback - the same
//               three steps as GX__FinishInterruptHandler_8016ed94.
//
//   0x801C6290  GXCopyDisp. Identified by: second argument tested as the `clear` flag
//               (`cmpwi r4, 0` at 0x801C62C4), BP loads of the GXData copy fields at +0x220 /
//               +0x228 / +0x22C / +0x23C, and a closing `sth 0, 2(gd)` dirty-state clear. It
//               ends at 0x801C63C8, immediately before GXCopyTex (0x801C63D0), which itself
//               ends exactly at GXClearBoundingBox (0x801C6530 - independently confirmed as
//               the only function writing BP 0x55/0x56). Its size 0x140 matches MKW's 0x13C.
//               This is the function that increments g_gxFrameCount, marks the XFB ready and
//               paces to VI retrace; without it no frame is ever presented.
//
// Everything else is deliberately left translated. In particular the 205 GX_FATAL_STUB
// registrations are MKW-only inlined switch fragments - binding them here would abort.
//
// Lives here, not in the shared runtime tree, because the translator's override-skip detection
// only scans this project's native_registration_root (projects/nsmbw/native).
#include "hle_stubs.h"
#include "memory_access.h"
#include <aurora/env.hpp>
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"

#include <cstdint>

//   0x801C9720  GXCallDisplayList(list, nbytes). Flushes dirty state (0x801C5430, and 0x801C5800
//               when gd->0 is set), then writes FIFO byte 0x40 (GX_CMD_CALL_DL) followed by the
//               list address and byte count - the same shape as MKW's GX::CallDisplayList.
//               Left translated, that raw CALL_DL reaches Aurora's command processor, which
//               refuses display lists nested inside a display list ("Ignoring nested
//               GX_CMD_CALL_DL", 322+ per run and still climbing in the steady frame loop) and
//               drops the geometry. Routing it through the runtime's own gx_dl.cpp handler
//               executes the list here instead, where nesting is handled.
//   0x801C48C0  GXSetArray(attr, base, stride). Identified by its opening
//               `if (attr == 0x19) attr = 0xA;` (the SDK's GX_VA_NBT -> GX_VA_NRM
//               canonicalization, the same thing CanonicalVtxAttr does in the HLE), then CP
//               loads of register 0xA0+idx (array base) and 0xB0+idx (array stride); it is the
//               only function in NSMBW's GX library that writes both, and it has many callers.
//               This one CANNOT go through the FIFO decoder: Aurora's handle_cp answers a
//               0xA0-0xAF write with "CP_REG_ARRAYBASE_ID is not supported on Aurora. Use
//               GX_LOAD_AURORA_ARRAYBASE instead." - array bases have to arrive as host
//               pointers via the function-level HLE, which is why MKW binds it too.
//
//               Left unbound, every vertex array stayed base/stride/size 0. That is the first
//               bad state in the frame: indexed XF loads were skipped ("invalid source array:
//               array=24 ... stride=0 ... size=0", the earliest GX complaint in the log),
//               Aurora then computed a nonsense vertex size for the following draw, truncated
//               it ("need 74691 bytes at pos 3, have 32"), and drain() discarded the remainder -
//               which desynchronised the stream so the next drain began mid-vertex. The
//               "Ignoring nested GX_CMD_CALL_DL" messages were a symptom of exactly that: the
//               rejected byte was 0x41, a float fragment, not a real CALL_DL opcode.
extern "C" void GX__DrawDone_8016eab0();
extern "C" void GX__FinishInterruptHandler_8016ed94();
extern "C" void GX__CopyDisp_8016fc38(uint32_t destAddr, uint32_t clear);
extern "C" void GX__CallDisplayList_80172f64(uint32_t listAddr, uint32_t nbytes);
extern "C" void GX__SetArray_8016e32c(uint32_t attr, uint32_t base, uint32_t stride);
// GXLoadTexObj(GXTexObj* obj, GXTexMapID id): decodes the guest texture object and uploads its
// pixel data into Aurora's texture cache. Unlike CP/XF/BP register writes, this is not reproduced
// by the raw-FIFO forwarding path (gx_fifo.cpp -> GXCallDisplayList -> aurora::gx::fifo::process),
// because the actual pixel bytes never travel through the gather pipe as GX register writes - only
// a hardware-form pointer/format descriptor does. Without this binding NSMBW's GXLoadTexObj
// (found at 0x801C7600 via projects/nsmbw/function_map.txt) runs as plain translated PowerPC and
// never reaches Aurora's host-side upload, so every textured draw samples an unbound/placeholder
// texture. Confirmed live: func_801C7600 is hit ~7000+ times per boot while g_boundTexMaps never
// gets populated for NSMBW.
extern "C" void GX__LoadTexObj_80170f2c(uint32_t oa, uint32_t tid);
// PE state setters (blend/color-update/alpha-update/z-mode): like LoadTexObj, MKW HLEs these
// natively rather than relying on raw-BP forwarding, so the raw path alone is not sufficient here
// either. Confirmed live: NSMBW's own addresses (found via function_map.txt) are hit thousands of
// times per boot (BLEND 4392, ZMODE 1495, COLORUPD 520, ALPHAUPD 34) while running as plain
// translated code, unbound.
extern "C" void GX__SetBlendMode_8017277c(uint32_t t, uint32_t s, uint32_t d, uint32_t op);
extern "C" void GX__SetColorUpdate_801727cc(uint32_t en);
extern "C" void GX__SetAlphaUpdate_801727f8(uint32_t en);
extern "C" void GX__SetZMode_80172824(uint32_t ce, uint32_t f, uint32_t ue);
extern "C" void GX__SetDither_80172930(uint32_t d);
// GXSetDispCopySrc/Dst: on real hardware these only cache a BP command word into a guest dirty-
// state struct ([SDA2-0x4ef8] -> +0x230/+0x234), flushed to the FIFO later by __GXSetDirtyState.
// Confirmed live: NSMBW's GXSetDispCopySrc (0x801C5A60) runs exactly once with correct args
// (x=0,y=0,w=640,h=480) and the computed BP words ARE cached at that struct address (0x49000000 /
// 0x4a071e7f read directly from guest memory) - but aurora::gx::g_gxState.dispCopySrc still reads
// {0,0,0,0} at every draw, so the guest-side flush of this field never reaches GXApplyBPReg. Same
// fix as LoadTexObj/blend/zmode: MKW HLEs this pair natively, writing Aurora's host state
// directly instead of depending on that guest flush, and NSMBW's addresses were never bound.
extern "C" void GX__SetDispCopySrc_8016f438(uint32_t l, uint32_t t, uint32_t w, uint32_t h);
extern "C" void GX__SetDispCopyDst_8016f4b8(uint32_t w, uint32_t h);

PPC_NATIVE_OVERRIDE_VOID(801C4FE0, GX__DrawDone_8016eab0, (), ());
PPC_NATIVE_OVERRIDE_VOID(801C5340, GX__FinishInterruptHandler_8016ed94, (), ());
PPC_NATIVE_OVERRIDE_VOID(801C6290, GX__CopyDisp_8016fc38, (uint32_t destAddr, uint32_t clear), (destAddr, clear));
// GXCallDisplayList must first flush the guest's pending GX dirty state.
//
// Root cause of "the 3D scene never gets its perspective projection or view matrix": NSMBW's
// GXSetProjection (0x801C9980), GXSetCurrentMtx and the other SDK setters that are not natively
// bound here do not write the FIFO. They cache into the __gx struct and set a bit in the
// dirty-state word at __gx+0x5FC; the SDK flushes that cache to the GPU inside
// __GXSetDirtyState (0x801C5430), which GXBegin and GXCallDisplayList call just before a draw.
// The translated GXBegin still does that (which is why 2D layout draws, all immediate-mode, keep
// getting their orthographic projections), but this native GXCallDisplayList replaced the guest's,
// and aurora's own dirty-state flush only knows about state set through aurora's API - so every
// display-list draw (every nw4r::g3d model) ran with whatever projection/matrix the LAST layout
// GXBegin had flushed: the 32x32 HUD ortho and a zero PnMtx0 (confirmed by NSMBW_LOG_STATE_RUNS:
// the main 640x352 viewport arrives, because GXSetViewport IS natively bound and applies
// immediately, while the projection under it stays the HUD ortho every frame).
//
// MKW's runtime stubs __GXSetDirtyState to just clear the flags, which is right for MKW because
// all of its setters are native and already applied. NSMBW still depends on the guest flush for
// its unbound setters, so this runs the guest's real __GXSetDirtyState first when anything is
// dirty: its raw FIFO writes are parsed synchronously by HleFifoWrite into aurora, in order,
// before the list is processed. __gx is at *(r2 - 0x4EF8) = *(0x8042E468) (r2 = 0x80433360 for
// this build).
extern "C" void NsmbwCallDisplayList_801C9720(uint32_t listAddr, uint32_t nbytes)
{
    constexpr uint32_t kGxDataPtrAddr = 0x80433360u - 0x4EF8u;
    constexpr uint32_t kSetDirtyStateAddr = 0x801C5430u;
    uint32_t gd = 0;
    if (Memory::TryRead32(kGxDataPtrAddr, gd) && gd != 0) {
        uint32_t dirty = 0;
        if (Memory::TryRead32(gd + 0x5FCu, dirty) && dirty != 0) {
            auto& cpu = GetPersistentCpuContext();
            InvokeIndirectCpu(kSetDirtyStateAddr, &cpu);
        }
    }
    GX__CallDisplayList_80172f64(listAddr, nbytes);
}
PPC_NATIVE_OVERRIDE_VOID(801C9720, NsmbwCallDisplayList_801C9720, (uint32_t listAddr, uint32_t nbytes), (listAddr, nbytes));
PPC_NATIVE_OVERRIDE_VOID(801C48C0, GX__SetArray_8016e32c, (uint32_t attr, uint32_t base, uint32_t stride), (attr, base, stride));
PPC_NATIVE_OVERRIDE_VOID(801C7600, GX__LoadTexObj_80170f2c, (uint32_t oa, uint32_t tid), (oa, tid));
PPC_NATIVE_OVERRIDE_VOID(801C8F00, GX__SetBlendMode_8017277c, (uint32_t t, uint32_t s, uint32_t d, uint32_t op), (t, s, d, op));
PPC_NATIVE_OVERRIDE_VOID(801C8F50, GX__SetColorUpdate_801727cc, (uint32_t en), (en));
PPC_NATIVE_OVERRIDE_VOID(801C8F80, GX__SetAlphaUpdate_801727f8, (uint32_t en), (en));
PPC_NATIVE_OVERRIDE_VOID(801C8FB0, GX__SetZMode_80172824, (uint32_t ce, uint32_t f, uint32_t ue), (ce, f, ue));
// GXSetDither shares BP 0x41 (cmode0) with GXSetBlendMode / GXSetColorUpdate / GXSetAlphaUpdate, all
// three bound natively above, so aurora's __gx->cmode0 is the copy those keep current. Left unbound,
// the guest body (cmode0 = (__GXData+0x220 & ~4) | dither << 2, then a raw BP write) re-sent the
// *guest's* copy of the whole register - which no native setter ever updates, so blend none and
// colour/alpha update off. Every liquid renderer routine (0x8000AFA0, 0x8000B530..0x8000CA50, reached
// from AC_BG_WATER/LAVA/POISON's m3d::proc_c draw slots) calls it after setting blend and colour
// update: NSMBW_LOG_LIQUID showed their draws with colorUpdate=0 alphaUpdate=0 blend=0, so water,
// lava and poison drew nothing. Same register-sharing trap as the viewport/scissor bookkeeping.
// DIAGNOSTIC A/B (NSMBW_DITHER_LEGACY=1): for callers outside the liquid renderer, reproduce the old
// behaviour - the guest body re-sending the guest's own cmode0 copy (__GXData+0x220) - to find which
// non-liquid GXSetDither caller changed appearance with this binding.
extern "C" void NsmbwSetDither_801C90D0(uint32_t d) {
    static const bool legacy = AURORA_ENV("NSMBW_DITHER_LEGACY") != nullptr;
    if (legacy) {
        const CpuContext* c = TryGetCpuContext();
        const uint32_t lr = c ? static_cast<uint32_t>(c->lr) : 0u;
        if (!(lr >= 0x8000AFA0u && lr < 0x8000D170u)) {
            uint32_t gd = 0;
            if (Memory::TryRead32(0x8042E468u, gd) && gd != 0) {
                uint32_t cmode0 = Memory::Read32(gd + 0x220u);
                cmode0 = (cmode0 & ~4u) | ((d & 1u) << 2);
                GX_HLE_FIFO_Write8(0x61);
                GX_HLE_FIFO_Write32(cmode0);
                Memory::Write32(gd + 0x220u, cmode0);
                Memory::Write16(gd + 2u, 0);
                return;
            }
        }
    }
    GX__SetDither_80172930(d);
}
PPC_NATIVE_OVERRIDE_VOID(801C90D0, NsmbwSetDither_801C90D0, (uint32_t d), (d));
PPC_NATIVE_OVERRIDE_VOID(801C5A60, GX__SetDispCopySrc_8016f438, (uint32_t l, uint32_t t, uint32_t w, uint32_t h), (l, t, w, h));
// GXSetDispCopyDst's NSMBW address is NOT bound here: the nearby candidate (0x801C5AE0) takes
// only one register argument (r3) in its translated disassembly, not the two GXSetDispCopyDst
// needs, so it is not confirmed to be that function and binding it under a 2-arg signature risks
// reading an unrelated register as the height argument. dispCopyDstWidth/Height already default
// to 640/480 (non-zero, correct), so this is not the field that was actually broken.
