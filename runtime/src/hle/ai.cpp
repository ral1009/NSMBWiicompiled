// Audio Interface (AI) hardware register block: 0xCD006C00-0xCD006C1F, 32-bit registers
// (NSMBW-Decomp's include/lib/revolution/AI/ai_hardware.h: AI_HW_REGS[], AICR/AIVR/AISCNT/AIIT).
// AI drives the actual audio-out sample clock/streaming, distinct from the DSP block in dsp.cpp.
//
// Only wired into the checked read/write path - see pi.cpp's comment for why. Flat storage is
// faithful for the AICR interrupt-mask bit this runtime needs for early interrupt-controller
// bring-up; it does not emulate real audio streaming/sample-count behavior. If execution later
// demonstrates a real AI streaming read is needed, that is the signal to build actual audio HLE,
// not something to guess at here.
#include "memory_access.h"

#include <array>
#include <mutex>

namespace {
constexpr uint32_t kAiBase = 0xCD006C00u;
constexpr uint32_t kAiRegCount = 8u; // headroom past the 4 named registers
constexpr uint32_t kAiSize = kAiRegCount * 4u;

std::mutex g_aiMutex;
std::array<uint32_t, kAiRegCount> g_aiRegs{};
} // namespace

extern "C" bool AI_HLE_TryRead32(uint32_t addr, uint32_t* outValue) {
    if (addr < kAiBase || addr >= kAiBase + kAiSize || (addr & 3u) != 0 || outValue == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_aiMutex);
    *outValue = g_aiRegs[(addr - kAiBase) / 4u];
    return true;
}

extern "C" bool AI_HLE_TryWrite32(uint32_t addr, uint32_t value) {
    if (addr < kAiBase || addr >= kAiBase + kAiSize || (addr & 3u) != 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_aiMutex);
    g_aiRegs[(addr - kAiBase) / 4u] = value;
    return true;
}
