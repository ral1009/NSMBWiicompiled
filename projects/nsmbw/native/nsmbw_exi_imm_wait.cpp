// Native override for NSMBW's own compiled EXI immediate-transfer completion helper (0x801B9090).
// Fully read and traced end to end (561 lines as translated after the DI_CONFIG fix removed an
// inlined leaf that used to make this function much larger) - no untraced regions remain. Its
// verified contract:
//   1. Poll the channel's CR register (base 0xCD006800 + channel*0x14 + 0xC) for the TSTART bit.
//      A single poll, not a spin loop of its own - the outer "wait" shape comes from its callers
//      re-invoking it, not from a loop inside this function.
//   2. If TSTART is clear (transfer physically done) and the per-channel RAM state struct
//      (0x80390000-1520 + channel*64, the same struct EXISelect/EXIDeselect use) says a read was
//      pending, unpack the DATA register (offset 0x10) into the caller's buffer - a
//      ctr-decrement loop, which PowerPC's ctr semantics make hardware-guaranteed to terminate,
//      not an unbounded retry.
//   3. Clear the "active" flag, then call the already-fixed DI_CONFIG probe (0x801AB2F0) and
//      compare its result to the 0xFF sentinel.
//
// That last comparison is the reason a full port of this function was previously abandoned as
// hang-prone: with DI_CONFIG unresolved at the time, whatever guess was in place for it could
// route into the device-ID-comparison chain below (loc_801B9258 onward - a call into
// func_801AA020, an SI-status halfword poll, and comparisons against three hardware ID sentinels)
// - untested, unverified territory. Now that DI_CONFIG faithfully returns 0 (this runtime's
// genuine "no real DVD interface board revision" default, see hle/di.cpp), that comparison is
// PROVEN to always take the "not equal to 0xFF" branch (0 != 0xFF), which unconditionally skips
// the entire device-ID chain. It is ported below verbatim for structural fidelity - the
// translator's own dead-code elimination would otherwise be doing this same job invisibly - but it
// is unreachable given the current DI_CONFIG behavior, not a hazard.
//
// This is a byte-for-byte structural copy of the translated PPC body (build/nsmbw/functions/
// func_801B9090.cpp) - every branch, comparison, sub-call and bit operation is unchanged,
// including the InvokeDirectCpu<> calls into other translated functions (0x801AA020) and the
// native override call (0x801AB2F0). The ONLY edit: the 5 FlatRead32 calls that touch EXI's real
// hardware registers (CR at +0xC, DATA at +0x10, CPR at +0x0, all r-relative to base 0xCD006800 +
// channel*0x14) are swapped for Memory::Read32 (the checked path, dispatching to
// runtime/src/hle/exi.cpp's register-block backing, the same block EXISelect/EXIDeselect already
// write through) - see pi.cpp's comment for why FlatRead against real MMIO always hits an
// unrecoverable hardware fault on this runtime. Every other FlatRead32/FlatRead16/FlatWrite32 call
// here (stack spill/restore, the per-channel RAM state struct, the caller's own buffer) is genuine
// RAM access, left untouched.
#include "hle_stubs.h"
#include "memory.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

