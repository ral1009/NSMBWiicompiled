// Diagnostic (not a fix): traces the real GXSetTevColor(id, GXColor*) call for NSMBW, to see
// the actual incoming color bytes at the point of the call - part of the WiiStrap
// invisible-content investigation (vertex/TEV alpha consistently reading back as 0 for content
// draws). The existing HLE override for this function in runtime/src/hle/gx/gx_tev.cpp targets
// 0x80171e10, which is MKW's address for GXSetTevColor and does not correspond to a real
// function in NSMBW's binary (that whole 0x80171xxx range disassembles as unrelated
// list/allocator code here) - so it silently never fires. The real, confirmed NSMBW
// GXSetTevColor, verified directly against original/wiimj2d.dol's disassembly, is at
// 0x801C8570 (reads a packed RGBA word from *r4, builds the RA/BG BP register pair, resends
// the BG word 3 times total - matches aurora's own GXSetTevColor implementation exactly).
//
// This file lives under projects/nsmbw/native/ specifically so the NSMBW shard generator
// (Translator.Cli emit-nsmbw-build-shards --native-source-dir projects/nsmbw/native) picks up
// and excludes 0x801C8570 from the translated output - overriding it from the shared
// runtime/src/hle tree instead breaks the build (linker: duplicate symbol against the
// translator's own auto-generated func_801C8570), which is why this diagnostic sits here
// instead of alongside the other (misaddressed, harmlessly-dead) GX TEV overrides.
//
// CHANGE-DETECTION EXTENSION: rather than a memory write-watchpoint (which would mean adding a
// branch to MemoryInline::FlatWrite32/FlatWriteRam32 - force-inlined into essentially every
// memory write in BOTH MKW's and NSMBW's entire generated codebases, not a narrow/local change),
// this tracks the last-seen color word per distinct colorPtr and, the moment it changes, dumps
// DiagRecentCalls (abi_bridge.h - already populated on every fast-path dispatch throughout
// normal execution, no new instrumentation needed) to narrow down which function ran in the
// single-frame window between the previous GXSetTevColor call for this address and this one -
// that window is exactly when the write must have happened.
//
// Delegates to aurora's own GXSetTevColor (already verified correct by reading its source)
// rather than hand-rolling the BP-register bit packing again here.
#include "hle_stubs.h"
#include <aurora/env.hpp>
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include <dolphin/gx/GXTev.h>
#include <dolphin/gx/GXAurora.h>

#include <cstdio>
#include <cstdlib>

namespace {

constexpr int kMaxTrackedAddrs = 16;
uint32_t g_trackedAddr[kMaxTrackedAddrs] = {0};
uint32_t g_trackedLastWord[kMaxTrackedAddrs] = {0};
bool g_trackedSeen[kMaxTrackedAddrs] = {false};
int g_trackedCount = 0;

void DumpRecentCalls(uint32_t count) {
    const uint32_t next = DiagRecentCalls::g_next.load(std::memory_order_relaxed);
    const uint32_t cap = static_cast<uint32_t>(DiagRecentCalls::kCapacity);
    if (count > cap) count = cap;
    std::fprintf(stderr, "  DiagRecentCalls (most recent first, %u entries):\n", count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t slot = (next - 1u - i) % cap;
        std::fprintf(stderr, "    [-%u] addr=0x%08X lr=0x%08X\n", i,
            DiagRecentCalls::g_addrs[slot], DiagRecentCalls::g_lrs[slot]);
    }
}

} // namespace

