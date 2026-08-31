// Native override for NSMBW's own compiled __OSMaskInterrupts/__OSUnmaskInterrupts body
// (0x801B13F0). Confirmed against the real SDK's declared enum in NSMBW-Decomp's
// include/lib/revolution/OS/OSInterrupt.h (OSInterruptType: MEM_0..3/ADDRESS, DSP_AI/ARAM/DSP,
// AI_AI, EXI_0..2_*, PI_*) - r3 is an interrupt bitmask (OS_INTR_MASK(intr) = 1 << (31-intr)),
// cntlzw(r3) recovers the interrupt index, and each branch below reads-modifies-writes the one
// hardware register that interrupt line's mask bit lives in: MI_INTMR (0xCC00401C), DSP_CSR
// (0xCC00500A), AI_AICR (0xCD006C00), EXI channel 0's CPR (0xCD006800), or PI_INTMR (0xCC003004).
//
// This is a byte-for-byte structural copy of the translated PPC body (build/nsmbw/functions/
// func_801B13F0.cpp) - every branch, comparison and bit operation is unchanged. The ONLY edit is
// swapping MemoryInline::FlatRead16/32 and FlatWrite16/32 (the raw, unchecked flat-memory path)
// for Memory::Read16/32 and Memory::Write16/32 (the checked path). That substitution is the whole
// fix: FlatRead/FlatWrite against real MMIO hardware registers always hits a hardware page fault
// on this runtime (PAGE_NOACCESS is intentional - see guest_flat_memory.cpp), and recovering a
// read's value from a caught fault isn't feasible in general (no reliable way to decode which x86
// register an arbitrarily-optimized inlined memcpy targeted). The checked path instead dispatches
// to this runtime's own PI/MI/DSP/EXI/AI register-block backing (runtime/src/hle/{pi,mi,dsp,exi,
// ai}.cpp), which holds exactly what earlier __OSMaskInterrupts calls already wrote - the same
// flat-storage model already proven correct for PI_INTMR/MI_INTMR/IPC's PPCIRQMASK.
//
// Not re-deriving or guessing the bit semantics for any of the 32 interrupt lines: preserving the
// exact original control flow removes that risk entirely.
#include "hle_stubs.h"
#include "memory.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

extern "C" void OSMaskInterrupts_NSMBW_801B13F0(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t cr = ctx->cr;
    uint32_t xer = ctx->xer;

    goto loc_801B13F0;

loc_801B13F0:
{
    r0 = PPC_CntlzwInline(static_cast<uint32_t>(r3));
}

loc_801B13F8:
{
    if ((static_cast<int32_t>(r0) >= static_cast<int32_t>(12))) {
        goto loc_801B141C;
    }
}

loc_801B13FC:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(8));
}

loc_801B1400:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801B14CC;
    }
}

loc_801B1404:
{
    if (((cr & 0x80000000u) == 0)) {
        goto loc_801B14F8;
    }
}

loc_801B1408:
{
}

loc_801B140C:
{
    if ((static_cast<int32_t>(r0) >= static_cast<int32_t>(5))) {
        goto loc_801B148C;
    }
}

loc_801B1410:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_801B1414:
{
    if (((cr & 0x80000000u) == 0)) {
        goto loc_801B143C;
    }
}

loc_801B1418:
{
    ctx->gpr[0] = r0;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->cr = cr;
    return;
}

loc_801B141C:
{
}

loc_801B1420:
{
    if ((static_cast<int32_t>(r0) >= static_cast<int32_t>(17))) {
        goto loc_801B1430;
    }
}

loc_801B1424:
{
}

loc_801B1428:
{
    if ((static_cast<int32_t>(r0) >= static_cast<int32_t>(15))) {
        goto loc_801B1580;
    }
}

loc_801B142C:
{
    goto loc_801B153C;
}

loc_801B1430:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(28));
}

loc_801B1434:
{
    if (((cr & 0x80000000u) == 0)) {
        goto loc_return;
    }
}

loc_801B1438:
{
    goto loc_801B15B4;
}

loc_801B143C:
{
    r0 = (r4 & -2147483648);
}

