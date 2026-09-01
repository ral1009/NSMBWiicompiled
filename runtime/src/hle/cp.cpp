// GX Command Processor (CP) hardware register block: 0xCC000000-0xCC000042ish, 16-bit registers
// (CP_STATUS, CP_CTRL, CP_CLEAR, CP_TOKEN, CP_BP, and the FIFO base/end/watermark/read/write
// pointer pairs). Unlike PI/MI/DSP/VI, this block has no NSMBW-Decomp ground truth to cite - CP is
// part of the Revolution GX library, not the OS library NSMBW-Decomp's OSHardware.h documents, and
// nothing in that repo names or offsets it. The register layout and count below follow the
// long-standing, widely-published GameCube/Wii hardware documentation (YAGCD/WiiBrew-style CP
// register map), not a project-verified source - flagged here rather than presented as certain.
//
// First hit: NSMBW's dSys_c::create() (traced via lldb backtrace through
// func_800E4C50 -> func_802BC850 -> func_802BC960 -> func_801C1EF0 -> func_801C2FB0) writes 0 to
// CP_CTRL (0xCC000002) - a real hardware reset-before-reconfigure write, not a bug. This runtime's
// actual FIFO/rendering path is driven by intercepting gather-pipe writes directly
// (IsGpuFifoAddress in guest_flat_memory.cpp/memory.cpp), not by polling CP_CTRL's enable bits or
// the FIFO base/end/watermark pointers here, so - matching this codebase's own established
// philosophy for MI/PI (see those files' comments: "flat write storage is faithful... this runtime
// never asserts real asynchronous hardware interrupts") - this just stores whatever the guest
// writes and returns it faithfully on read, with no modeled side effects. If execution later
// demonstrates a real dependency on a specific CP register's live value (e.g. guest code polling
// CP_STATUS for a real FIFO-idle/breakpoint condition), that is a separate, evidence-driven reason
// to add one, not something to guess at now.
#include "memory_access.h"

#include <array>
#include <mutex>

namespace {
constexpr uint32_t kCpBase = 0xCC000000u;
constexpr uint32_t kCpRegCount = 0x22u; // covers CP_STATUS..CP_FIFO_BP_HI (0xCC000000-0xCC000042)
constexpr uint32_t kCpSize = kCpRegCount * 2u;

std::mutex g_cpMutex;
std::array<uint16_t, kCpRegCount> g_cpRegs{};
} // namespace

extern "C" bool CP_HLE_TryRead16(uint32_t addr, uint16_t* outValue) {
    if (addr < kCpBase || addr >= kCpBase + kCpSize || (addr & 1u) != 0 || outValue == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_cpMutex);
    *outValue = g_cpRegs[(addr - kCpBase) / 2u];
    return true;
}

extern "C" bool CP_HLE_TryWrite16(uint32_t addr, uint16_t value) {
    if (addr < kCpBase || addr >= kCpBase + kCpSize || (addr & 1u) != 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_cpMutex);
    g_cpRegs[(addr - kCpBase) / 2u] = value;
    return true;
}
