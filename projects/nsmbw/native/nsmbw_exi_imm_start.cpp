// Native override for NSMBW's own compiled EXI immediate-transfer start helper (0x801B8C60), the
// counterpart to 0x801B9090 (nsmbw_exi_imm_wait.cpp) - together they implement one immediate
// (non-DMA) EXI transfer: this half queues it and, for a write, clocks the caller's data out; the
// other half waits for hardware completion and, for a read, clocks the result back in. Fully read
// and traced end to end (576 lines) - no untraced regions remain, and every loop here is a
// ctr-decrement loop, which PowerPC's ctr semantics make hardware-guaranteed to terminate.
// Verified contract:
//   1. Bail out (return FALSE) if the per-channel RAM state struct (0x80390000-1520 +
//      channel*64, the same struct EXISelect/EXIDeselect/the wait half use) shows a transfer
//      already pending, or the channel isn't marked selected.
//   2. Record the requested type in that struct; if non-zero, OR the requested bits into the
//      channel's CPR register (base 0xCD006800 + channel*0x14 + 0x0) and call func_801B16D0 (an
//      already-translated, ordinary function - untouched here, only this function's own body is
//      replaced).
//   3. Mark the transfer active. If a nonzero length was requested, pack the caller's buffer into
//      a 32-bit accumulator and write it to the channel's DATA register (offset 0x10) - the write
//      half of the transfer; a zero length skips straight past this.
//   4. Record the caller's buffer pointer and clamped length into the RAM state struct (offset
//      0x14/0x10) so the wait half (0x801B9090) knows where to unpack a read's result later, build
//      the CR value (type + length bits), and write it to the CR register (offset 0xC) - the
//      actual hardware "start transfer" trigger.
//
// This is a byte-for-byte structural copy of the translated PPC body (build/nsmbw/functions/
// func_801B8C60.cpp) - every branch, comparison, sub-call and bit operation is unchanged,
// including the InvokeDirectCpu<> call into func_801B16D0 (an ordinary, still-translated
// function). The ONLY edit: the 4 FlatRead32/FlatWrite32 calls that touch EXI's real hardware
// registers (CPR at +0x0, DATA at +0x10, CR at +0xC, all r-relative to base 0xCD006800 +
// channel*0x14) are swapped for Memory::Read32/Write32 (the checked path, dispatching to
// runtime/src/hle/exi.cpp's register-block backing, the same block EXISelect/EXIDeselect/the wait
// half already use) - see pi.cpp's comment for why FlatRead/FlatWrite against real MMIO always
// hits an unrecoverable hardware fault on this runtime (the CR write here previously landed on an
// unguarded page and silently vanished instead of faulting - harmless by accident, since the
// checked path's zero-initialized CR already produces the same "no transfer in progress" result
// the wait half's TSTART poll needs, but not something to leave undiagnosed). Every other
// FlatRead32/FlatRead8/FlatWrite32/FlatWriteRam32/ReadResolved8 call here (stack spill/restore,
// the per-channel RAM state struct, the caller's own buffer) is genuine RAM access, left
// untouched.
#include "hle_stubs.h"
#include "memory.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