loc_801B1440:
{
    r5 = 0;
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B144C;
    }
}

loc_801B1448:
{
    r5 = (r5 | 1);
}

loc_801B144C:
{
    r0 = (r4 & 1073741824);
}

loc_801B1450:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B1458;
    }
}

loc_801B1454:
{
    r5 = (r5 | 2);
}

loc_801B1458:
{
    r0 = (r4 & 536870912);
}

loc_801B145C:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B1464;
    }
}

loc_801B1460:
{
    r5 = (r5 | 4);
}

loc_801B1464:
{
    r0 = (r4 & 268435456);
}

loc_801B1468:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B1470;
    }
}

loc_801B146C:
{
    r5 = (r5 | 8);
}

loc_801B1470:
{
    r0 = (r4 & 134217728);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_801B1474:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801B147C;
    }
}

loc_801B1478:
{
    r5 = (r5 | 16);
}

loc_801B147C:
{
    r4 = -872415232;
    r3 = (r3 & 134217727);
    Memory::Write16(static_cast<uint32_t>(r4 + 16412), static_cast<uint16_t>(r5));
    ctx->gpr[0] = r0;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->cr = cr;
    return;
}

loc_801B148C:
{
    r5 = -872415232;
    r0 = (r4 & 67108864);
}

loc_801B1494:
{
    r5 = Memory::Read16(static_cast<uint32_t>(r5 + 20490));
    r5 = (r5 & -505);
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B14A4;
    }
}

loc_801B14A0:
{
    r5 = (r5 | 16);
}

loc_801B14A4:
{
    r0 = (r4 & 33554432);
}

loc_801B14A8:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B14B0;
    }
}

loc_801B14AC:
{
    r5 = (r5 | 64);
}

loc_801B14B0:
{
    r0 = (r4 & 16777216);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_801B14B4:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801B14BC;
    }
}

loc_801B14B8:
{
    r5 = (r5 | 256);
}

loc_801B14BC:
{
    r4 = -872415232;
    r3 = (r3 & -117440513);
    Memory::Write16(static_cast<uint32_t>(r4 + 20490), static_cast<uint16_t>(r5));
    ctx->gpr[0] = r0;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->cr = cr;
    return;
}

loc_801B14CC:
{
    r0 = (r4 & 8388608);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_801B14D0:
{
    r4 = -855638016;
    r5 = Memory::Read32(static_cast<uint32_t>(r4 + 27648));
    r0 = -45;
    r5 = (r5 & r0);
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801B14E8;
    }
}

loc_801B14E4:
{
    r5 = (r5 | 4);
}

loc_801B14E8:
{
    r4 = -855638016;
    r3 = (r3 & -8388609);
    Memory::Write32(static_cast<uint32_t>(r4 + 27648), r5);
    ctx->gpr[0] = r0;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->cr = cr;
    return;
}

loc_801B14F8:
{
    r0 = (r4 & 4194304);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_801B14FC:
{
    r5 = -855638016;
    r5 = Memory::Read32(static_cast<uint32_t>(r5 + 26624));
    r0 = -11280;
    r5 = (r5 & r0);
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801B1514;
    }
}

loc_801B1510:
{
    r5 = (r5 | 1);
}

loc_801B1514:
{
    r0 = (r4 & 2097152);
}

loc_801B1518:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B1520;
    }
}

loc_801B151C:
{
    r5 = (r5 | 4);
}

loc_801B1520:
{
    r0 = (r4 & 1048576);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_801B1524:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801B152C;
    }
}

loc_801B1528:
{
    r5 = (r5 | 1024);
}

loc_801B152C:
{
    r4 = -855638016;
    r3 = (r3 & -7340033);
    Memory::Write32(static_cast<uint32_t>(r4 + 26624), r5);
    ctx->gpr[0] = r0;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->cr = cr;
    return;
}

loc_801B153C:
{
    r0 = (r4 & 524288);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_801B1540:
{
    r5 = -855638016;
    r5 = Memory::Read32(static_cast<uint32_t>(r5 + 26644));
    r0 = -3088;
    r5 = (r5 & r0);
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801B1558;
    }
}