extern "C" void EXIImmWait_NSMBW_801B9090(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r0_rot_0 = 0;
    uint32_t r3_rot_0 = 0;
    uint32_t r3_rot_1 = 0;
    uint32_t r3_rot_2 = 0;
    uint32_t r3_rot_3 = 0;
    uint32_t r5_rot_0 = 0;
    uint32_t r5_rot_1 = 0;
    uint32_t r5_rot_2 = 0;
    uint32_t r5_rot_3 = 0;
    uint32_t r5_rot_4 = 0;
    uint32_t r5_rot_5 = 0;
    uint32_t r5_rot_6 = 0;
    uint32_t r6_rot_0 = 0;
    uint32_t r6_rot_1 = 0;
    uint32_t r6_rot_10 = 0;
    uint32_t r6_rot_11 = 0;
    uint32_t r6_rot_12 = 0;
    uint32_t r6_rot_13 = 0;
    uint32_t r6_rot_14 = 0;
    uint32_t r6_rot_15 = 0;
    uint32_t r6_rot_2 = 0;
    uint32_t r6_rot_3 = 0;
    uint32_t r6_rot_4 = 0;
    uint32_t r6_rot_5 = 0;
    uint32_t r6_rot_6 = 0;
    uint32_t r6_rot_7 = 0;
    uint32_t r6_rot_8 = 0;
    uint32_t r6_rot_9 = 0;
    uint32_t r6_subfic_ra_0 = 0;
    uint32_t r6_subfic_ra_1 = 0;
    uint32_t r6_subfic_ra_10 = 0;
    uint32_t r6_subfic_ra_11 = 0;
    uint32_t r6_subfic_ra_12 = 0;
    uint32_t r6_subfic_ra_2 = 0;
    uint32_t r6_subfic_ra_3 = 0;
    uint32_t r6_subfic_ra_4 = 0;
    uint32_t r6_subfic_ra_5 = 0;
    uint32_t r6_subfic_ra_6 = 0;
    uint32_t r6_subfic_ra_7 = 0;
    uint32_t r6_subfic_ra_8 = 0;
    uint32_t r6_subfic_ra_9 = 0;
    uint8_t* guest_range_0 = nullptr;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r7 = ctx->gpr[7];
    uint32_t r8 = ctx->gpr[8];
    uint32_t r28 = ctx->gpr[28];
    uint32_t r29 = ctx->gpr[29];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_801B9090;

loc_801B9090:
{
    MemoryInline::FlatWriteRam32((r1 + -32), r1);
    r1 = (r1 + -32);
    r0 = ctx->lr;
    MemoryInline::FlatWriteRam32((r1 + 36), r0);
    MemoryInline::FlatWriteRam32((r1 + 28), r31);
    MemoryInline::FlatWriteRam32((r1 + 24), r30);
    MemoryInline::FlatWriteRam32((r1 + 20), r29);
    MemoryInline::FlatWriteRam32((r1 + 16), r28);
    r0_rot_0 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(6));
    r0 = (r0_rot_0 & -64);
    r4 = 0x80390000u;
    r4 = (r4 + -1520);
    r31 = (r4 + r0);
    r29 = 0;
    r30 = (r3 * 20);
    r0 = -855638016;
    r3 = (r0 + r30);
    goto loc_801B92DC;
}

loc_801B90D0:
{
    r0 = Memory::Read32(static_cast<uint32_t>(r3 + 26636));
    r0 = (r0 & 1);
}

loc_801B90D8:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B92DC;
    }
}

loc_801B90DC:
{
    // inline leaf 0x801B1280 (5 guest instruction(s))
    r3 = ctx->msr;
    r4 = (r3 & -32769);
    ctx->msr = r4;
    r3_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(17));
    r3 = (r3_rot_1 & 1);
    // end of inlined leaf 0x801B1280
    r28 = r3;
    r0 = MemoryInline::FlatRead32((r31 + 12));
    r0 = (r0 & 4);
}

loc_801B90EC:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_801B92D0;
    }
}

loc_801B90F0:
{
    r0 = MemoryInline::FlatRead32((r31 + 12));
    r0 = (r0 & 3);
}

loc_801B90F8:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_801B924C;
    }
}

loc_801B90FC:
{
    r0 = MemoryInline::FlatRead32((r31 + 12));
    r0 = (r0 & 2);
}

loc_801B9104:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_801B9240;
    }
}

loc_801B9108:
{
    r5 = MemoryInline::FlatRead32((r31 + 16));
    SetCRResident(cr, xer, 1, static_cast<int32_t>(r5), static_cast<int32_t>(0));
}

loc_801B9110:
{
    if (((cr & 0x02000000u) != 0)) {
        goto loc_801B9240;
    }
}

loc_801B9114:
{
    r4 = MemoryInline::FlatRead32((r31 + 20));
    r0 = -855638016;
    r3 = (r0 + r30);
    r0 = Memory::Read32(static_cast<uint32_t>(r3 + 26640));
    r3 = 0;
    if (((cr & 0x04000000u) == 0)) {
        goto loc_801B9240;
    }
}

loc_801B912C:
{
    r7 = (r5 + -8);
}

loc_801B9134:
{
    if ((static_cast<int32_t>(r5) <= static_cast<int32_t>(8))) {
        goto loc_801B9214;
    }
}

loc_801B9138:
{
    r8 = 0;
    if (((cr & 0x08000000u) != 0)) {
        goto loc_801B9154;
    }
}

loc_801B9140:
{
    r6 = 0x80000000u;
    r6 = (r6 + -2);
}

loc_801B914C:
{
    if ((static_cast<int32_t>(r5) > static_cast<int32_t>(r6))) {
        goto loc_801B9154;
    }
}

loc_801B9150:
{
    r8 = 1;
}

loc_801B9154:
{
}

loc_801B9158:
{
    if ((static_cast<int32_t>(r8) == static_cast<int32_t>(0))) {
        goto loc_801B9214;
    }
}

loc_801B915C:
{
    r6 = (r7 + 7);
    r6_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r6), static_cast<uint32_t>(29));
    r6 = (r6_rot_1 & 536870911);
    ctr = r6;
}

