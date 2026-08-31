// Expansion Interface (EXI) hardware register block: 0xCD006800-0xCD00684B, 3 channels of
// EXIChannelParam (cpr/mar/length/cr/data, 0x14 bytes each) - NSMBW-Decomp's
// include/lib/revolution/EXI/EXIHardware.h. Real EXI drives memory cards / RTC / etc.
//
// Only wired into the checked read/write path - see pi.cpp's comment for why. Flat storage is
// faithful for the CPR interrupt-mask bits (EXIINTMASK/TCINTMASK/EXTINTMASK) this runtime needs
// for early interrupt-controller bring-up; it does not emulate real EXI device transactions. If
// execution later demonstrates a real EXI transfer/status read is needed, that is the signal to
// build actual EXI device HLE, not something to guess at here.
#include "memory_access.h"

#include <array>
#include <mutex>

namespace {
constexpr uint32_t kExiBase = 0xCD006800u;
constexpr uint32_t kExiChannelCount = 3u;
constexpr uint32_t kExiChannelStride = 0x14u;
constexpr uint32_t kExiSize = kExiChannelCount * kExiChannelStride;

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
    g_exiRegs[(addr - kExiBase) / 4u] = value;
    return true;
}
