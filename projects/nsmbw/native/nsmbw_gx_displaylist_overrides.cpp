// NSMBW bindings for GXBeginDisplayList / GXEndDisplayList.
//
// Why these two need native overrides (the same dead-override class as GXInit, the texture
// copies and the locked-cache DMA before them):
//
// On hardware, GXBeginDisplayList re-points the CPU FIFO at a RAM buffer, so everything the
// guest writes to the gather pipe (0xCC008000) until GXEndDisplayList lands in that buffer
// instead of the GPU. This runtime routes every gather-pipe store into HleFifoWrite
// (runtime/src/hle/gx/gx_fifo.cpp), which only diverts into the guest buffer while
// g_dlRecordState.active - and only MKW's bound GX__BeginDisplayList_80172e00 ever set that.
// NSMBW's copies ran as translated code, so:
//   1. every command the guest meant to *record* was executed live (the FIFO_DESYNC ring in
//      build_nsmbw/nsmbw_fifodesync6.err.log:203-230 shows draws with LR=0x801C9690, the return
//      address inside GXEndDisplayList right after its GXFlush call), and
//   2. the buffer the guest later hands to GXCallDisplayList held stale memory - garbage CP writes
//      cleared POS from the VCD ("[aurora] unmapped vtx attr 9", aurora shader.cpp:575, the
//      intro-cutscene crash) and float bytes were dereferenced as pointers (the ~98
//      "GX guest pointer: memory error" lines per run).
//
// Addresses and constants come from the translated bodies in
// generated_nsmbw/build_shards/nsmbw_all/shard_c2debdb1cc9573e73c221177.cpp:
//   func_801C95B0 (GXBeginDisplayList): reads __gx+0x5FC -> bl 0x801C5430 (__GXSetDirtyState);
//     reads __gx+0x5F9 -> memcpy(0x80390850, __gx, 0x600); fills the GXFifoObj at 0x803907D0
//     (lis 0x8039 / addi 2000): +0 base, +4 base+size-4, +8 size, +0x14/+0x18 rd/wr = base,
//     +0x1C count = 0; writes __gx+0x5F8 = 1; then GXFlush / GXGetCPUFifo(0x80390E50) /
//     GXSetCPUFifo(0x803907D0).
//   func_801C9670 (GXEndDisplayList): GXFlush; GXGetCPUFifo(0x803907D0); wrapped = [0x803907F0];
//     GXSetCPUFifo(0x80390E50); if __gx+0x5F9: memcpy(__gx, 0x80390850, 0x600) keeping __gx+8;
//     __gx+0x5F8 = 0; return wrapped ? 0 : [0x803907EC] (count).
// The guest FIFO swap (GXGetCPUFifo/GXSetCPUFifo) is deliberately NOT reproduced: the host owns
// the redirect through g_dlRecordState, and NSMBW's GX init never went through the host's fifo
// objects in the first place (GXInit is translated here, see the 2026-09-04 progress-log entry).
//
// __gx is *(r2 - 0x4EF8) = *(0x8042E468), as in nsmbw_gx_overrides.cpp.
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void GxHle_BeginDisplayListRecording(uint32_t listAddr, uint32_t sizeBytes, uint32_t fifoObjAddr);
extern "C" void GxHle_EndDisplayListRecording();

namespace {
constexpr uint32_t kGxDataPtrAddr = 0x80433360u - 0x4EF8u;
constexpr uint32_t kSetDirtyStateAddr = 0x801C5430u;
constexpr uint32_t kDlFifoObjAddr = 0x803907D0u;   // GXFifoObj DisplayListFifo
constexpr uint32_t kDlStateSaveAddr = 0x80390850u; // 0x600-byte __GXData save area
constexpr uint32_t kGxDataSize = 0x600u;
constexpr uint32_t kFifoRdPtrOff = 0x14u;
constexpr uint32_t kFifoWrPtrOff = 0x18u;
constexpr uint32_t kFifoCountOff = 0x1Cu;
constexpr uint32_t kFifoWrapOff = 0x20u;
constexpr uint32_t kGxInDispListOff = 0x5F8u;
constexpr uint32_t kGxDlSaveContextOff = 0x5F9u;
constexpr uint32_t kGxDirtyStateOff = 0x5FCu;

bool LogEnabled() {
    static const bool enabled = std::getenv("NSMBW_LOG_DISPLAY_LIST") != nullptr;
    return enabled;
}

uint32_t ReadGxData() {
    uint32_t gd = 0;
    return Memory::TryRead32(kGxDataPtrAddr, gd) ? gd : 0u;
}
} // namespace