extern "C" void EXIImmStart_NSMBW_801B8C60(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t cr1_0 = 0;
    uint32_t r0_rot_0 = 0;
    uint32_t r0_rot_1 = 0;
    uint32_t r0_rot_2 = 0;
    uint32_t r0_rot_3 = 0;
    uint32_t r0_rot_4 = 0;
    uint32_t r0_rot_5 = 0;
    uint32_t r3_rot_0 = 0;
    uint32_t r3_rot_1 = 0;
    uint32_t r3_rot_2 = 0;
    uint32_t r3_rot_3 = 0;
    uint32_t r3_rot_4 = 0;
    uint32_t r3_rot_5 = 0;
    uint32_t r3_rot_6 = 0;
    uint32_t r3_rot_7 = 0;
    uint32_t r5_rot_0 = 0;
    uint32_t r5_rot_1 = 0;
    uint32_t r5_rot_10 = 0;
    uint32_t r5_rot_11 = 0;
    uint32_t r5_rot_12 = 0;
    uint32_t r5_rot_13 = 0;
    uint32_t r5_rot_14 = 0;
    uint32_t r5_rot_2 = 0;
    uint32_t r5_rot_3 = 0;
    uint32_t r5_rot_4 = 0;
    uint32_t r5_rot_5 = 0;
    uint32_t r5_rot_6 = 0;
    uint32_t r5_rot_7 = 0;
    uint32_t r5_rot_8 = 0;
    uint32_t r5_rot_9 = 0;
    uint32_t r5_subfic_ra_0 = 0;
    uint32_t r5_subfic_ra_1 = 0;
    uint32_t r5_subfic_ra_10 = 0;
    uint32_t r5_subfic_ra_11 = 0;
    uint32_t r5_subfic_ra_2 = 0;
    uint32_t r5_subfic_ra_3 = 0;
    uint32_t r5_subfic_ra_4 = 0;
    uint32_t r5_subfic_ra_5 = 0;
    uint32_t r5_subfic_ra_6 = 0;
    uint32_t r5_subfic_ra_7 = 0;
    uint32_t r5_subfic_ra_8 = 0;
    uint32_t r5_subfic_ra_9 = 0;
    uint8_t* guest_range_0 = nullptr;
    uint8_t* guest_range_1 = nullptr;
    uint8_t* guest_range_2 = nullptr;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r7 = ctx->gpr[7];
    uint32_t r11 = ctx->gpr[11];
    uint32_t r25 = ctx->gpr[25];
    uint32_t r26 = ctx->gpr[26];
    uint32_t r27 = ctx->gpr[27];
    uint32_t r28 = ctx->gpr[28];
    uint32_t r29 = ctx->gpr[29];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_801B8C60;

loc_801B8C60:
{
    guest_range_1 = MemoryInline::ResolveRangeHost((r1 + -48), 0, 56u, false, true);
    MemoryInline::WriteResolved32(guest_range_1, 0u, (r1 + -48), r1);
    r1 = (r1 + -48);
    r0 = ctx->lr;
    MemoryInline::WriteResolved32(guest_range_1, 52u, (r1 + 52), r0);
    r11 = (r1 + 48);
    // inline leaf 0x802DD05C (8 guest instruction(s))
    if (!MemoryInline::WriteResolvedPair32(guest_range_1, 20u, ((static_cast<uint64_t>(static_cast<uint32_t>(r25)) << 32) | static_cast<uint32_t>(r26)))) {
        MemoryInline::WriteResolved32(guest_range_1, 20u, (r11 + -28), r25);
        MemoryInline::WriteResolved32(guest_range_1, 24u, (r11 + -24), r26);
    }
    if (!MemoryInline::WriteResolvedPair32(guest_range_1, 28u, ((static_cast<uint64_t>(static_cast<uint32_t>(r27)) << 32) | static_cast<uint32_t>(r28)))) {
        MemoryInline::WriteResolved32(guest_range_1, 28u, (r11 + -20), r27);
        MemoryInline::WriteResolved32(guest_range_1, 32u, (r11 + -16), r28);
    }
    if (!MemoryInline::WriteResolvedPair32(guest_range_1, 36u, ((static_cast<uint64_t>(static_cast<uint32_t>(r29)) << 32) | static_cast<uint32_t>(r30)))) {
        MemoryInline::WriteResolved32(guest_range_1, 36u, (r11 + -12), r29);
        MemoryInline::WriteResolved32(guest_range_1, 40u, (r11 + -8), r30);
    }
    MemoryInline::WriteResolved32(guest_range_1, 44u, (r11 + -4), r31);
    // end of inlined leaf 0x802DD05C
    r26 = r3;
    r27 = r4;
    r28 = r5;
    r29 = r6;
    r25 = r7;
    r0_rot_0 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(6));
    r0 = (r0_rot_0 & -64);
    r3 = 0x80390000u;
    r3 = (r3 + -1520);
    r31 = (r3 + r0);
    // inline leaf 0x801B1280 (5 guest instruction(s))
    r3 = ctx->msr;
    r4 = (r3 & -32769);
    ctx->msr = r4;
    r3_rot_0 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(17));
    r3 = (r3_rot_0 & 1);
    // end of inlined leaf 0x801B1280
    r30 = r3;
    r0 = MemoryInline::FlatRead32((r31 + 12));
    r0 = (r0 & 3);
}

loc_801B8CA8:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B8CB8;
    }
}

loc_801B8CAC:
{
    r0 = MemoryInline::FlatRead32((r31 + 12));
    r0 = (r0 & 4);
}

loc_801B8CB4:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B8CC8;
    }
}

loc_801B8CB8:
{
    r3 = r30;
    // inline leaf 0x801B12C0 (9 guest instruction(s))
}

