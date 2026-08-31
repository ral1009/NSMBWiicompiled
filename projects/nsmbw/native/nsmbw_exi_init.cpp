// Native override for NSMBW's own compiled EXIInit body (0x801B9CE0). Confirmed against
// NSMBW-Decomp's include/lib/revolution/EXI/EXIHardware.h: this busy-waits on each of the 3 EXI
// channels' CPR.EXIINT-adjacent "transfer start" bit (offsets 0xC, 0x20, 0x34 = channel N's `cr`
// field, base 0xCD006800 + N*0x14 + 0xC), then resets each channel's cpr (offset 0, 20, 40) to 0
// before probing what's plugged into channel 0 via several sub-calls.
//
// This is a byte-for-byte structural copy of the translated PPC body (build/nsmbw/functions/
// func_801B9CE0.cpp) - every branch, comparison, sub-call and bit operation is unchanged,
// including the InvokeDirectCpu<> calls into other translated functions (those are ordinary,
// still-translated functions; only THIS function's own body is replaced). The ONLY edit: the 7
// FlatRead32/FlatWrite32 calls that touch EXI's real hardware registers (r3-relative, base
// 0xCD006800-ish) are swapped for Memory::Read32/Write32 (the checked path, which dispatches to
// this runtime's own EXI register-block backing in runtime/src/hle/exi.cpp) - see pi.cpp's
// comment for why FlatRead/FlatWrite against real MMIO always hits an unrecoverable hardware
// fault on this runtime. Every other FlatRead32/FlatWrite32/FlatWriteRam32 call here (stack
// spill/restore, r13-relative small-data-area globals, r5-relative array indexing) is genuine RAM
// access, left untouched.
#include "hle_stubs.h"
#include "memory.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

extern "C" void EXIInit_NSMBW_801B9CE0(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r0_rot_0 = 0;
    uint32_t r0_rot_1 = 0;
    uint32_t r0_rot_2 = 0;
    uint32_t r0_rot_3 = 0;
    uint32_t r0_rot_4 = 0;
    uint32_t r0_rot_5 = 0;
    uint32_t r0_rot_6 = 0;
    uint32_t r0_rot_7 = 0;
    uint32_t r5_addr_0 = 0;
    uint32_t r5_addr_1 = 0;
    uint32_t r5_addr_10 = 0;
    uint32_t r5_addr_11 = 0;
    uint32_t r5_addr_12 = 0;
    uint32_t r5_addr_13 = 0;
    uint32_t r5_addr_14 = 0;
    uint32_t r5_addr_15 = 0;
    uint32_t r5_addr_2 = 0;
    uint32_t r5_addr_3 = 0;
    uint32_t r5_addr_4 = 0;
    uint32_t r5_addr_5 = 0;
    uint32_t r5_addr_6 = 0;
    uint32_t r5_addr_7 = 0;
    uint32_t r5_addr_8 = 0;
    uint32_t r5_addr_9 = 0;
    uint8_t* guest_range_0 = nullptr;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r13 = ctx->gpr[13];
    uint32_t r28 = ctx->gpr[28];
    uint32_t r29 = ctx->gpr[29];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t xer = ctx->xer;

    goto loc_801B9CE0;

loc_801B9CE0:
{
    MemoryInline::FlatWriteRam32((r1 + -32), r1);
    r1 = (r1 + -32);
    r0 = ctx->lr;
    MemoryInline::FlatWriteRam32((r1 + 36), r0);
    MemoryInline::FlatWriteRam32((r1 + 28), r31);
    MemoryInline::FlatWriteRam32((r1 + 24), r30);
    MemoryInline::FlatWriteRam32((r1 + 20), r29);
    MemoryInline::FlatWriteRam32((r1 + 16), r28);
    r3 = -855638016;
}

loc_801B9D00:
{
    r0 = Memory::Read32(static_cast<uint32_t>(r3 + 26636));
    r0 = (r0 & 1);
}

loc_801B9D0C:
{
    if ((static_cast<uint32_t>(r0) == static_cast<uint32_t>(1))) {
        goto loc_801B9D00;
    }
}

loc_801B9D10:
{
    r0 = Memory::Read32(static_cast<uint32_t>(r3 + 26656));
    r0 = (r0 & 1);
}

loc_801B9D1C:
{
    if ((static_cast<uint32_t>(r0) == static_cast<uint32_t>(1))) {
        goto loc_801B9D00;
    }
}

loc_801B9D20:
{
    r0 = Memory::Read32(static_cast<uint32_t>(r3 + 26676));
    r0 = (r0 & 1);
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(1));
}

