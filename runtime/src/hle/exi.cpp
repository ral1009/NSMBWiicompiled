// Expansion Interface (EXI) hardware register block: 0xCD006800-0xCD00684B, 3 channels of
// EXIChannelParam (cpr/mar/length/cr/data, 0x14 bytes each) - NSMBW-Decomp's
// include/lib/revolution/EXI/EXIHardware.h. Real EXI drives memory cards / RTC / etc.
//
// Only wired into the checked read/write path - see pi.cpp's comment for why. Flat storage is
// faithful for the CPR interrupt-mask bits (EXIINTMASK/TCINTMASK/EXTINTMASK) this runtime needs
// for early interrupt-controller bring-up; it does not emulate real EXI device transactions.
//
// EXIDma (func_801B8F90 in NSMBW, called from func_801B3F60's EXI channel 0/device 1 probe -
// EXIBios.h's `BOOL EXIDma(EXIChannel, void*, s32, u32, ...)`) is the one DMA-mode caller this
// runtime has actually reached: it programs mar/length, then writes cr with TSTART=1, DMA=1,
// RW=EXI_READ(0) to pull 64 bytes from the RTC/SRAM device. Real hardware clocks that out
// asynchronously and clears TSTART on completion, then fires TCINT if unmasked; this runtime has
// no device timing model, so DMA completion is synchronous here too, same as the immediate/PIO
// case below - it just also has a destination buffer (mar/length) to fill instead of a single
// DATA register. For an EXI_READ, that buffer is zero-filled (no real RTC/SRAM chip behind this
// - matches this file's existing "no real hardware, evidence-backed default" precedent, e.g.
// DI_CONFIG/VI_VICLK's 0 defaults; the SDK's own SRAM checksum validation is written to fall back
// to hardcoded defaults on exactly this "blank/uninitialized" case, so it isn't a guess). An
// EXI_WRITE/EXI_TYPE_2 DMA leaves the data untouched - nothing reads it back, matching the
// "write to an unmodeled device just vanishes" precedent already noted in
// projects/nsmbw/native/nsmbw_exi_imm_start.cpp. If execution later demonstrates NSMBW actually
// depends on specific SRAM/RTC field values (not just "DMA completes"), that is the signal to
// build real device content, not something to guess at here.
#include "guest_flat_memory.h"
#include "memory_access.h"

#include <array>
#include <cstring>
#include <mutex>

namespace {
constexpr uint32_t kExiBase = 0xCD006800u;
constexpr uint32_t kExiChannelCount = 3u;
constexpr uint32_t kExiChannelStride = 0x14u;
constexpr uint32_t kExiSize = kExiChannelCount * kExiChannelStride;

// EXIChannelParam field offsets within one channel block, and the CR bits this file cares
// about - all from NSMBW-Decomp's include/lib/revolution/EXI/EXIHardware.h.
constexpr uint32_t kExiMarOffset = 0x4u;
constexpr uint32_t kExiLengthOffset = 0x8u;
constexpr uint32_t kExiCrOffset = 0xCu;
constexpr uint32_t kExiCrTstart = 1u << 0; // EXI_CR_TSTART
constexpr uint32_t kExiCrDma = 1u << 1;    // EXI_CR_DMA
constexpr uint32_t kExiCrRwMask = (1u << 2) | (1u << 3); // EXI_CR_RW
constexpr uint32_t kExiCrRwRead = 0u; // EXIType::EXI_READ, EXICommon.h

std::mutex g_exiMutex;
std::array<uint32_t, kExiSize / 4u> g_exiRegs{};
} // namespace

extern "C" bool EXI_HLE_TryRead32(uint32_t addr, uint32_t* outValue) {
    if (addr < kExiBase || addr >= kExiBase + kExiSize || (addr & 3u) != 0 || outValue == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_exiMutex);
    *outValue = g_exiRegs[(addr - kExiBase) / 4u];
    return true;
}

extern "C" bool EXI_HLE_TryWrite32(uint32_t addr, uint32_t value) {
    if (addr < kExiBase || addr >= kExiBase + kExiSize || (addr & 3u) != 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_exiMutex);
    // Both immediate (PIO) and DMA EXI transfers are synchronous on real hardware from
    // software's point of view by the time it can poll TSTART back (PIO: a hardware shift
    // register; DMA: this runtime has no timing/TCINT model, so it completes on the same write
    // that starts it) - see this file's header comment for the DMA case's rationale.
    if ((addr - kExiBase) % kExiChannelStride == kExiCrOffset && (value & kExiCrTstart) != 0) {
        if ((value & kExiCrDma) != 0) {
            const uint32_t channelRegBase = (addr - kExiBase) / 4u - kExiCrOffset / 4u;
            const uint32_t mar = g_exiRegs[channelRegBase + kExiMarOffset / 4u];
            const uint32_t length = g_exiRegs[channelRegBase + kExiLengthOffset / 4u];
            if ((value & kExiCrRwMask) == kExiCrRwRead && length != 0u) {
                std::memset(MKW_FLAT_GUEST_BASE + mar, 0, length);
            }
        }
        value &= ~kExiCrTstart;
    }
    g_exiRegs[(addr - kExiBase) / 4u] = value;
    return true;
}
