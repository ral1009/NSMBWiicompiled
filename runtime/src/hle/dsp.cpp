// DSP interface hardware register block: 0xCC005000-0xCC00503B, 16-bit registers
// (NSMBW-Decomp's include/lib/revolution/DSP/dsp_hardware.h: DSP_HW_REGS[], DSPHwReg enum).
// Covers the DSP/CPU mailboxes, DSP_CSR (control/status - includes per-source interrupt mask
// bits DSPINTMSK/ARINTMSK/AIDINTMSK), ARAM DMA registers, and AI DMA registers.
//
// Only wired into the checked read/write path (Read16Slow/Write16Slow in memory.cpp), not the raw
// flat-memory path - see pi.cpp's comment for why. Flat storage is faithful for the mask bits this
// runtime needs (nothing here models a real DSP core, ARAM, or audio DMA); if execution later
// demonstrates a real mailbox/DMA-complete read is needed, that is the signal to build actual
// audio HLE, not something to guess at here.
#include "memory_access.h"

#include <array>
#include <mutex>

namespace {
constexpr uint32_t kDspBase = 0xCC005000u;
constexpr uint32_t kDspRegCount = 30u; // through DSP_AI_DMA_BYTES_LEFT @ 0xCC00503A
constexpr uint32_t kDspSize = kDspRegCount * 2u;

std::mutex g_dspMutex;
std::array<uint16_t, kDspRegCount> g_dspRegs{};
} // namespace

extern "C" bool DSP_HLE_TryRead16(uint32_t addr, uint16_t* outValue) {
    if (addr < kDspBase || addr >= kDspBase + kDspSize || (addr & 1u) != 0 || outValue == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_dspMutex);
    *outValue = g_dspRegs[(addr - kDspBase) / 2u];
    return true;
}

extern "C" bool DSP_HLE_TryWrite16(uint32_t addr, uint16_t value) {
    if (addr < kDspBase || addr >= kDspBase + kDspSize || (addr & 1u) != 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_dspMutex);
    g_dspRegs[(addr - kDspBase) / 2u] = value;
    return true;
}