loc_801B1554:
{
    r5 = (r5 | 1);
}

loc_801B1558:
{
    r0 = (r4 & 262144);
}

loc_801B155C:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B1564;
    }
}

loc_801B1560:
{
    r5 = (r5 | 4);
}

loc_801B1564:
{
    r0 = (r4 & 131072);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_801B1568:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801B1570;
    }
}

loc_801B156C:
{
    r5 = (r5 | 1024);
}

loc_801B1570:
{
    r4 = -855638016;
    r3 = (r3 & -917505);
    Memory::Write32(static_cast<uint32_t>(r4 + 26644), r5);
    ctx->gpr[0] = r0;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->cr = cr;
    return;
}

loc_801B1580:
{
    r5 = -855638016;
    r0 = (r4 & 65536);
}

loc_801B1588:
{
    r5 = Memory::Read32(static_cast<uint32_t>(r5 + 26664));
    r5 = (r5 & -16);
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B1598;
    }
}

loc_801B1594:
{
    r5 = (r5 | 1);
}

loc_801B1598:
{
    r0 = (r4 & 32768);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_801B159C:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801B15A4;
    }
}

loc_801B15A0:
{
    r5 = (r5 | 4);
}

loc_801B15A4:
{
    r4 = -855638016;
    r3 = (r3 & -98305);
    Memory::Write32(static_cast<uint32_t>(r4 + 26664), r5);
    ctx->gpr[0] = r0;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->cr = cr;
    return;
}

loc_801B15B4:
{
    r0 = (r4 & 16384);
}

loc_801B15B8:
{
    r5 = 240;
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B15C4;
    }
}

loc_801B15C0:
{
    r5 = (r5 | 2048);
}

loc_801B15C4:
{
    r0 = (r4 & 2048);
}

loc_801B15C8:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B15D0;
    }
}

loc_801B15CC:
{
    r5 = (r5 | 8);
}

loc_801B15D0:
{
    r0 = (r4 & 1024);
}

loc_801B15D4:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B15DC;
    }
}

loc_801B15D8:
{
    r5 = (r5 | 4);
}

loc_801B15DC:
{
    r0 = (r4 & 512);
}

loc_801B15E0:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B15E8;
    }
}

loc_801B15E4:
{
    r5 = (r5 | 2);
}

loc_801B15E8:
{
    r0 = (r4 & 256);
}

loc_801B15EC:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B15F4;
    }
}

loc_801B15F0:
{
    r5 = (r5 | 1);
}

loc_801B15F4:
{
    r0 = (r4 & 128);
}

loc_801B15F8:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B1600;
    }
}

loc_801B15FC:
{
    r5 = (r5 | 256);
}

loc_801B1600:
{
    r0 = (r4 & 64);
}

loc_801B1604:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B160C;
    }
}

loc_801B1608:
{
    r5 = (r5 | 4096);
}

loc_801B160C:
{
    r0 = (r4 & 8192);
}

loc_801B1610:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B1618;
    }
}

loc_801B1614:
{
    r5 = (r5 | 512);
}

loc_801B1618:
{
    r0 = (r4 & 4096);
}

loc_801B161C:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B1624;
    }
}

loc_801B1620:
{
    r5 = (r5 | 1024);
}

loc_801B1624:
{
    r0 = (r4 & 32);
}

loc_801B1628:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B1630;
    }
}

loc_801B162C:
{
    r5 = (r5 | 8192);
}

loc_801B1630:
{
    r0 = (r4 & 16);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_801B1634:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801B163C;
    }
}

loc_801B1638:
{
    r5 = (r5 | 16384);
}

loc_801B163C:
{
    r4 = -872415232;
    r3 = (r3 & -32753);
    Memory::Write32(static_cast<uint32_t>(r4 + 12292), r5);
    ctx->gpr[0] = r0;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->cr = cr;
    return;
}

loc_return:
{
    ctx->gpr[0] = r0;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->cr = cr;
    return;
}
}

PPC_NATIVE_OVERRIDE_VOID(801B13F0, OSMaskInterrupts_NSMBW_801B13F0, (CpuContext* ctx), (ctx));
