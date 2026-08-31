// PPC-side IPC (inter-processor communication with the Wii's ARM/IOS core) hardware register
// block: 0xCD000000-0xCD0003FF (NSMBW-Decomp's include/lib/revolution/IPC/ipcHardware.h:
// IPC_HW_REGS_PPC[256]). Covers IPC_PPCMSG/PPCCTRL/ARMMSG/ARMCTRL, TIMER, ALARM, the
// PPC/ARM IRQ flag+mask pairs, and the GPIO block from 0xCD0000C0.
//
// Only wired into the checked write path - see pi.cpp's comment for why reads of this range need
// a function-level HLE override instead, once something actually needs a real read to succeed.
//
// Modeled as flat write storage only, same reasoning as pi.cpp: nothing in this runtime currently
// emulates the real ARM/IOS core or drives actual mailbox communication, so registers like
// PPCIRQMASK just need to hold what the guest wrote. PPCCTRL/PPCMSG are the real IPC command
// mailbox and are NOT faithfully emulated by flat storage - if guest code blocks waiting for an
// IOS reply bit this will never set, that will surface as a hang, not a crash, and is the real
// signal to build actual IOS/ARM-side emulation (Phase 6 priority 5), not something to guess at
// preemptively here.
#include "memory_access.h"

#include <array>
#include <mutex>

namespace {
constexpr uint32_t kIpcBase = 0xCD000000u;
constexpr uint32_t kIpcRegCount = 256u;
constexpr uint32_t kIpcSize = kIpcRegCount * 4u;

std::mutex g_ipcMutex;
std::array<uint32_t, kIpcRegCount> g_ipcRegs{};
} // namespace

extern "C" bool IPC_HLE_TryRead32(uint32_t addr, uint32_t* outValue) {
    if (addr < kIpcBase || addr >= kIpcBase + kIpcSize || (addr & 3u) != 0 || outValue == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_ipcMutex);
    *outValue = g_ipcRegs[(addr - kIpcBase) / 4u];
    return true;
}

extern "C" bool IPC_HLE_TryWrite32(uint32_t addr, uint32_t value) {
    if (addr < kIpcBase || addr >= kIpcBase + kIpcSize || (addr & 3u) != 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_ipcMutex);
    g_ipcRegs[(addr - kIpcBase) / 4u] = value;
    return true;
}
