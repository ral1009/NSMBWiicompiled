// Native override for NSMBW's own compiled EXIDeselect body (0x801B9830). Confirmed against
// NSMBW-Decomp's include/lib/revolution/EXI/EXIBios.h (`BOOL EXIDeselect(EXIChannel chan)`) and
// EXIHardware.h's CPR bit layout: after checking the channel's per-channel RAM state struct
// (0x80390000-1520 + channel*64, unrelated to hardware - same struct func_801B9700/EXISelect
// uses) actually has the "selected" flag set, it clears the flag, then reads the channel's `cpr`
// hardware register (base 0xCD006800 + channel*0x14) and writes back `cpr & 1029`. 1029 = 0x405 =
// EXI_CPR_EXTINTMASK(0x400) | EXI_CPR_TCINTMASK(0x004) | EXI_CPR_EXIINTMASK(0x001) - i.e. clears
// EXI_CPR_CS0B/CS1B/CS2B (chip-select), EXI_CPR_CLK, EXI_CPR_EXT and EXI_CPR_ROMDIS while
// preserving only the three interrupt-mask bits. That's exactly EXIDeselect's real hardware
// action (drop the chip select, keep interrupt masking), not a guess.
//
// This is a byte-for-byte structural copy of the translated PPC body (build/nsmbw/functions/
// func_801B9830.cpp) - every branch, comparison, sub-call and bit operation is unchanged,
// including the InvokeDirectCpu<> calls into other translated functions. The ONLY edit: the 2
// FlatRead32/FlatWrite32 calls that touch EXI's real hardware register (r3-relative, base
// 0xCD006800 + channel*20) are swapped for Memory::Read32/Write32 (the checked path, dispatching
// to runtime/src/hle/exi.cpp's register-block backing, the same block func_801B9700/EXISelect's
// own override already writes through) - see pi.cpp's comment for why FlatRead/FlatWrite against
// real MMIO always hits an unrecoverable hardware fault on this runtime. Every other
// FlatRead32/FlatWrite32/FlatWriteRam32 call here (stack spill/restore, the per-channel RAM state
// struct at r31) is genuine RAM access, left untouched.
#include "hle_stubs.h"
#include "memory.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

extern "C" void EXIDeselect_NSMBW_801B9830(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r0_rot_0 = 0;
    uint32_t r3_rot_0 = 0;
    uint32_t r3_rot_1 = 0;
    uint32_t r3_rot_2 = 0;
    uint32_t r3_rot_3 = 0;
    uint32_t r3_rot_4 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r28 = ctx->gpr[28];
    uint32_t r29 = ctx->gpr[29];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t xer = ctx->xer;

    goto loc_801B9830;

loc_801B9830:
{
    MemoryInline::FlatWriteRam32((r1 + -32), r1);
    r1 = (r1 + -32);
    r0 = ctx->lr;
    MemoryInline::FlatWriteRam32((r1 + 36), r0);
    MemoryInline::FlatWriteRam32((r1 + 28), r31);
    MemoryInline::FlatWriteRam32((r1 + 24), r30);
    MemoryInline::FlatWriteRam32((r1 + 20), r29);
    MemoryInline::FlatWriteRam32((r1 + 16), r28);
    r28 = r3;
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
    r29 = r3;
    r0 = MemoryInline::FlatRead32((r31 + 12));
    r0 = (r0 & 4);
}

loc_801B9870:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B9880;
    }
}

loc_801B9874:
{
    // inline leaf 0x801B12C0 (9 guest instruction(s))
}

loc_inl1_0x801B12C0:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_inl1_0x801B12C4:
{
    r4 = ctx->msr;
    if (((cr & 0x20000000u) != 0)) {
        goto loc_inl1_0x801B12D4;
    }
}

loc_inl1_0x801B12CC:
{
    r5 = (r4 | 32768);
    goto loc_inl1_0x801B12D8;
}

loc_inl1_0x801B12D4:
{
    r5 = (r4 & -32769);
}

loc_inl1_0x801B12D8:
{
    ctx->msr = r5;
    r3_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r4), static_cast<uint32_t>(17));
    r3 = (r3_rot_1 & 1);
}

loc_inl1_cont_801B12C0:
{
    // end of inlined leaf 0x801B12C0
    r3 = 0;
    goto loc_801B990C;
}

loc_801B9880:
{
    r0 = MemoryInline::FlatRead32((r31 + 12));
    r0 = (r0 & -5);
    MemoryInline::FlatWrite32((r31 + 12), r0);
    r3 = (r28 * 20);
    r0 = -855638016;
    r3 = (r0 + r3);
    r30 = Memory::Read32(static_cast<uint32_t>(r3 + 26624));
    r0 = (r30 & 1029);
}

loc_801B98A0:
{
    Memory::Write32(static_cast<uint32_t>(r3 + 26624), r0);
    r0 = MemoryInline::FlatRead32((r31 + 12));
    r0 = (r0 & 8);
}

loc_801B98AC:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_801B98D8;
    }
}

loc_801B98B0:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r28), static_cast<int32_t>(0));
}

loc_801B98B4:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801B98C4;
    }
}

loc_801B98B8:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r28), static_cast<int32_t>(1));
}

loc_801B98BC:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801B98D0;
    }
}

loc_801B98C0:
{
    goto loc_801B98D8;
}

loc_801B98C4:
{
    r3 = 1048576;
    ctx->lr = 0x801B98CCu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
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
    goto loc_801B98D8;
}

loc_801B98D0:
{
    r3 = 131072;
    ctx->lr = 0x801B98D8u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
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

loc_801B98D8:
{
    r3 = r29;
    // inline leaf 0x801B12C0 (9 guest instruction(s))
}

loc_inl2_0x801B12C0:
{
}

loc_inl2_0x801B12C4:
{
    r4 = ctx->msr;
    if ((static_cast<int32_t>(r3) == static_cast<int32_t>(0))) {
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
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r28), static_cast<int32_t>(2));
}

loc_801B98E4:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801B9908;
    }
}

loc_801B98E8:
{
    r0 = (r30 & 128);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_801B98EC:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801B9908;
    }
}

loc_801B98F0:
{
    r3 = r28;
    ctx->lr = 0x801B98F8u;
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
    r0 = (0 - r3);
    r0 = (r0 | r3);
    r3_rot_3 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(1));
    r3 = (r3_rot_3 & 1);
    goto loc_801B990C;
}

loc_801B9908:
{
    r3 = 1;
}

loc_801B990C:
{
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

PPC_NATIVE_OVERRIDE_VOID(801B9830, EXIDeselect_NSMBW_801B9830, (CpuContext* ctx), (ctx));