loc_inl2_0x801B12C0:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_inl2_0x801B12C4:
{
    r4 = ctx->msr;
    if (((cr & 0x20000000u) != 0)) {
        goto loc_inl2_0x801B12D4;
    }
}

loc_inl2_0x801B12CC:
{
    r5 = (r4 | 32768);
    goto loc_inl2_0x801B12D8;
}

loc_inl2_0x801B12D4:
{
    r5 = (r4 & -32769);
}

loc_inl2_0x801B12D8:
{
    ctx->msr = r5;
    r3_rot_6 = PpcRotl32Inline(static_cast<uint32_t>(r4), static_cast<uint32_t>(17));
    r3 = (r3_rot_6 & 1);
}

loc_inl2_cont_801B12C0:
{
    // end of inlined leaf 0x801B12C0
    r3 = 0;
    goto loc_801B8EC4;
}

loc_801B8CC8:
{
    MemoryInline::FlatWrite32((r31 + 4), r25);
}

loc_801B8CD0:
{
    if ((static_cast<int32_t>(r25) == static_cast<int32_t>(0))) {
        goto loc_801B8D04;
    }
}

loc_801B8CD4:
{
    r3 = (r26 * 20);
    r0 = -855638016;
    r3 = (r0 + r3);
    r0 = Memory::Read32(static_cast<uint32_t>(r3 + 26624));
    r0 = (r0 & 2037);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_801B8CE8:
{
    r0 = (r0 | 8);
    Memory::Write32(static_cast<uint32_t>(r3 + 26624), r0);
    r3 = 2097152;
    r0_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r26), static_cast<uint32_t>(2));
    r0 = (r0_rot_1 & -4);
    r0 = (r0 - r26);
    r3 = PPC_Srw(static_cast<uint32_t>(r3), static_cast<uint32_t>(r0));
    ctx->lr = 0x801B8D04u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->xer = xer;
    InvokeDirectCpu<0x801B16D0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
}

loc_801B8D04:
{
    r0 = MemoryInline::FlatRead32((r31 + 12));
    r0 = (r0 | 2);
    MemoryInline::FlatWrite32((r31 + 12), r0);
}

loc_801B8D14:
{
    if ((static_cast<int32_t>(r29) == static_cast<int32_t>(0))) {
        goto loc_801B8E78;
    }
}

loc_801B8D18:
{
    r0 = 0;
    r4 = 0;
    SetCRResident(cr, xer, 1, static_cast<int32_t>(r28), static_cast<int32_t>(0));
}

loc_801B8D24:
{
    if (((cr & 0x04000000u) == 0)) {
        goto loc_801B8E68;
    }
}

loc_801B8D28:
{
    r6 = (r28 + -8);
}

loc_801B8D30:
{
    if ((static_cast<int32_t>(r28) <= static_cast<int32_t>(8))) {
        goto loc_801B8E34;
    }
}

loc_801B8D34:
{
    r5 = 0;
    if (((cr & 0x08000000u) != 0)) {
        goto loc_801B8D50;
    }
}

loc_801B8D3C:
{
    r3 = 0x80000000u;
    r3 = (r3 + -2);
}

loc_801B8D48:
{
    if ((static_cast<int32_t>(r28) > static_cast<int32_t>(r3))) {
        goto loc_801B8D50;
    }
}

loc_801B8D4C:
{
    r5 = 1;
}

loc_801B8D50:
{
}

loc_801B8D54:
{
    if ((static_cast<int32_t>(r5) == static_cast<int32_t>(0))) {
        goto loc_801B8E34;
    }
}

loc_801B8D58:
{
    r3 = r27;
    r5 = (r6 + 7);
    r5_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(29));
    r5 = (r5_rot_1 & 536870911);
    ctr = r5;
}

loc_801B8D6C:
{
    if ((static_cast<int32_t>(r6) <= static_cast<int32_t>(0))) {
        goto loc_801B8E34;
    }
}