loc_801B9D2C:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801B9D00;
    }
}

loc_801B9D30:
{
    r3 = 8388608;
    r3 = (r3 + -32768);
    ctx->lr = 0x801B9D3Cu;
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
    r28 = 0;
    r3 = -855638016;
    Memory::Write32(static_cast<uint32_t>(r3 + 26624), r28);
    Memory::Write32(static_cast<uint32_t>(r3 + 26644), r28);
    Memory::Write32(static_cast<uint32_t>(r3 + 26664), r28);
    r0 = 8192;
    Memory::Write32(static_cast<uint32_t>(r3 + 26624), r0);
    r3 = 9;
    r29 = 0x801C0000u;
    r4 = (r29 + -26320);
    // inline leaf 0x801B12F0 (5 guest instruction(s))
    guest_range_0 = MemoryInline::ResolveRangeHost((r13 + -20424), 0, 4u, true, false);
    r5 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r13 + -20424));
    r0_rot_0 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(2));
    r0 = (r0_rot_0 & -4);
    r5_addr_0 = (r5 + r0);
    r3 = MemoryInline::FlatRead32(r5_addr_0);
    r5_addr_1 = (r5 + r0);
    MemoryInline::FlatWrite32(r5_addr_1, r4);
    // end of inlined leaf 0x801B12F0
    r3 = 10;
    r30 = 0x801C0000u;
    r4 = (r30 + -26128);
    // inline leaf 0x801B12F0 (5 guest instruction(s))
    r5 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r13 + -20424));
    r0_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(2));
    r0 = (r0_rot_1 & -4);
    r5_addr_2 = (r5 + r0);
    r3 = MemoryInline::FlatRead32(r5_addr_2);
    r5_addr_3 = (r5 + r0);
    MemoryInline::FlatWrite32(r5_addr_3, r4);
    // end of inlined leaf 0x801B12F0
    r3 = 11;
    r31 = 0x801C0000u;
    r4 = (r31 + -25584);
    // inline leaf 0x801B12F0 (5 guest instruction(s))
    r5 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r13 + -20424));
    r0_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(2));
    r0 = (r0_rot_2 & -4);
    r5_addr_4 = (r5 + r0);
    r3 = MemoryInline::FlatRead32(r5_addr_4);
    r5_addr_5 = (r5 + r0);
    MemoryInline::FlatWrite32(r5_addr_5, r4);
    // end of inlined leaf 0x801B12F0
    r3 = 12;
    r4 = (r29 + -26320);
    // inline leaf 0x801B12F0 (5 guest instruction(s))
    r5 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r13 + -20424));
    r0_rot_3 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(2));
    r0 = (r0_rot_3 & -4);
    r5_addr_6 = (r5 + r0);
    r3 = MemoryInline::FlatRead32(r5_addr_6);
    r5_addr_7 = (r5 + r0);
    MemoryInline::FlatWrite32(r5_addr_7, r4);
    // end of inlined leaf 0x801B12F0
    r3 = 13;
    r4 = (r30 + -26128);
    // inline leaf 0x801B12F0 (5 guest instruction(s))
    r5 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r13 + -20424));
    r0_rot_4 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(2));
    r0 = (r0_rot_4 & -4);
    r5_addr_8 = (r5 + r0);
    r3 = MemoryInline::FlatRead32(r5_addr_8);
    r5_addr_9 = (r5 + r0);
    MemoryInline::FlatWrite32(r5_addr_9, r4);
    // end of inlined leaf 0x801B12F0
    r3 = 14;
    r4 = (r31 + -25584);
    // inline leaf 0x801B12F0 (5 guest instruction(s))
    r5 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r13 + -20424));
    r0_rot_5 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(2));
    r0 = (r0_rot_5 & -4);
    r5_addr_10 = (r5 + r0);
    r3 = MemoryInline::FlatRead32(r5_addr_10);
    r5_addr_11 = (r5 + r0);
    MemoryInline::FlatWrite32(r5_addr_11, r4);
    // end of inlined leaf 0x801B12F0
    r3 = 15;
    r4 = (r29 + -26320);
    // inline leaf 0x801B12F0 (5 guest instruction(s))
    r5 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r13 + -20424));
    r0_rot_6 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(2));
    r0 = (r0_rot_6 & -4);
    r5_addr_12 = (r5 + r0);
    r3 = MemoryInline::FlatRead32(r5_addr_12);
    r5_addr_13 = (r5 + r0);
    MemoryInline::FlatWrite32(r5_addr_13, r4);
    // end of inlined leaf 0x801B12F0
    r3 = 16;
    r4 = (r30 + -26128);
    // inline leaf 0x801B12F0 (5 guest instruction(s))
    r5 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r13 + -20424));
    r0_rot_7 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(2));
    r0 = (r0_rot_7 & -4);
    r5_addr_14 = (r5 + r0);
    r3 = MemoryInline::FlatRead32(r5_addr_14);
    r5_addr_15 = (r5 + r0);
    MemoryInline::FlatWrite32(r5_addr_15, r4);
    // end of inlined leaf 0x801B12F0
    r3 = 0;
    r4 = 2;
    r5 = (r13 + -20248);
    ctx->lr = 0x801B9DD4u;
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
    InvokeDirectCpu<0x801BA0C0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r13 = ctx->gpr[13];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
    r0 = MemoryInline::FlatRead32((r13 + -20584));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_801B9DDC:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801B9E10;
    }
}

