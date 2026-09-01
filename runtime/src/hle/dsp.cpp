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
//
// Two completion signals ARE modeled below, both traced to func_801AC2C0 (NSMBW's one-time DSP
// bring-up, DSPInit-shaped) hanging on a hardware handshake this runtime can't produce with no
// real DSP core:
//   - DSP_CSR bit 0 (undocumented in dsp_hardware.h's partial enum - bits 1-11 are named, 0 and 10
//     are gaps) is written then polled-until-clear twice in that function, in the exact
//     write-then-spin shape already fixed for EXI's TSTART (exi.cpp) - a real DSP core clears this
//     reset-pulse bit once reset completes; ours has none, so it self-clears on write instead.
//   - DSP_CPUMBOX_H bit 15 (DSP_CPUMBOX_H_STATUS, actually named in dsp_hardware.h) is polled by
//     that same function waiting for the DSP to signal "mail available" after being taken out of
//     HALT. Traced the code immediately after that wait: it reads DSP_CPUMBOX_L (the mail payload)
//     and overwrites the register holding it before ever using it - the content is provably
//     discarded, only the presence flag matters here. So reads report the status bit set, with no
//     payload synthesized - not a mailbox protocol implementation, just satisfying the one flag
//     this call site actually checks.
//     IMPORTANT: this bit is polled TWICE in func_801AC2C0, for OPPOSITE transitions - once early
//     (still HALTed) waiting for it to be CLEAR (drain any stale mail before releasing HALT), and
//     again later (HALT just released) waiting for it to become SET. An unconditional "always
//     report set" was tried first and deadlocked the EARLIER wait instead (confirmed via a
//     temporary CSR-write/CPUMBOX-read log: CSR's reset pulse cleared correctly, then CPUMBOX_H
//     read in a tight loop, forced 0x8000 every time, never satisfying "wait for clear"). The
//     real hardware precondition is simple and already tracked here: the DSP can't have anything
//     to say while HALTed, so the status bit is only forced set once DSP_CSR's HALT bit (bit 2,
//     named in dsp_hardware.h) is clear - this is reading state this file already stores
//     faithfully, not modeling DSP execution.
//     If execution later demonstrates real mailbox CONTENT is needed (DSPAddTask/
//     DSPCheckMailFromDSP audio task submission), that is a different, separate signal to build
//     actual mailbox semantics, not something to extend from this.
//
// 32-bit accessors: DSP_HW_REGS is declared u16[] in the real SDK, but a `stw`/`lwz` to an aligned
// register pair (e.g. DSP_AR_DMA_MMADDR_H/L at 0x20/0x22) is ordinary PowerPC big-endian hardware
// access, not a special case - the high 16 bits land in the lower-addressed ("_H") register, the
// low 16 bits in the higher-addressed ("_L") one, exactly matching the SDK's own naming. Traced to
// func_801AC2C0's ARAM-DMA setup (FlatWrite32 to AR_DMA_MMADDR_H/ARADDR_H/CNT_H) crashing with
// "MMIO write blocked" because only the 16-bit path was wired in - this just widens the same
// passive register storage to also accept 4-byte-aligned access, reusing Read16/Write16's own
// special-casing (CSR bit 0, CPUMBOX_H status) for whichever half of a 32-bit access touches them.
#include "memory_access.h"
#include "runtime_log.h"

#include <array>
#include <atomic>
#include <mutex>