loc_801B8D70:
{
    guest_range_0 = MemoryInline::ResolveRangeHost(r3, 0, 8u, true, false);
    r6 = MemoryInline::ReadResolved8(guest_range_0, 0u, r3);
    r5 = (3 - r4);
    r5_rot_3 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(3));
    r5 = (r5_rot_3 & -8);
    r5 = PPC_Slw(static_cast<uint32_t>(r6), static_cast<uint32_t>(r5));
    r0 = (r0 | r5);
    r6 = MemoryInline::ReadResolved8(guest_range_0, 1u, (r3 + 1));
    r5 = (r4 + 1);
    r5_subfic_ra_2 = r5;
    r5 = (3 - r5_subfic_ra_2);
    r5_rot_4 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(3));
    r5 = (r5_rot_4 & -8);
    r5 = PPC_Slw(static_cast<uint32_t>(r6), static_cast<uint32_t>(r5));
    r0 = (r0 | r5);
    r6 = MemoryInline::ReadResolved8(guest_range_0, 2u, (r3 + 2));
    r5 = (r4 + 2);
    r5_subfic_ra_3 = r5;
    r5 = (3 - r5_subfic_ra_3);
    r5_rot_5 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(3));
    r5 = (r5_rot_5 & -8);
    r5 = PPC_Slw(static_cast<uint32_t>(r6), static_cast<uint32_t>(r5));
    r0 = (r0 | r5);
    r6 = MemoryInline::ReadResolved8(guest_range_0, 3u, (r3 + 3));
    r5 = (0 - r4);
    r5_rot_6 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(3));
    r5 = (r5_rot_6 & -8);
    r5 = PPC_Slw(static_cast<uint32_t>(r6), static_cast<uint32_t>(r5));
    r0 = (r0 | r5);
    r6 = MemoryInline::ReadResolved8(guest_range_0, 4u, (r3 + 4));
    r5 = (r4 + 4);
    r5_subfic_ra_4 = r5;
    r5 = (3 - r5_subfic_ra_4);
    r5_rot_7 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(3));
    r5 = (r5_rot_7 & -8);
    r5 = PPC_Slw(static_cast<uint32_t>(r6), static_cast<uint32_t>(r5));
    r0 = (r0 | r5);
    r6 = MemoryInline::ReadResolved8(guest_range_0, 5u, (r3 + 5));
    r5 = (r4 + 5);
    r5_subfic_ra_5 = r5;
    r5 = (3 - r5_subfic_ra_5);
    r5_rot_8 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(3));
    r5 = (r5_rot_8 & -8);
    r5 = PPC_Slw(static_cast<uint32_t>(r6), static_cast<uint32_t>(r5));
    r0 = (r0 | r5);
    r6 = MemoryInline::ReadResolved8(guest_range_0, 6u, (r3 + 6));
    r5 = (r4 + 6);
    r5_subfic_ra_6 = r5;
    r5 = (3 - r5_subfic_ra_6);
    r5_rot_9 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(3));
    r5 = (r5_rot_9 & -8);
    r5 = PPC_Slw(static_cast<uint32_t>(r6), static_cast<uint32_t>(r5));
    r0 = (r0 | r5);
    r6 = MemoryInline::ReadResolved8(guest_range_0, 7u, (r3 + 7));
    r5 = (r4 + 7);
    r5_subfic_ra_7 = r5;
    r5 = (3 - r5_subfic_ra_7);
    xer = (xer & 0xDFFFFFFFu) | ((static_cast<uint32_t>(3) >= static_cast<uint32_t>(r5_subfic_ra_7) ? 1u : 0u) << 29);
    r5_rot_10 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(3));
    r5 = (r5_rot_10 & -8);
    r5 = PPC_Slw(static_cast<uint32_t>(r6), static_cast<uint32_t>(r5));
    r0 = (r0 | r5);
    r3 = (r3 + 8);
    r4 = (r4 + 8);
    ctr = (ctr + -1);
    if ((ctr != 0)) {
        goto loc_801B8D70;
    }
}

loc_801B8E34:
{
    r6 = (r27 + r4);
    r3 = (r28 - r4);
    ctr = r3;
}

loc_801B8E44:
{
    if ((static_cast<int32_t>(r4) >= static_cast<int32_t>(r28))) {
        goto loc_801B8E68;
    }
}

loc_801B8E48:
{
    r5 = MemoryInline::FlatRead8(r6);
    r3 = (3 - r4);
    xer = (xer & 0xDFFFFFFFu) | ((static_cast<uint32_t>(3) >= static_cast<uint32_t>(r4) ? 1u : 0u) << 29);
    r3_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(3));
    r3 = (r3_rot_2 & -8);
    r3 = PPC_Slw(static_cast<uint32_t>(r5), static_cast<uint32_t>(r3));
    r0 = (r0 | r3);
    r6 = (r6 + 1);
    r4 = (r4 + 1);
    ctr = (ctr + -1);
    if ((ctr != 0)) {
        goto loc_801B8E48;
    }
}

loc_801B8E68:
{
    r4 = -855638016;
    r3 = (r26 * 20);
    r3 = (r4 + r3);
    Memory::Write32(static_cast<uint32_t>(r3 + 26640), r0);
}