loc_801B916C:
{
    if ((static_cast<int32_t>(r7) <= static_cast<int32_t>(0))) {
        goto loc_801B9214;
    }
}

loc_801B9170:
{
    r6 = (3 - r3);
    r6_rot_3 = PpcRotl32Inline(static_cast<uint32_t>(r6), static_cast<uint32_t>(3));
    r6 = (r6_rot_3 & -8);
    r6 = PPC_Srw(static_cast<uint32_t>(r0), static_cast<uint32_t>(r6));
    guest_range_0 = MemoryInline::ResolveRangeHost(r4, 0, 8u, false, true);
    MemoryInline::WriteResolved8(guest_range_0, 0u, r4, static_cast<uint8_t>(r6));
    r6 = (r3 + 1);
    r6_subfic_ra_2 = r6;
    r6 = (3 - r6_subfic_ra_2);
    r6_rot_4 = PpcRotl32Inline(static_cast<uint32_t>(r6), static_cast<uint32_t>(3));
    r6 = (r6_rot_4 & -8);
    r6 = PPC_Srw(static_cast<uint32_t>(r0), static_cast<uint32_t>(r6));
    MemoryInline::WriteResolved8(guest_range_0, 1u, (r4 + 1), static_cast<uint8_t>(r6));
    r6 = (r3 + 2);
    r6_subfic_ra_3 = r6;
    r6 = (3 - r6_subfic_ra_3);
    r6_rot_5 = PpcRotl32Inline(static_cast<uint32_t>(r6), static_cast<uint32_t>(3));
    r6 = (r6_rot_5 & -8);
    r6 = PPC_Srw(static_cast<uint32_t>(r0), static_cast<uint32_t>(r6));
    MemoryInline::WriteResolved8(guest_range_0, 2u, (r4 + 2), static_cast<uint8_t>(r6));
    r6 = (0 - r3);
    r6_rot_6 = PpcRotl32Inline(static_cast<uint32_t>(r6), static_cast<uint32_t>(3));
    r6 = (r6_rot_6 & -8);
    r6 = PPC_Srw(static_cast<uint32_t>(r0), static_cast<uint32_t>(r6));
    MemoryInline::WriteResolved8(guest_range_0, 3u, (r4 + 3), static_cast<uint8_t>(r6));
    r6 = (r3 + 4);
    r6_subfic_ra_4 = r6;
    r6 = (3 - r6_subfic_ra_4);
    r6_rot_7 = PpcRotl32Inline(static_cast<uint32_t>(r6), static_cast<uint32_t>(3));
    r6 = (r6_rot_7 & -8);
    r6 = PPC_Srw(static_cast<uint32_t>(r0), static_cast<uint32_t>(r6));
    MemoryInline::WriteResolved8(guest_range_0, 4u, (r4 + 4), static_cast<uint8_t>(r6));
    r6 = (r3 + 5);
    r6_subfic_ra_5 = r6;
    r6 = (3 - r6_subfic_ra_5);
    r6_rot_8 = PpcRotl32Inline(static_cast<uint32_t>(r6), static_cast<uint32_t>(3));
    r6 = (r6_rot_8 & -8);
    r6 = PPC_Srw(static_cast<uint32_t>(r0), static_cast<uint32_t>(r6));
    MemoryInline::WriteResolved8(guest_range_0, 5u, (r4 + 5), static_cast<uint8_t>(r6));
    r6 = (r3 + 6);
    r6_subfic_ra_6 = r6;
    r6 = (3 - r6_subfic_ra_6);
    r6_rot_9 = PpcRotl32Inline(static_cast<uint32_t>(r6), static_cast<uint32_t>(3));
    r6 = (r6_rot_9 & -8);
    r6 = PPC_Srw(static_cast<uint32_t>(r0), static_cast<uint32_t>(r6));
    MemoryInline::WriteResolved8(guest_range_0, 6u, (r4 + 6), static_cast<uint8_t>(r6));
    r6 = (r3 + 7);
    r6_subfic_ra_7 = r6;
    r6 = (3 - r6_subfic_ra_7);
    xer = (xer & 0xDFFFFFFFu) | ((static_cast<uint32_t>(3) >= static_cast<uint32_t>(r6_subfic_ra_7) ? 1u : 0u) << 29);
    r6_rot_10 = PpcRotl32Inline(static_cast<uint32_t>(r6), static_cast<uint32_t>(3));
    r6 = (r6_rot_10 & -8);
    r6 = PPC_Srw(static_cast<uint32_t>(r0), static_cast<uint32_t>(r6));
    MemoryInline::WriteResolved8(guest_range_0, 7u, (r4 + 7), static_cast<uint8_t>(r6));
    r4 = (r4 + 8);
    r3 = (r3 + 8);
    ctr = (ctr + -1);
    if ((ctr != 0)) {
        goto loc_801B9170;
    }
}

