// Serial Interface (SI) hardware register block: 0xCD006400-0xCD0064FF, 64 32-bit words
// (NSMBW-Decomp's include/lib/revolution/SI/SIHardware.h, SIHwReg enum). The first 16 words
// (0xCD006400-0xCD00643F) are the named registers: four channels' worth of OUTBUF/INBUFH/INBUFL,
// then SIPOLL/SICOMCSR/SISR/SIEXILK. The header's own SI_RAM_BASE constant places "SI
// communication RAM" at 0xCD006480 - real GC/Wii SI hardware's 128-byte poll-response buffer
// (publicly documented in YAGCD, not part of the SDK header, hence no macro for its size here) -
// so the block below runs long enough to cover 0xCD006480-0xCD0064FF too, the first address in it
// NSMBW's own boot code actually touches (a zeroing write, seen while bringing this file up).
// The 0xCD006440-0xCD00647F gap between the two is unlabeled by the header; backing it flatly
// alongside both real regions is simpler than modeling a hole nothing has been shown to depend on.
// Real SI drives the four GameCube controller ports.
//
// This is the base of the generic SI/input HLE layer described in docs/MASTER_PLAN.md's Phase 7
// notes: flat register storage, keyed only by hardware address range - no guest function or
// guest code address is registered here, so unlike hle/input/kpad.cpp this file needs no
// MKW_RUNTIME_PRODUCT_NSMBW guard and is safe to share with MKW's build unconditionally. MKW
// never exercises it: pad.cpp overrides MKW's PAD library functions at the guest-address level
// instead of letting their real bodies run down to raw SI registers, so this range simply never
// gets hit there.
//
// Only SI_SIPOLL is modeled with any real semantics yet, per NSMBW's own first SI write
// (SIPOLL=0x00F60200: SI_SIPOLL_X=246, SI_SIPOLL_Y=2, poll-enable bits for all four channels
// clear - a timer-config-only call, nothing enabled to poll yet). On real hardware SIPOLL has no
// write side effect: it only latches the autonomous polling timer's interval, which this runtime
// doesn't run (no periodic controller-poll interrupt model). Accepting the write and remembering
// the value is therefore the complete, correct behavior for that register - flat storage, same
// as pi.cpp's PI_INTMR. Every other register in the block (per-channel OUTBUF/INBUF, SICOMCSR,
// SISR, SIEXILK) is backed the same flat way for now so a read after a plain write sees what was
// stored, but none of them have a modeled side effect: in particular SICOMCSR's TSTART bit
// (which begins a real hardware transfer) is NOT specially handled here, unlike exi.cpp's
// EXI_CR_TSTART. If/when NSMBW actually sets it, that is the signal to build real SI transfer
// HLE (decode the OUTBUF command byte, produce a response via the host input backend, write
// INBUFH/INBUFL, set the matching SISR RDST bit) - not something to guess at in advance.
#include "memory_access.h"

#include <array>
#include <mutex>

namespace {
constexpr uint32_t kSiBase = 0xCD006400u;
constexpr uint32_t kSiRegCount = 64u; // covers the 16 named registers plus SI comm RAM through 0xCD0064FF
constexpr uint32_t kSiSize = kSiRegCount * 4u;

std::mutex g_siMutex;
std::array<uint32_t, kSiRegCount> g_siRegs{};
} // namespace

extern "C" bool SI_HLE_TryRead32(uint32_t addr, uint32_t* outValue) {
    if (addr < kSiBase || addr >= kSiBase + kSiSize || (addr & 3u) != 0 || outValue == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_siMutex);
    *outValue = g_siRegs[(addr - kSiBase) / 4u];
    return true;
}

extern "C" bool SI_HLE_TryWrite32(uint32_t addr, uint32_t value) {
    if (addr < kSiBase || addr >= kSiBase + kSiSize || (addr & 3u) != 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_siMutex);
    g_siRegs[(addr - kSiBase) / 4u] = value;
    return true;
}