extern "C" void GXSetTevColor_Diag_801C8570(uint32_t id, uint32_t colorPtr) {
    uint32_t colorWord = 0;
    bool readOk = false;
    try {
        colorWord = Memory::Read32(colorPtr);
        readOk = true;
    } catch (const Memory::AccessViolation&) {
        readOk = false;
    }

    const uint8_t r = static_cast<uint8_t>(colorWord >> 24);
    const uint8_t g = static_cast<uint8_t>(colorWord >> 16);
    const uint8_t b = static_cast<uint8_t>(colorWord >> 8);
    const uint8_t a = static_cast<uint8_t>(colorWord);

    if (readOk && AURORA_ENV("NSMBW_LOG_TEVCOLOR_CHANGES") != nullptr) {
        static int totalChangeLogs = 0;
        int slot = -1;
        for (int i = 0; i < g_trackedCount; ++i) {
            if (g_trackedAddr[i] == colorPtr) { slot = i; break; }
        }
        if (slot < 0 && g_trackedCount < kMaxTrackedAddrs) {
            slot = g_trackedCount++;
            g_trackedAddr[slot] = colorPtr;
            g_trackedLastWord[slot] = colorWord;
            g_trackedSeen[slot] = true;
            if (totalChangeLogs < 60) {
                ++totalChangeLogs;
                std::fprintf(stderr,
                    "[nsmbw][tevcolor-change] FIRST SEEN id=%u colorPtr=0x%08X rgba=(%u,%u,%u,%u) callerLR=0x%08X\n",
                    id, colorPtr, r, g, b, a, GetPersistentCpuContext().lr);
                std::fflush(stderr);
            }
        } else if (slot >= 0 && g_trackedLastWord[slot] != colorWord) {
            const uint32_t oldWord = g_trackedLastWord[slot];
            g_trackedLastWord[slot] = colorWord;
            if (totalChangeLogs < 60) {
                ++totalChangeLogs;
                const uint8_t or_ = static_cast<uint8_t>(oldWord >> 24);
                const uint8_t og = static_cast<uint8_t>(oldWord >> 16);
                const uint8_t ob = static_cast<uint8_t>(oldWord >> 8);
                const uint8_t oa = static_cast<uint8_t>(oldWord);
                std::fprintf(stderr,
                    "[nsmbw][tevcolor-change] CHANGED id=%u colorPtr=0x%08X (%u,%u,%u,%u) -> (%u,%u,%u,%u) callerLR=0x%08X\n",
                    id, colorPtr, or_, og, ob, oa, r, g, b, a, GetPersistentCpuContext().lr);
                DumpRecentCalls(120);
                std::fflush(stderr);
            }
        }
    }

    if (AURORA_ENV("NSMBW_LOG_TEVCOLOR_RAW") != nullptr) {
        static int logged = 0;
        if (logged < 50) {
            ++logged;
            const uint32_t lr = GetPersistentCpuContext().lr;
            char hexdump[3 * 64 + 1] = {0};
            int hexoff = 0;
            bool dumpOk = true;
            const uint32_t dumpStart = colorPtr - 32u;
            for (int i = 0; i < 64 && dumpOk; ++i) {
                try {
                    const uint8_t byteVal = Memory::Read8(dumpStart + static_cast<uint32_t>(i));
                    hexoff += std::snprintf(hexdump + hexoff, sizeof(hexdump) - static_cast<size_t>(hexoff), "%02x ", byteVal);
                } catch (const Memory::AccessViolation&) {
                    dumpOk = false;
                }
            }
            std::fprintf(stderr,
                "[nsmbw][tevcolor] call#%d id=%u colorPtr=0x%08X readOk=%d rgba=(%u,%u,%u,%u) callerLR=0x%08X\n"
                "  dump[0x%08X..+64) ok=%d: %s\n",
                logged, id, colorPtr, readOk ? 1 : 0, r, g, b, a, lr,
                dumpStart, dumpOk ? 1 : 0, hexdump);
            std::fflush(stderr);
        }
    }

    if (!readOk) {
        return;
    }

    // NSMBW_LOG_LIQUID also covers the screen-mask routines (0x800CB990-0x800CC700, just before
    // dMaskMng::isCaveMask): they draw full-screen with blend ZERO / INVSRCALPHA, darkness taken from
    // this TEV colour. Log the colour and arm aurora's per-draw log (stream-ordered marker).
    {
        static const bool logLiquid = AURORA_ENV("NSMBW_LOG_LIQUID") != nullptr;
        static int maskLogged = 0;
        if (logLiquid && maskLogged < 40) {
            const CpuContext* c = TryGetCpuContext();
            const uint32_t lr = c ? static_cast<uint32_t>(c->lr) : 0u;
            if (lr >= 0x800CB990u && lr < 0x800CC700u) {
                ++maskLogged;
                std::fprintf(stderr, "[nsmbw][mask] GXSetTevColor id=%u rgba=(%u,%u,%u,%u) from LR=0x%08X\n", id, r, g, b, a, lr);
                GXInsertDebugMarker("nsmbw-arm-drawlog");
            }
        }
    }
    GXSetTevColor(static_cast<GXTevRegID>(id), GXColor{r, g, b, a});
}

PPC_NATIVE_OVERRIDE_VOID(801C8570, GXSetTevColor_Diag_801C8570, (uint32_t id, uint32_t colorPtr), (id, colorPtr));
