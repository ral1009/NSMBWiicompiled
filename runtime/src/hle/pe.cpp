// GX Pixel Engine (PE) hardware register block: 0xCC001000-0xCC001017ish, 16-bit registers
// (PE_ZCONFIG, PE_ALPHACONFIG, PE_DSTALPHACONFIG, PE_ALPHAMODE, PE_ALPHAREAD, the interrupt token
// registers, PE_COPYCLEAR values, etc). Same situation as cp.cpp: this is Revolution GX library
// territory, not OS library, so NSMBW-Decomp's OSHardware.h doesn't name or offset it - the layout
// below follows the same long-standing, publicly-published GameCube/Wii hardware documentation used
// for cp.cpp, not a project-verified source.
//
// First hit: NSMBW's dSys_c::create() chain (func_800E4C50 -> func_802BC850 -> func_802BC960 ->
// func_801C1EF0 -> func_801C53C0) does a read-modify-write on PE+0xA (read, OR in 0xF, write back) -
// a one-shot config-bits-enable, not a poll loop (confirmed by reading func_801C53C0.cpp directly:
// no branch on the read value). Same rationale as cp.cpp: this runtime's actual rendering path
// doesn't depend on live PE register values, so faithful flat storage (default 0, OR-in gives 0xF,
// exactly what real hardware would end up with from a freshly-reset register) is enough. If
// something later depends on a real PE interrupt/token value, that is a separate, evidence-driven
// reason to add real semantics.
#include "memory_access.h"

#include <array>
#include <mutex>

namespace {
constexpr uint32_t kPeBase = 0xCC001000u;
constexpr uint32_t kPeRegCount = 0x0Cu; // covers PE_ZCONFIG..the interrupt token registers
constexpr uint32_t kPeSize = kPeRegCount * 2u;

std::mutex g_peMutex;
std::array<uint16_t, kPeRegCount> g_peRegs{};
} // namespace

extern "C" bool PE_HLE_TryRead16(uint32_t addr, uint16_t* outValue) {
    if (addr < kPeBase || addr >= kPeBase + kPeSize || (addr & 1u) != 0 || outValue == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_peMutex);
    *outValue = g_peRegs[(addr - kPeBase) / 2u];
    return true;
}

extern "C" bool PE_HLE_TryWrite16(uint32_t addr, uint16_t value) {
    if (addr < kPeBase || addr >= kPeBase + kPeSize || (addr & 1u) != 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_peMutex);
    g_peRegs[(addr - kPeBase) / 2u] = value;
    return true;
}
