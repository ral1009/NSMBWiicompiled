// Reserve the four pre-seeded RELs' address ranges in the dylink heap the moment it is created.
//
// The problem (2026-09-28): RestoreRelImages() (nsmbw_guest_ctor_hook.cpp) copies d_profileNP,
// d_basesNP, d_enemiesNP and d_en_bossNP to their retail addresses at the start of boot, and the
// game's own load/link is skipped (nsmbw_preseeded_rel_link.cpp). Those addresses lie inside the
// 5 MB dylink ExpHeap (HEAP_SIZE_DYLINK), logged here as [0x80767660, 0x80C675D8), which on a
// console holds the RELs as ordinary heap blocks. Here the heap never allocated them, so it used
// that memory for other things. Measured: the game briefly allocates ~8 KB there during
// DynamicModuleCallback::InitCallback and frees it again; freeing merges the block but leaves the
// split point's free-block header ('FR', size 0x4FCFA0) in memory at 0x8076A628 - inside
// d_profileNP, over dYesNoWindow's MainMsgIDs[12..15]. On a console the REL is loaded after that,
// over the stale header; here the REL was already there. Visible result: the pause menu's
// "return to the map?" window (MainMsgIDs[12]) had no text.
//
// Reserving at link time is too late for that transient allocation, and the later RELs are not
// where a fresh allocation would land (it returned 0x8076D660 for d_basesNP's 0x8076D680). So the
// ranges are reserved here, while the heap is still empty: for each REL, in address order, a
// padding block brings the heap's first free byte to (image - 0x10) and the REL block is
// allocated there; afterwards the padding blocks are freed so the game can still use those gaps.
// Every result is checked against the image address and logged.
//
// Block size per REL is fixSize + bssSize from its .rel header (+0x48, +0x20), the size the
// console's loader shrinks each REL's block to after OSLinkFixed (c_dylink.cpp); the retail
// addresses fit that exactly (d_profileNP 0x807684C0 + 0x4F84 + 0x208 -> header/alignment ->
// d_basesNP 0x8076D680). It covers the image RestoreRelImages() copies (sections + .bss).
//
// createDylinkHeap (0x8016EC60) itself is a structural copy of the translated body
// (build/nsmbw/functions/func_8016EC60.cpp): call mHeap::createHeap(size, parent, name) at
// 0x8016EA30 with r5 = the heap name, store the result in mHeap::g_dylinkHeap (r13 - 21068) and
// return it. Only the reservation call is added.
#include "hle_stubs.h"
#include <aurora/env.hpp>
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include "recomp_mod_loader.h"

#include <cstdint>
#include <cstdio>

