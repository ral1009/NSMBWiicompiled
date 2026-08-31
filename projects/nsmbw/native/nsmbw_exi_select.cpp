// Native override for NSMBW's own compiled EXISelect body (0x801B9700). Confirmed against
// NSMBW-Decomp's include/lib/revolution/EXI/EXIHardware.h: asserts the requested device's chip-
// select bits (CS0B/CS1B/CS2B) and clock divider into the channel's `cpr` field (base
// 0xCD006800 + channel*0x14), after checking the channel isn't already busy/locked via its own
// per-channel RAM state struct (0x8038FA10 + channel*64, unrelated to hardware).
//
// This is a byte-for-byte structural copy of the translated PPC body (build/nsmbw/functions/
// func_801B9700.cpp) - every branch, comparison, sub-call and bit operation is unchanged,
// including the InvokeDirectCpu<> calls into other translated functions. The ONLY edit: the 2
// FlatRead32/FlatWrite32 calls that touch EXI's real hardware register (r4-relative, base
// 0xCD006800 + channel*20) are swapped for Memory::Read32/Write32 (the checked path, dispatching
// to runtime/src/hle/exi.cpp's register-block backing) - see pi.cpp's comment for why FlatRead/
// FlatWrite against real MMIO always hits an unrecoverable hardware fault on this runtime. Every
// other FlatRead32/FlatWrite32/FlatWriteRam32 call here (stack spill/restore, the per-channel RAM
// state struct at r31) is genuine RAM access, left untouched.
#include "hle_stubs.h"
#include "memory.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

extern "C" void EXISelect_NSMBW_801B9700(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r0_rot_0 = 0;
    uint32_t r0_rot_1 = 0;
    uint32_t r0_rot_2 = 0;
    uint32_t r0_rot_3 = 0;
    uint32_t r3_rot_0 = 0;
    uint32_t r3_rot_1 = 0;
    uint32_t r3_rot_2 = 0;
    uint32_t r3_rot_3 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r11 = ctx->gpr[11];
    uint32_t r27 = ctx->gpr[27];
    uint32_t r28 = ctx->gpr[28];
    uint32_t r29 = ctx->gpr[29];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t xer = ctx->xer;

    goto loc_801B9700;

loc_801B9700:
{
    MemoryInline::FlatWriteRam32((r1 + -32), r1);
    r1 = (r1 + -32);
    r0 = ctx->lr;
    MemoryInline::FlatWriteRam32((r1 + 36), r0);
    r11 = (r1 + 32);
    // inline leaf 0x802DD064 (6 guest instruction(s))
    MemoryInline::FlatWriteRam32((r11 + -20), r27);
    MemoryInline::FlatWriteRam32((r11 + -16), r28);
    MemoryInline::FlatWriteRam32((r11 + -12), r29);
    MemoryInline::FlatWriteRam32((r11 + -8), r30);
    MemoryInline::FlatWriteRam32((r11 + -4), r31);
    // end of inlined leaf 0x802DD064
    r27 = r3;
    r28 = r4;
    r29 = r5;
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
    r0 = (r0 & 4);
}

loc_801B9740:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B9788;
    }
}

loc_801B9744:
{
}

loc_801B9748:
{
    if ((static_cast<int32_t>(r27) == static_cast<int32_t>(2))) {
        goto loc_801B9798;
    }
}

loc_801B974C:
{
}

loc_801B9750:
{
    if ((static_cast<int32_t>(r28) != static_cast<int32_t>(0))) {
        goto loc_801B9770;
    }
}

loc_801B9754:
{
    r0 = MemoryInline::FlatRead32((r31 + 12));
    r0 = (r0 & 8);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_801B975C:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801B9770;
    }
}

loc_801B9760:
{
    r3 = r27;
    ctx->lr = 0x801B9768u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<0x801B93A0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
}

loc_801B976C:
{
    if ((static_cast<int32_t>(r3) == static_cast<int32_t>(0))) {
        goto loc_801B9788;
    }
}

loc_801B9770:
{
    r0 = MemoryInline::FlatRead32((r31 + 12));
    r0 = (r0 & 16);
}

loc_801B9778:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_801B9788;
    }
}

loc_801B977C:
{
    r0 = MemoryInline::FlatRead32((r31 + 24));
}

loc_801B9784:
{
    if ((static_cast<uint32_t>(r0) == static_cast<uint32_t>(r28))) {
        goto loc_801B9798;
    }
}

loc_801B9788:
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
    r3_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r4), static_cast<uint32_t>(17));
    r3 = (r3_rot_2 & 1);
}

loc_inl2_cont_801B12C0:
{
    // end of inlined leaf 0x801B12C0
    r3 = 0;
    goto loc_801B9814;
}

loc_801B9798:
{
    r0 = MemoryInline::FlatRead32((r31 + 12));
    r0 = (r0 | 4);
    MemoryInline::FlatWrite32((r31 + 12), r0);
    r3 = (r27 * 20);
    r0 = -855638016;
    r4 = (r0 + r3);
    r3 = Memory::Read32(static_cast<uint32_t>(r4 + 26624));
    r3 = (r3 & 1029);
}

loc_801B97B8:
{
    r0_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r29), static_cast<uint32_t>(4));
    r0 = (r0_rot_1 & -16);
    r3 = (r3 | r0);
    r0 = 1;
    r0 = PPC_Slw(static_cast<uint32_t>(r0), static_cast<uint32_t>(r28));
    r0_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(7));
    r0 = (r0_rot_2 & -128);
    r3 = (r3 | r0);
    Memory::Write32(static_cast<uint32_t>(r4 + 26624), r3);
    r0 = MemoryInline::FlatRead32((r31 + 12));
    r0 = (r0 & 8);
}

loc_801B97DC:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_801B9808;
    }
}

loc_801B97E0:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r27), static_cast<int32_t>(0));
}

loc_801B97E4:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801B97F4;
    }
}

loc_801B97E8:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r27), static_cast<int32_t>(1));
}

loc_801B97EC:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801B9800;
    }
}

loc_801B97F0:
{
    goto loc_801B9808;
}

loc_801B97F4:
{
    r3 = 1048576;
    ctx->lr = 0x801B97FCu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<0x801B1650u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    goto loc_801B9808;
}

loc_801B9800:
{
    r3 = 131072;
    ctx->lr = 0x801B9808u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<0x801B1650u>(ctx);
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

loc_801B9808:
{
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
    r3_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r4), static_cast<uint32_t>(17));
    r3 = (r3_rot_1 & 1);
}

loc_inl3_cont_801B12C0:
{
    // end of inlined leaf 0x801B12C0
    r3 = 1;
}

loc_801B9814:
{
    r11 = (r1 + 32);
    // inline leaf 0x802DD0B0 (6 guest instruction(s))
    r27 = MemoryInline::FlatRead32((r11 + -20));
    r28 = MemoryInline::FlatRead32((r11 + -16));
    r29 = MemoryInline::FlatRead32((r11 + -12));
    r30 = MemoryInline::FlatRead32((r11 + -8));
    r31 = MemoryInline::FlatRead32((r11 + -4));
    // end of inlined leaf 0x802DD0B0
    r0 = MemoryInline::FlatRead32((r1 + 36));
    ctx->lr = r0;
    r1 = (r1 + 32);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[11] = r11;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(801B9700, EXISelect_NSMBW_801B9700, (CpuContext* ctx), (ctx));
