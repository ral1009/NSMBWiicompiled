// Diagnostic (not a fix): NSMBW_LOG_MSG=1 logs every EGG::MsgRes::getMsg (0x802D7B50) lookup -
// the requested message (getMsg__Q23EGG6MsgResFUlUl: r4 = BMG group, r5 = index within it), the
// entry the lookup found (0 = not in the file) and the text pointer it returns. Purpose: the
// pause menu's Exit confirmation in an uncleared course
// (2026-09-28) drew no text, and the log showed setText reading an unmapped pointer
// (0xE3608478) returned from getMsg - this names the message that was asked for.
//
// Structural copy of the translated body (build/nsmbw/functions/func_802D7B50.cpp): call the
// entry lookup 0x802D7C90, then text = *(this + 8) + *entry + 8. Only the log line is added.
#include "hle_stubs.h"
#include <aurora/env.hpp>
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include "recomp_mod_loader.h"

#include <cstdint>
#include <cstdio>

extern "C" void NsmbwGetMsgDiag_802D7B50(CpuContext* MKW_RESTRICT ctx)
{
    static const bool log = AURORA_ENV("NSMBW_LOG_MSG") != nullptr;
    const uint32_t group = ctx->gpr[4], index = ctx->gpr[5];

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r31 = ctx->gpr[31];

    MemoryInline::FlatWriteRam32((r1 + -16), r1);
    r1 = (r1 + -16);
    r0 = ctx->lr;
    MemoryInline::FlatWriteRam32((r1 + 20), r0);
    MemoryInline::FlatWriteRam32((r1 + 12), r31);
    r31 = r3;
    ctx->lr = 0x802D7B68u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[31] = r31;
    InvokeDirectCpu<0x802D7C90u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r31 = ctx->gpr[31];
    const uint32_t entry = r3;
    r4 = MemoryInline::FlatRead32((r31 + 8));
    r0 = MemoryInline::FlatRead32(r3);
    r31 = MemoryInline::FlatRead32((r1 + 12));
    r3 = (r4 + r0);
    r0 = MemoryInline::FlatRead32((r1 + 20));
    r3 = (r3 + 8);
    ctx->lr = r0;
    r1 = (r1 + 16);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[31] = r31;

    if (log) {
        std::fprintf(stderr, "[nsmbw][msg] getMsg group=%u index=%u entry=0x%08X text=0x%08X lr=0x%08X\n",
                     group, index, entry, r3, r0);
        // On a failed lookup: the stage's yes/no window (*(*(r13-21720) + 4612), as 0x800D1010
        // opens it) with its mType (+652), and dYesNoWindow's MainMsgIDs table as the text setup
        // 0x80769130 reads it (0x80770000 - 23160 + 112).
        if (entry == 0) {
            uint32_t base = 0, win = 0, type = 0;
            if (Memory::TryRead32(0x8042F980u - 21720u, base) && base != 0 && Memory::TryRead32(base + 4612u, win) &&
                win != 0) {
                Memory::TryRead32(win + 652u, type);
            }
            std::fprintf(stderr, "[nsmbw][msg]   stage yes/no window 0x%08X mType=%u (0x%08X); MainMsgIDs@0x8076A5F8:", win, type,
                         type);
            for (uint32_t i = 0; i < 29; ++i) {
                uint32_t v = 0;
                Memory::TryRead32(0x8076A5F8u + i * 4u, v);
                std::fprintf(stderr, " %u", v);
            }
            std::fprintf(stderr, "\n");
        }
    }
}
PPC_NATIVE_OVERRIDE_VOID(802D7B50, NsmbwGetMsgDiag_802D7B50, (CpuContext* ctx), (ctx));