loc_801B9DE0:
{
    r3 = 0x80000000u;
    MemoryInline::FlatWriteRam32((r3 + 12484), r28);
    MemoryInline::FlatWriteRam32((r3 + 12480), r28);
    r3 = 0x80390000u;
    r3 = (r3 + -1520);
    MemoryInline::FlatWriteRam32((r3 + 96), r28);
    MemoryInline::FlatWriteRam32((r3 + 32), r28);
    r3 = 0;
    ctx->lr = 0x801B9E04u;
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
    r3 = 1;
    ctx->lr = 0x801B9E0Cu;
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
    goto loc_801B9E7C;
}

loc_801B9E10:
{
    r3 = 0;
    r4 = 0;
    r5 = (r1 + 8);
    ctx->lr = 0x801B9E20u;
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
    InvokeDirectCpu<0x801BA0C0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r13 = ctx->gpr[13];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_801B9E24:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801B9E48;
    }
}

loc_801B9E28:
{
    r3 = MemoryInline::FlatRead32((r1 + 8));
    r0 = (r3 + -117506048);
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(0));
}

loc_801B9E34:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801B9E48;
    }
}

loc_801B9E38:
{
    r3 = 1;
    r4 = 0;
    ctx->lr = 0x801B9E44u;
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
    InvokeDirectCpu<0x801BA5F0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r13 = ctx->gpr[13];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
    goto loc_801B9E7C;
}

loc_801B9E48:
{
    r3 = 1;
    r4 = 0;
    r5 = (r1 + 8);
    ctx->lr = 0x801B9E58u;
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
    InvokeDirectCpu<0x801BA0C0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r13 = ctx->gpr[13];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_801B9E5C:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801B9E7C;
    }
}

loc_801B9E60:
{
    r3 = MemoryInline::FlatRead32((r1 + 8));
    r0 = (r3 + -117506048);
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(0));
}

loc_801B9E6C:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801B9E7C;
    }
}

loc_801B9E70:
{
    r3 = 0;
    r4 = 2;
    ctx->lr = 0x801B9E7Cu;
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
    InvokeDirectCpu<0x801BA5F0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r13 = ctx->gpr[13];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
}

loc_801B9E7C:
{
    r3 = MemoryInline::FlatRead32((r13 + -24880));
    ctx->lr = 0x801B9E84u;
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
    InvokeDirectCpu<0x801AB300u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r13 = ctx->gpr[13];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
    r31 = MemoryInline::FlatRead32((r1 + 28));
    r30 = MemoryInline::FlatRead32((r1 + 24));
    r29 = MemoryInline::FlatRead32((r1 + 20));
    r28 = MemoryInline::FlatRead32((r1 + 16));
    r0 = MemoryInline::FlatRead32((r1 + 36));
    ctx->lr = r0;
    r1 = (r1 + 32);
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
    return;
}

}

PPC_NATIVE_OVERRIDE_VOID(801B9CE0, EXIInit_NSMBW_801B9CE0, (CpuContext* ctx), (ctx));