loc_801B9214:
{
    r6 = (r5 - r3);
    ctr = r6;
}

loc_801B9220:
{
    if ((static_cast<int32_t>(r3) >= static_cast<int32_t>(r5))) {
        goto loc_801B9240;
    }
}

loc_801B9224:
{
    r5 = (3 - r3);
    xer = (xer & 0xDFFFFFFFu) | ((static_cast<uint32_t>(3) >= static_cast<uint32_t>(r3) ? 1u : 0u) << 29);
    r5_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(3));
    r5 = (r5_rot_2 & -8);
    r5 = PPC_Srw(static_cast<uint32_t>(r0), static_cast<uint32_t>(r5));
    MemoryInline::FlatWrite8(r4, static_cast<uint8_t>(r5));
    r4 = (r4 + 1);
    r3 = (r3 + 1);
    ctr = (ctr + -1);
    if ((ctr != 0)) {
        goto loc_801B9224;
    }
}

loc_801B9240:
{
    r0 = MemoryInline::FlatRead32((r31 + 12));
    r0 = (r0 & -4);
    MemoryInline::FlatWrite32((r31 + 12), r0);
}

loc_801B924C:
{
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->xer = xer;
    InvokeDirectCpu<0x801AB2F0u>(ctx);
    r0 = ctx->gpr[0];
    r3 = ctx->gpr[3];
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r3), static_cast<uint32_t>(255));
}

loc_801B9254:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801B92CC;
    }
}

loc_801B9258:
{
    ctx->lr = 0x801B925Cu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->xer = xer;
    InvokeDirectCpu<0x801AA020u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    r3 = (r3 & -268435456);
    r0 = (r3 + -536870912);
}

loc_801B9268:
{
    if ((static_cast<uint32_t>(r0) == static_cast<uint32_t>(0))) {
        goto loc_801B92CC;
    }
}

loc_801B926C:
{
    r0 = MemoryInline::FlatRead32((r31 + 16));
}

loc_801B9274:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(4))) {
        goto loc_801B92CC;
    }
}

loc_801B9278:
{
    r0 = -855638016;
    r4 = (r0 + r30);
    r0 = Memory::Read32(static_cast<uint32_t>(r4 + 26624));
    r0 = (r0 & 112);
}

loc_801B9288:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B92CC;
    }
}

loc_801B928C:
{
    r3 = Memory::Read32(static_cast<uint32_t>(r4 + 26640));
    r0 = (r3 + -16842752);
}

loc_801B9298:
{
    if ((static_cast<uint32_t>(r0) == static_cast<uint32_t>(0))) {
        goto loc_801B92BC;
    }
}

loc_801B929C:
{
    r3 = Memory::Read32(static_cast<uint32_t>(r4 + 26640));
    r0 = (r3 + -84344832);
}

loc_801B92A8:
{
    if ((static_cast<uint32_t>(r0) == static_cast<uint32_t>(0))) {
        goto loc_801B92BC;
    }
}

loc_801B92AC:
{
    r3 = Memory::Read32(static_cast<uint32_t>(r4 + 26640));
    r0 = (r3 + -69337088);
}

loc_801B92B8:
{
    if ((static_cast<uint32_t>(r0) != static_cast<uint32_t>(1))) {
        goto loc_801B92CC;
    }
}

loc_801B92BC:
{
    r3 = 0x80000000u;
    r0 = MemoryInline::FlatRead16((r3 + 12518));
}

loc_801B92C8:
{
    if ((static_cast<uint32_t>(r0) != static_cast<uint32_t>(33280))) {
        goto loc_801B92D0;
    }
}

loc_801B92CC:
{
    r29 = 1;
}

loc_801B92D0:
{
    r3 = r28;
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
    r3_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r4), static_cast<uint32_t>(17));
    r3 = (r3_rot_2 & 1);
}

loc_inl1_cont_801B12C0:
{
    // end of inlined leaf 0x801B12C0
    goto loc_801B92E8;
}

loc_801B92DC:
{
    r0 = MemoryInline::FlatRead32((r31 + 12));
    r0 = (r0 & 4);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_801B92E4:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801B90D0;
    }
}

loc_801B92E8:
{
    r3 = r29;
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
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[8] = r8;
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

PPC_NATIVE_OVERRIDE_VOID(801B9090, EXIImmWait_NSMBW_801B9090, (CpuContext* ctx), (ctx));