extern "C" void NsmbwBeginDisplayList_801C95B0(uint32_t list, uint32_t size)
{
    const uint32_t gd = ReadGxData();
    try {
        if (gd != 0) {
            // Pending cached state (projection, VCD/VAT, ...) must reach the GPU before recording
            // starts, exactly as the guest did it; its FIFO writes are parsed live here.
            uint32_t dirty = 0;
            if (Memory::TryRead32(gd + kGxDirtyStateOff, dirty) && dirty != 0) {
                auto& cpu = GetPersistentCpuContext();
                InvokeIndirectCpu(kSetDirtyStateAddr, &cpu);
            }
            if (Memory::Read8(gd + kGxDlSaveContextOff) != 0) {
                std::memcpy(Memory::GetPointer(kDlStateSaveAddr, kGxDataSize),
                            Memory::GetPointer(gd, kGxDataSize), kGxDataSize);
            }
        }
        // Keep the guest-visible fifo object in the state the SDK leaves it in.
        Memory::Write32(kDlFifoObjAddr + 0x0u, list);
        Memory::Write32(kDlFifoObjAddr + 0x4u, list + size - 4u);
        Memory::Write32(kDlFifoObjAddr + 0x8u, size);
        Memory::Write32(kDlFifoObjAddr + kFifoRdPtrOff, list);
        Memory::Write32(kDlFifoObjAddr + kFifoWrPtrOff, list);
        Memory::Write32(kDlFifoObjAddr + kFifoCountOff, 0);
        // The recorder sets this on overflow and nothing else ever clears it; the SDK's fifo
        // init path would. Clear per list so one overflow doesn't poison every later list.
        Memory::Write8(kDlFifoObjAddr + kFifoWrapOff, 0);
        if (gd != 0) Memory::Write8(gd + kGxInDispListOff, 1);
    } catch (const Memory::AccessViolation&) {
    }
    GxHle_BeginDisplayListRecording(list, size, kDlFifoObjAddr);
    if (LogEnabled()) {
        static int logged = 0;
        if (logged < 40) {
            ++logged;
            std::fprintf(stderr, "[nsmbw][dl] GXBeginDisplayList(list=0x%08X size=%u)\n", list, size);
            std::fflush(stderr);
        }
    }
}
PPC_NATIVE_OVERRIDE_VOID(801C95B0, NsmbwBeginDisplayList_801C95B0, (uint32_t list, uint32_t size), (list, size));

extern "C" uint32_t NsmbwEndDisplayList_801C9670()
{
    // Publishes rdPtr/count into the fifo object at kDlFifoObjAddr and stops the redirect.
    GxHle_EndDisplayListRecording();
    uint32_t count = 0;
    uint8_t wrapped = 0;
    const uint32_t gd = ReadGxData();
    try {
        wrapped = Memory::Read8(kDlFifoObjAddr + kFifoWrapOff);
        count = Memory::Read32(kDlFifoObjAddr + kFifoCountOff);
        if (gd != 0) {
            if (Memory::Read8(gd + kGxDlSaveContextOff) != 0) {
                // The SDK restores everything except __gx+8 (the CP/BP "sent" bookkeeping word).
                const uint32_t keep = Memory::Read32(gd + 0x8u);
                std::memcpy(Memory::GetPointer(gd, kGxDataSize),
                            Memory::GetPointer(kDlStateSaveAddr, kGxDataSize), kGxDataSize);
                Memory::Write32(gd + 0x8u, keep);
            }
            Memory::Write8(gd + kGxInDispListOff, 0);
        }
    } catch (const Memory::AccessViolation&) {
    }
    const uint32_t result = wrapped == 0 ? count : 0u;
    if (LogEnabled()) {
        static int logged = 0;
        if (logged < 40) {
            ++logged;
            std::fprintf(stderr, "[nsmbw][dl] GXEndDisplayList -> %u bytes (wrapped=%u)\n", result, wrapped);
            std::fflush(stderr);
        }
    }
    return result;
}
PPC_NATIVE_OVERRIDE(801C9670, NsmbwEndDisplayList_801C9670, uint32_t, (), ());
