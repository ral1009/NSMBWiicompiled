// Hollywood-native hardware register block: 0xCD800000-0xCD8003FF, the Wii-only extended
// register set with no GameCube-Flipper equivalent (unlike ai.cpp/exi.cpp/ipc.cpp/pi.cpp, which
// each cover one Flipper-derived peripheral reachable through the classic 0xCC00xxxx compat alias
// or the 0xCD00xxxx native-Wii alias of that same physical Flipper bus - this block has neither).
//
// NSMBW's boot sequence (reached right after the EXI RTC/SRAM read - see exi.cpp) pokes this
// block directly: func_801AC0A0 runs a timed clock/PLL calibration sequence at offsets
// 0x180/0x1CC/0x1D0 (PPC_Mftb()-timed read-modify-write, no register-value polling), and
// func_801BE650/func_801BE9A0 bit-bang a GPIO serial write at offsets 0xC0/0xC4/0xC8 - real
// WiiBrew-documented Hollywood GPIOB_OUT/GPIOB_DIR/GPIOB_IN registers, clocked by the same
// Mftb-timed delay helper, with the one GPIOB_IN read feeding a single one-shot conditional
// rather than a retry loop.
//
// Modeled as flat read/write storage only, same reasoning as pi.cpp/ipc.cpp/ai.cpp: nothing in
// this runtime emulates the real external hardware these registers talk to (clock/PLL circuitry,
// whatever's wired to those GPIO pins), and nothing in the NSMBW boot code reached so far spins
// waiting on a specific bit value here, so a zero-initialized "unconfigured hardware" default
// cannot hang. If execution later demonstrates NSMBW depends on a specific value here (not just
// "the read succeeds"), that is the signal to build real semantics for that one register, not
// something to guess at here.
#include "memory_access.h"

#include <array>
#include <mutex>

namespace {
constexpr uint32_t kHwBase = 0xCD800000u;
constexpr uint32_t kHwRegCount = 256u;
constexpr uint32_t kHwSize = kHwRegCount * 4u;

std::mutex g_hwMutex;
std::array<uint32_t, kHwRegCount> g_hwRegs{};
} // namespace

extern "C" bool HW_HLE_TryRead32(uint32_t addr, uint32_t* outValue) {
    if (addr < kHwBase || addr >= kHwBase + kHwSize || (addr & 3u) != 0 || outValue == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_hwMutex);
    *outValue = g_hwRegs[(addr - kHwBase) / 4u];
    return true;
}

extern "C" bool HW_HLE_TryWrite32(uint32_t addr, uint32_t value) {
    if (addr < kHwBase || addr >= kHwBase + kHwSize || (addr & 3u) != 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_hwMutex);
    g_hwRegs[(addr - kHwBase) / 4u] = value;
    return true;
}