loc_801B8E78:
{
    MemoryInline::FlatWrite32((r31 + 20), r27);
    r3 = (r29 + -1);
    r0 = (1 - r29);
    r0 = (r3 | r0);
    {
        const uint32_t ppcCarryValue = static_cast<uint32_t>(r0);
        const uint32_t ppcCarryShift = static_cast<uint32_t>(31) & 0x3Fu;
        const bool ppcCarryNegative = (ppcCarryValue & 0x80000000u) != 0;
        const uint32_t ppcCarry = ppcCarryShift == 0 ? 0u : (ppcCarryShift >= 32 ? (ppcCarryNegative && ppcCarryValue != 0 ? 1u : 0u) : (ppcCarryNegative && (ppcCarryValue & ((1u << ppcCarryShift) - 1u)) != 0 ? 1u : 0u));
        xer = (xer & 0xDFFFFFFFu) | (ppcCarry << 29);
    }
    r0 = (static_cast<int32_t>(r0) >> 31);
    r0 = (r28 & r0);
    MemoryInline::FlatWrite32((r31 + 16), r0);
    r0_rot_3 = PpcRotl32Inline(static_cast<uint32_t>(r29), static_cast<uint32_t>(2));
    r0 = (r0_rot_3 & -4);
    r3 = (r0 | 1);
    r0 = (r28 + -1);
    r0_rot_4 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(4));
    r0 = (r0_rot_4 & -16);
    r4 = (r3 | r0);
    r3 = -855638016;
    r0 = (r26 * 20);
    r3 = (r3 + r0);
    Memory::Write32(static_cast<uint32_t>(r3 + 26636), r4);
    r3 = r30;
    // inline leaf 0x801B12C0 (9 guest instruction(s))
}

loc_inl3_0x801B12C0:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_inl3_0x801B12C4:
{
    r4 = ctx->msr;
    if (((cr & 0x20000000u) != 0)) {
        goto loc_inl3_0x801B12D4;
    }
}

loc_inl3_0x801B12CC:
{
    r5 = (r4 | 32768);
    goto loc_inl3_0x801B12D8;
}

loc_inl3_0x801B12D4:
{
    r5 = (r4 & -32769);
}

loc_inl3_0x801B12D8:
{
    ctx->msr = r5;
    r3_rot_5 = PpcRotl32Inline(static_cast<uint32_t>(r4), static_cast<uint32_t>(17));
    r3 = (r3_rot_5 & 1);
}

loc_inl3_cont_801B12C0:
{
    // end of inlined leaf 0x801B12C0
    r3 = 1;
}

loc_801B8EC4:
{
    r11 = (r1 + 48);
    // inline leaf 0x802DD0A8 (8 guest instruction(s))
    guest_range_2 = MemoryInline::ResolveRangeHost((r11 + -28), 0, 36u, true, false);
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_2, 0u);
        if (resolved_pair.valid) {
            r25 = resolved_pair.first;
            r26 = resolved_pair.second;
        } else {
            r25 = MemoryInline::ReadResolved32(guest_range_2, 0u, (r11 + -28));
            r26 = MemoryInline::ReadResolved32(guest_range_2, 4u, (r11 + -24));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_2, 8u);
        if (resolved_pair.valid) {
            r27 = resolved_pair.first;
            r28 = resolved_pair.second;
        } else {
            r27 = MemoryInline::ReadResolved32(guest_range_2, 8u, (r11 + -20));
            r28 = MemoryInline::ReadResolved32(guest_range_2, 12u, (r11 + -16));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_2, 16u);
        if (resolved_pair.valid) {
            r29 = resolved_pair.first;
            r30 = resolved_pair.second;
        } else {
            r29 = MemoryInline::ReadResolved32(guest_range_2, 16u, (r11 + -12));
            r30 = MemoryInline::ReadResolved32(guest_range_2, 20u, (r11 + -8));
        }
    }
    r31 = MemoryInline::ReadResolved32(guest_range_2, 24u, (r11 + -4));
    // end of inlined leaf 0x802DD0A8
    r0 = MemoryInline::ReadResolved32(guest_range_2, 32u, (r1 + 52));
    ctx->lr = r0;
    r1 = (r1 + 48);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[11] = r11;
    ctx->gpr[25] = r25;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    ctx->xer = xer;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(801B8C60, EXIImmStart_NSMBW_801B8C60, (CpuContext* ctx), (ctx));
