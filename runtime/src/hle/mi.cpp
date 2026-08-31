// Memory Interface (MI) hardware register block: 0xCC004000-0xCC004027, 20 16-bit registers
// (NSMBW-Decomp's include/lib/revolution/OS/OSHardware.h: MI_HW_REGS[20], MIHwReg enum). Accessed
// as 16-bit halfwords on real hardware, matching the SDK's `volatile u16 MI_HW_REGS[20]`.
//
// Only wired into the checked write path - see pi.cpp's comment for why reads of this range need
// a function-level HLE override instead, once something actually needs a real read to succeed.
//
// Flat write storage is faithful for the page/protection/interrupt-mask registers this runtime
// doesn't need to act on, since nothing here models real DRAM paging/protection faults at the MI
// level. MI_INTSR (interrupt cause) is write-1-to-clear like PI_INTSR; everything else is plain.
#include "memory_access.h"

#include <array>
#include <mutex>

namespace {
constexpr uint32_t kMiBase = 0xCC004000u;
constexpr uint32_t kMiRegCount = 20u;
constexpr uint32_t kMiSize = kMiRegCount * 2u;
constexpr uint32_t kMiIntsrIndex = 15u; // MI_INTSR (0xCC00401E) - write-1-to-clear

std::mutex g_miMutex;
std::array<uint16_t, kMiRegCount> g_miRegs{};
} // namespace

extern "C" bool MI_HLE_TryRead16(uint32_t addr, uint16_t* outValue) {
    if (addr < kMiBase || addr >= kMiBase + kMiSize || (addr & 1u) != 0 || outValue == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_miMutex);
    *outValue = g_miRegs[(addr - kMiBase) / 2u];
    return true;
}

extern "C" bool MI_HLE_TryWrite16(uint32_t addr, uint16_t value) {
    if (addr < kMiBase || addr >= kMiBase + kMiSize || (addr & 1u) != 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_miMutex);
    const uint32_t index = (addr - kMiBase) / 2u;
    if (index == kMiIntsrIndex) {
        g_miRegs[index] &= static_cast<uint16_t>(~value);
    } else {
        g_miRegs[index] = value;
    }
    return true;
}
