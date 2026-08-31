// Processor Interface (PI) hardware register block: 0xCC003000-0xCC003027, 10 32-bit registers
// (NSMBW-Decomp's include/lib/revolution/OS/OSHardware.h: PI_HW_REGS[10], PIHwReg enum).
//
// Only wired into the checked write path (Write32Slow in memory.cpp), not the raw flat-memory
// path: translated stores commonly go through the checked API (it also backs the executable-write
// guard), so this is reachable there. Translated *loads* of this range mostly use the raw,
// unchecked flat path instead (MemoryInline::FlatRead*) for speed - a fault there can't safely
// recover a value/width from a caught hardware exception (see memory.cpp's ThrowMmioReadBlocked
// comment), so a genuine read of these registers needs a function-level HLE override for whatever
// guest code performs it, the same pattern hle/vi.cpp uses for MKW's VI functions.
//
// Real hardware semantics are simple enough that flat write storage is faithful for now: PI_INTMR
// (per-device interrupt mask) only needs to hold whatever the guest last wrote, since this runtime
// never asserts real asynchronous hardware interrupts. PI_INTSR (interrupt cause) is write-1-to-
// clear on real hardware.
#include "memory_access.h"

#include <array>
#include <mutex>

namespace {
constexpr uint32_t kPiBase = 0xCC003000u;
constexpr uint32_t kPiRegCount = 10u;
constexpr uint32_t kPiSize = kPiRegCount * 4u;
constexpr uint32_t kPiIntsrIndex = 0u; // PI_INTSR - write-1-to-clear

std::mutex g_piMutex;
std::array<uint32_t, kPiRegCount> g_piRegs{};
} // namespace

extern "C" bool PI_HLE_TryRead32(uint32_t addr, uint32_t* outValue) {
    if (addr < kPiBase || addr >= kPiBase + kPiSize || (addr & 3u) != 0 || outValue == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_piMutex);
    *outValue = g_piRegs[(addr - kPiBase) / 4u];
    return true;
}

extern "C" bool PI_HLE_TryWrite32(uint32_t addr, uint32_t value) {
    if (addr < kPiBase || addr >= kPiBase + kPiSize || (addr & 3u) != 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_piMutex);
    const uint32_t index = (addr - kPiBase) / 4u;
    if (index == kPiIntsrIndex) {
        g_piRegs[index] &= ~value;
    } else {
        g_piRegs[index] = value;
    }
    return true;
}
