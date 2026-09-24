// Diagnostic (not a fix): NSMBW_LOG_STATE_CHANGES prints every state-machine transition by
// name. Set it to "all" to include actor state machines; any other value logs only scene
// classes (names starting "dSc"). Purpose: find which state a scene is stuck in (GAME_SETUP
// after player-count select; BOOT never leaving the strap without input) without needing the
// undecompiled scene code's own addresses.
//
// Hook point: sStateMethod_c::changeStateMethod (0x8015FD50, function_map.txt line 3473) - the
// one non-template function every sStateMgr_c<...>::changeState() funnels through
// (NSMBW-Decomp source/dol/sLib/s_StateMethod.cpp):
//   void sStateMethod_c::changeStateMethod(const sStateIDIf_c &newID) {
//       if (!newID.isNull()) {
//           mpNewStateID = &newID;
//           changeStateLocalMethod(newID);
//           mStateChanged = true;
//       }
//   }
// Since an override replaces the translated body, it is reimplemented here exactly. Offsets and
// vtable slots read off the translated func_8015FD50:
//   r12=[r31]; r12=[r12+12]; bctrl        -> newID.isNull()             (sStateIDIf_c vtable +0x0C)
//   stw r31, 20(r30)                      -> mpNewStateID = &newID       (this + 0x14)
//   r12=[r30]; r12=[r12+60]; bctrl        -> changeStateLocalMethod()    (sStateMethodIf_c vtable +0x3C)
//   stb r0, 15(r30)  (r0 = 1)             -> mStateChanged = true        (this + 0x0F)
// sStateID_c (include/game/sLib/s_StateID.hpp): vtable +0, mpName +4; sFStateID_c<T> derives
// from it with the same layout, and every STATE_DEFINE name is "<class>::StateID_<name>".
#include "hle_stubs.h"
#include <aurora/env.hpp>
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" uint32_t g_nsmbwCurrentSceneProfile;

namespace {
constexpr uint32_t kIsNullSlot = 0x0Cu;
constexpr uint32_t kChangeStateLocalSlot = 0x3Cu;
constexpr uint32_t kNewStateIdOff = 0x14u;
constexpr uint32_t kStateIdOff = 0x1Cu;
constexpr uint32_t kStateChangedOff = 0x0Fu;
constexpr uint32_t kStateIdNameOff = 0x4u;

int LogMode() { // 0 = off, 1 = scenes only, 2 = everything
    static const int mode = [] {
        const char* v = AURORA_ENV("NSMBW_LOG_STATE_CHANGES");
        if (!v) return 0;
        return std::strcmp(v, "all") == 0 ? 2 : 1;
    }();
    return mode;
}

// Copies the state ID's name out of guest memory; "?" if anything is unreadable.
void ReadStateName(uint32_t idPtr, char* out, size_t cap) {
    out[0] = '?'; out[1] = 0;
    uint32_t namePtr = 0;
    if (idPtr == 0 || !Memory::TryRead32(idPtr + kStateIdNameOff, namePtr) || namePtr == 0) return;
    try {
        size_t i = 0;
        for (; i + 1 < cap; ++i) {
            const uint8_t c = Memory::Read8(namePtr + static_cast<uint32_t>(i));
            if (c == 0 || c < 0x20 || c > 0x7E) break;
            out[i] = static_cast<char>(c);
        }
        out[i] = 0;
        if (i == 0) { out[0] = '?'; out[1] = 0; }
    } catch (const Memory::AccessViolation&) {
        out[0] = '?'; out[1] = 0;
    }
}

uint32_t CallVirtual(uint32_t objPtr, uint32_t slot, uint32_t arg) {
    uint32_t vtable = 0, fn = 0;
    if (!Memory::TryRead32(objPtr, vtable) || !Memory::TryRead32(vtable + slot, fn) || fn == 0) return 0;
    auto& cpu = GetPersistentCpuContext();
    cpu.gpr[3] = objPtr;
    cpu.gpr[4] = arg;
    InvokeIndirectCpu(fn, &cpu);
    return cpu.gpr[3];
}
} // namespace

extern "C" void StateChangeDiag_8015FD50(uint32_t thisPtr, uint32_t newIdPtr)
{
    const bool isNull = (CallVirtual(newIdPtr, kIsNullSlot, 0) & 0xFFu) != 0;
    if (isNull) return;

    if (LogMode() != 0) {
        static uint32_t seq = 0;
        char oldName[96], newName[96];
        uint32_t oldId = 0;
        Memory::TryRead32(thisPtr + kStateIdOff, oldId);
        ReadStateName(oldId, oldName, sizeof(oldName));
        ReadStateName(newIdPtr, newName, sizeof(newName));
        const bool isScene = std::strncmp(newName, "dSc", 3) == 0 || std::strncmp(oldName, "dSc", 3) == 0;
        if (LogMode() == 2 || isScene) {
            std::fprintf(stderr, "[nsmbw][state] #%u scene=%u mgr=0x%08X %s -> %s\n",
                         ++seq, g_nsmbwCurrentSceneProfile, thisPtr, oldName, newName);
            std::fflush(stderr);
        }
    }

    try {
        Memory::Write32(thisPtr + kNewStateIdOff, newIdPtr);
    } catch (const Memory::AccessViolation&) {}
    CallVirtual(thisPtr, kChangeStateLocalSlot, newIdPtr);
    try {
        Memory::Write8(thisPtr + kStateChangedOff, 1);
    } catch (const Memory::AccessViolation&) {}
}
PPC_NATIVE_OVERRIDE_VOID(8015FD50, StateChangeDiag_8015FD50, (uint32_t thisPtr, uint32_t newIdPtr), (thisPtr, newIdPtr));