namespace {
constexpr uint32_t kHeapAllocAddr = 0x802B8E00u; // EGG::Heap::alloc(size, align, heap)   syms.txt
constexpr uint32_t kHeapFreeAddr = 0x802B90B0u;  // EGG::Heap::free(ptr, heap)            syms.txt
constexpr uint32_t kBlockHeader = 0x10u;         // MEMiExpHeapMBlock header (mem_expHeap.h)

struct RelRange {
    const char* name;
    uint32_t base; // where RestoreRelImages() puts the image
    uint32_t size; // fixSize + bssSize
};
constexpr RelRange kRels[] = {
    {"d_profileNP", 0x807684C0u, 0x4F84u + 0x208u},
    {"d_basesNP", 0x8076D680u, 0x223178u + 0x12484u},
    {"d_enemiesNP", 0x809A2CA0u, 0x16E780u + 0xB4E0u},
    {"d_en_bossNP", 0x80B1C920u, 0x6D170u + 0x487Cu},
};

// First free block's header address: EGG::Heap::mHeapHandle (+0x10) -> MEMiHeapHead, whose
// MEMiExpHeapHead (free list head at +0x3C) follows the 0x3C-byte common header.
uint32_t FirstFreeHeader(uint32_t heap) {
    uint32_t head = 0, blk = 0;
    Memory::TryRead32(heap + 0x10u, head);
    if (head != 0) Memory::TryRead32(head + 0x3Cu, blk);
    return blk;
}

uint32_t HeapAlloc(CpuContext* ctx, uint32_t heap, uint32_t size, uint32_t align) {
    ctx->gpr[3] = size;
    ctx->gpr[4] = align;
    ctx->gpr[5] = heap;
    InvokeDirectCpu<kHeapAllocAddr>(ctx);
    return ctx->gpr[3];
}

void HeapFree(CpuContext* ctx, uint32_t heap, uint32_t ptr) {
    ctx->gpr[3] = ptr;
    ctx->gpr[4] = heap;
    InvokeDirectCpu<kHeapFreeAddr>(ctx);
}

void ReserveRelRanges(CpuContext* ctx, uint32_t heap) {
    uint32_t pads[4] = {};
    int ok = 0;
    for (size_t i = 0; i < 4; ++i) {
        const RelRange& r = kRels[i];
        const uint32_t freeHdr = FirstFreeHeader(heap);
        const uint32_t wantHdr = r.base - kBlockHeader;
        if (freeHdr == 0 || freeHdr > wantHdr) {
            std::fprintf(stderr, "[nsmbw][rel] WARNING: cannot reserve %s: heap's first free byte 0x%08X is past 0x%08X\n",
                         r.name, freeHdr, wantHdr);
            continue;
        }
        if (freeHdr < wantHdr) {
            // A padding block whose data ends exactly where the REL's header must start.
            const uint32_t padSize = wantHdr - (freeHdr + kBlockHeader);
            if (padSize < 4u) {
                std::fprintf(stderr, "[nsmbw][rel] WARNING: cannot reserve %s: gap of %u bytes is too small for a pad\n",
                             r.name, wantHdr - freeHdr);
                continue;
            }
            pads[i] = HeapAlloc(ctx, heap, padSize, 4);
        }
        // A gap to the next REL smaller than a pad (0x10 header + 4 bytes) cannot be filled by a
        // pad block, so this block absorbs it: d_enemiesNP ends 0x10 bytes before d_en_bossNP's
        // header, which left d_en_bossNP unreservable ("gap of 16 bytes", first test run).
        uint32_t size = r.size;
        if (i + 1 < 4) {
            const uint32_t nextHdr = kRels[i + 1].base - kBlockHeader;
            const uint32_t gap = nextHdr - (r.base + size);
            if (nextHdr > r.base + size && gap < kBlockHeader + 4u) size += gap;
        }
        const uint32_t got = HeapAlloc(ctx, heap, size, 4);
        if (got == r.base) {
            ++ok;
            std::fprintf(stderr, "[nsmbw][rel] reserved %s [0x%08X, 0x%08X) in the dylink heap\n", r.name, got,
                         got + size);
        } else {
            std::fprintf(stderr, "[nsmbw][rel] WARNING: reserving %s returned 0x%08X, not its image at 0x%08X - freed\n",
                         r.name, got, r.base);
            if (got != 0) HeapFree(ctx, heap, got);
        }
    }
    for (uint32_t pad : pads) {
        if (pad != 0) HeapFree(ctx, heap, pad);
    }
    std::fprintf(stderr, "[nsmbw][rel] %d of 4 REL ranges reserved in the dylink heap\n", ok);
}
} // namespace

extern "C" void NsmbwCreateDylinkHeap_8016EC60(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r13 = ctx->gpr[13];

    MemoryInline::FlatWriteRam32((r1 + -16), r1);
    r1 = (r1 + -16);
    r0 = ctx->lr;
    r5 = 0x80330000u;
    MemoryInline::FlatWriteRam32((r1 + 20), r0);
    r5 = (r5 + -24824);
    ctx->lr = 0x8016EC78u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[5] = r5;
    InvokeDirectCpu<0x8016EA30u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r5 = ctx->gpr[5];
    r13 = ctx->gpr[13];
    MemoryInline::FlatWrite32((r13 + -21068), r3);

    // Added: reserve the REL ranges while the new heap is still empty (still inside this
    // function's own stack frame, r1). The heap pointer is restored as the return value below.
    const uint32_t heap = r3;
    if (heap != 0) ReserveRelRanges(ctx, heap);
    ctx->gpr[1] = r1;
    r3 = heap;

    r0 = MemoryInline::FlatRead32((r1 + 20));
    ctx->lr = r0;
    r1 = (r1 + 16);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[5] = r5;
    ctx->gpr[13] = r13;
}
PPC_NATIVE_OVERRIDE_VOID(8016EC60, NsmbwCreateDylinkHeap_8016EC60, (CpuContext* ctx), (ctx));