namespace {
constexpr uint32_t kDspBase = 0xCC005000u;
constexpr uint32_t kDspRegCount = 30u; // through DSP_AI_DMA_BYTES_LEFT @ 0xCC00503A
constexpr uint32_t kDspSize = kDspRegCount * 2u;

constexpr uint32_t kDspCpuMboxHOffset = 0x4u;    // DSP_CPUMBOX_H
constexpr uint16_t kDspCpuMboxHStatus = 1u << 15; // DSP_CPUMBOX_H_STATUS, dsp_hardware.h
constexpr uint32_t kDspCsrOffset = 0xAu;          // DSP_CSR
constexpr uint16_t kDspCsrResetPulse = 1u << 0;   // undocumented reset-trigger bit, see file comment
constexpr uint16_t kDspCsrHalt = 1u << 2;         // DSP_CSR_HALT, dsp_hardware.h

std::mutex g_dspMutex;
std::array<uint16_t, kDspRegCount> g_dspRegs{};

// TEMPORARY diagnostic for verifying func_801AC2C0's DSP bring-up sequence actually progresses
// past its polling loops instead of just moving which loop it's stuck in (none of those loops make
// further translated calls, so the DiagRecentCalls ring buffer in abi_bridge.h can't see them).
// Remove once the DSP bring-up hang investigation is confirmed resolved.
std::atomic<uint32_t> g_diagCsrWrites{0};
std::atomic<uint32_t> g_diagMboxReads{0};
constexpr uint32_t kDiagMaxLogs = 20;

// Shared by both the 16-bit and 32-bit accessors below - callers must already hold g_dspMutex and
// have validated addr is a register offset within range. `addr` here is the absolute guest address
// of the specific 16-bit half being touched (for a 32-bit access, called once per half).
uint16_t ReadOneRegLocked(uint32_t addr) {
    uint16_t value = g_dspRegs[(addr - kDspBase) / 2u];
    if (addr == kDspBase + kDspCpuMboxHOffset) {
        const uint16_t csr = g_dspRegs[kDspCsrOffset / 2u];
        const bool halted = (csr & kDspCsrHalt) != 0;
        if (!halted) {
            value |= kDspCpuMboxHStatus;
        }
        const uint32_t n = g_diagMboxReads.fetch_add(1, std::memory_order_relaxed);
        if (n < kDiagMaxLogs) {
            RT_LOGF(RT_TAG_OS, "dsp diag: CPUMBOX_H read #%u -> 0x%04X (halted=%d)\n", n, value, halted ? 1 : 0);
        }
    }
    return value;
}

void WriteOneRegLocked(uint32_t addr, uint16_t value) {
    if (addr == kDspBase + kDspCsrOffset) {
        const uint32_t n = g_diagCsrWrites.fetch_add(1, std::memory_order_relaxed);
        if (n < kDiagMaxLogs) {
            RT_LOGF(RT_TAG_OS, "dsp diag: CSR write #%u value=0x%04X (bit0 %s)\n", n, value,
                     (value & kDspCsrResetPulse) ? "set->cleared" : "clear");
        }
        value &= ~kDspCsrResetPulse;
    }
    g_dspRegs[(addr - kDspBase) / 2u] = value;
}
} // namespace

extern "C" bool DSP_HLE_TryRead16(uint32_t addr, uint16_t* outValue) {
    if (addr < kDspBase || addr >= kDspBase + kDspSize || (addr & 1u) != 0 || outValue == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_dspMutex);
    *outValue = ReadOneRegLocked(addr);
    return true;
}

extern "C" bool DSP_HLE_TryWrite16(uint32_t addr, uint16_t value) {
    if (addr < kDspBase || addr >= kDspBase + kDspSize || (addr & 1u) != 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_dspMutex);
    WriteOneRegLocked(addr, value);
    return true;
}

extern "C" bool DSP_HLE_TryRead32(uint32_t addr, uint32_t* outValue) {
    if (addr < kDspBase || addr + 4u > kDspBase + kDspSize || (addr & 3u) != 0 || outValue == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_dspMutex);
    const uint16_t hi = ReadOneRegLocked(addr);
    const uint16_t lo = ReadOneRegLocked(addr + 2u);
    *outValue = (static_cast<uint32_t>(hi) << 16) | lo;
    return true;
}

extern "C" bool DSP_HLE_TryWrite32(uint32_t addr, uint32_t value) {
    if (addr < kDspBase || addr + 4u > kDspBase + kDspSize || (addr & 3u) != 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_dspMutex);
    WriteOneRegLocked(addr, static_cast<uint16_t>(value >> 16));
    WriteOneRegLocked(addr + 2u, static_cast<uint16_t>(value & 0xFFFFu));
    return true;
}
