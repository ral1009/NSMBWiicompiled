// DVD Interface (DI) hardware register block: 0xCD006000-0xCD006027, 10 32-bit registers
// (NSMBW-Decomp's include/lib/revolution/OS/OSHardware.h: DI_HW_REGS[10]). Real hardware always
// has a genuine DI_CONFIG value (a DVD drive interface exists on every real console); code that
// reads it here (e.g. inside EXI channel-probe helpers, checking DI_CONFIG's low byte against the
// specific sentinel 0xFF) branches the same way whether that byte is a real board-revision value
// or this runtime's zero default, since 0 != 0xFF either way - confirmed by tracing the actual
// branch, not assumed.
//
// Only wired into the checked read/write path - see pi.cpp's comment for why. Flat storage does
// not emulate a real DVD drive; if execution later demonstrates a real DI command/status read is
// needed (actual disc access), that is the signal to build actual DI HLE, not something to guess
// at here.
//
// This file is shared runtime (compiled into both the MKW and NSMBW builds - see
// NsmbwProduct.cmake's nsmbw_runtime_common glob), so it only holds the game-agnostic register
// block. A genuine raw-flat-path read of DI_CONFIG does exist in NSMBW (0x801AB2F0's 3-instruction
// leaf, reached from func_801B9090/func_801BA7B0's EXI channel-probe helpers) and needs a
// function-level PPC_NATIVE_OVERRIDE_VOID per hle/vi.cpp's pattern - but that address is only
// meaningful in NSMBW's address space, so the override itself lives in
// projects/nsmbw/native/nsmbw_di_config_probe.cpp, not here (registering a guest address from a
// file shared with MKW would hijack whatever real function occupies that address in MKW's binary).
#include "memory_access.h"

#include <array>
#include <mutex>

namespace {
constexpr uint32_t kDiBase = 0xCD006000u;
constexpr uint32_t kDiRegCount = 10u;
constexpr uint32_t kDiSize = kDiRegCount * 4u;

std::mutex g_diMutex;
std::array<uint32_t, kDiRegCount> g_diRegs{};
} // namespace

extern "C" bool DI_HLE_TryRead32(uint32_t addr, uint32_t* outValue) {
    if (addr < kDiBase || addr >= kDiBase + kDiSize || (addr & 3u) != 0 || outValue == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_diMutex);
    *outValue = g_diRegs[(addr - kDiBase) / 4u];
    return true;
}

extern "C" bool DI_HLE_TryWrite32(uint32_t addr, uint32_t value) {
    if (addr < kDiBase || addr >= kDiBase + kDiSize || (addr & 3u) != 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_diMutex);
    g_diRegs[(addr - kDiBase) / 4u] = value;
    return true;
}
