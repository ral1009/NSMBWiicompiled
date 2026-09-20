// Diagnostic (not a fix), continuing past nsmbw_scboot_dodelete_diag.cpp: that hook proved
// dScBoot_c::doDelete() succeeds on its very first call (myBackGround_Phase was already DONE by
// the time deleteRequest() fired), so dScBoot_c finishes deleting cleanly. Yet no new scene's
// checkChildProcessCreateState() call ever appears afterward, and dFader_c::calc() keeps running
// forever at mStatus=OPAQUE with nothing to advance it.
//
// The missing link is dSys_c::execute() (NSMBW-Decomp source/dol/bases/d_system.cpp:262-267):
//   if (!inHbm && !dvdErrNew) {
//       if (myDylinkInitPhase.callMethod(nullptr) == sPhase_c::DONE) {
//           dScene_c::createNextScene();
//           fManager_c::mainLoop();   // <- drives every fBase_c's preExecute/execute, hence
//                                     //    checkChildProcessCreateState
//       }
//       dFader_c::calc();             // <- OUTSIDE that gate: runs every frame regardless
//   }
// dFader_c::calc() running forever while nothing else does is exactly consistent with
// createNextScene() failing every frame - it would explain every symptom seen so far without
// requiring mainLoop() itself to be broken.
//
// dScene_c::createNextScene() (0x800E1ED0, confirmed real address from
// projects/nsmbw/function_map.txt) disassembles to exactly the decompiled logic in
// source/dol/bases/d_scene.cpp:145-160:
//   lbz r0,-0x6f3a(r13); cmpwi r0,0; bne early-return         ; if (m_otherSceneFlg) return null
//   lhz r3,-0x6f40(r13); cmplwi r3,0x2ee; beq early-return    ; if (m_nextScene==INVALID) ditto
//   lwz r4,-0x5658(r13); li r5,1; bl 0x8006c6c0               ; dBase_c::createRoot(m_nextScene,
//                                                              ;   mPara, SCENE)
//   cmpwi r3,0; beq early-return-null                         ; <- if createRoot fails, silently
//                                                              ;    returns null WITHOUT clearing
//                                                              ;    m_nextScene, so this whole
//                                                              ;    sequence retries identically
//                                                              ;    every single frame forever
//   ...only on success: m_oldScene=m_nowScene; m_nowScene=m_nextScene; m_nextScene=INVALID;
//                        m_otherSceneFlg=true; return new scene
// giving these confirmed field addresses (all sda_base=0x8042f980 minus the disassembled offset):
//   m_otherSceneFlg @ 0x80428A46, m_nextScene @ 0x80428A40, m_nowScene @ 0x80428A42,
//   m_oldScene @ 0x80428A44, mPara @ 0x8042A328.
//
// dBase_c::createRoot (0x8006C6C0) is itself just `b 0x80162c60` (fBase_c::createRoot, an alias,
// no dBase_c-specific work), which tail-calls fBase_make (0x80162BB0). fBase_make's very first
// check is:
//   r7 = *(r13-0x52e8)         ; fProfListMg_c::m_data_p - SAME global (0x8042A698) that
//                              ; nsmbw_guest_ctor_hook.cpp's header comment already documents as
//                              ; having been found zeroed by OSInit wiping the REL images before
//                              ; RestoreRelImages() re-applies them
//   r0 = *(r7 + profName*4)    ; ->mBaseProfile
//   if (r0==0) return 0;       ; exactly fBase_make's documented null-profile early-out
// So this override does NOT reimplement fBase_make/createRoot's profile lookup at all - it
// delegates the entire call to the real, untouched dBase_c::createRoot (0x8006C6C0) via
// InvokeIndirectCpu, and only observes the (m_nextScene, createRoot-succeeded?) pair. If createRoot
// keeps returning null forever for the same m_nextScene value, that reopens the exact
// already-documented fProfListMg_c::m_data_p / REL-restoration bug class - just for a profile
// RestoreRelImages() apparently doesn't (yet) cover, rather than a new bug.
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"

#include <cstdio>

namespace {
constexpr uint32_t kOtherSceneFlgAddr = 0x80428A46u;
constexpr uint32_t kNextSceneAddr = 0x80428A40u;
constexpr uint32_t kNowSceneAddr = 0x80428A42u;
constexpr uint32_t kOldSceneAddr = 0x80428A44u;
constexpr uint32_t kParaAddr = 0x8042A328u;
constexpr uint32_t kSceneInvalid = 0x2EEu;
constexpr uint32_t kDBaseCreateRootAddr = 0x8006C6C0u;
constexpr uint32_t kGroupTypeScene = 1u;
}

// Exposed so other diagnostics (gx_texture.cpp's pane-identity survey) can gate on "which
// fProf::PROFILE_NAME_e scene is active right now" instead of guessing a GXLoadTexObj call-count
// offset. Set to the newly-active scene's profile id on every successful createNextScene()
// transition; never cleared, so it always reflects the most recent scene.
extern "C" uint32_t g_nsmbwCurrentSceneProfile = 0xFFFFFFFFu;

// TEMPORARY (GAME_SETUP-never-advances investigation): the live pointer to the most recently
// created GAME_SETUP (profile 0xA) scene object, so nsmbw_gamesetup_createpack_diag.cpp's
// fBase_c::createPack() hook can filter its log to just this one object out of every fBase_c in
// the game, instead of logging every object's create cycle. Heap addresses are only stable within
// one run, so this is captured fresh each time rather than hardcoded.
extern "C" uint32_t g_nsmbwGameSetupScenePtr = 0;

// Generic version of the above, for nsmbw_scene_chain_advance_diag.cpp's chained force-advance
// experiment: whichever scene is currently active, not just GAME_SETUP specifically.
extern "C" uint32_t g_nsmbwCurrentScenePtr = 0;

extern "C" uint32_t CreateNextScene_Diag_800E1ED0()
{
    const uint8_t otherSceneFlg = Memory::Read8(kOtherSceneFlgAddr);
    const uint16_t nextScene = Memory::Read16(kNextSceneAddr);

    static uint64_t callCount = 0;
    ++callCount;

    if (otherSceneFlg != 0 || nextScene == kSceneInvalid) {
        // Nothing queued / already mid-transition - the common per-frame case once a scene is
        // running normally. Log only occasionally so this doesn't spam during normal play.
        if (callCount <= 5 || (callCount % 300) == 0) {
            std::fprintf(stderr,
                "[nsmbw][diag] createNextScene() call #%llu: idle (otherSceneFlg=%u nextScene=0x%X)\n",
                static_cast<unsigned long long>(callCount), otherSceneFlg, nextScene);
            std::fflush(stderr);
        }
        return 0;
    }

    const uint32_t para = Memory::Read32(kParaAddr);

    auto& cpu = GetPersistentCpuContext();
    cpu.gpr[3] = nextScene;
    cpu.gpr[4] = para;
    cpu.gpr[5] = kGroupTypeScene;
    InvokeIndirectCpu(kDBaseCreateRootAddr, &cpu);
    const uint32_t newScene = cpu.gpr[3];

    static uint32_t lastLoggedNextScene = 0xFFFFFFFFu;
    static uint64_t sameFailureStreak = 0;
    const bool changed = (nextScene != lastLoggedNextScene);
    if (changed) {
        sameFailureStreak = 0;
    }
    ++sameFailureStreak;

    if (newScene == 0) {
        // The silent-failure path: createRoot() returned null, m_nextScene is NOT cleared by the
        // real logic below, so this exact call repeats next frame with the same nextScene value.
        if (changed || (sameFailureStreak % 60) == 0) {
            std::fprintf(stderr,
                "[nsmbw][diag] createNextScene() call #%llu: dBase_c::createRoot(profile=0x%X) "
                "FAILED (returned null) - retry streak=%llu\n",
                static_cast<unsigned long long>(callCount), nextScene,
                static_cast<unsigned long long>(sameFailureStreak));
            std::fflush(stderr);
        }
        if (changed) {
            // One-time dump of fProfListMg_c::m_data_p (0x8042A698, sda_base-0x52e8, per
            // fBase_make's `lwz r7,-0x52e8(r13)` at 0x80162BC4) and a handful of surrounding
            // profileList[] entries, to tell apart "this one relocation is wrong" from "every
            // REL-resident profile entry is wrong" (main.dol's own BOOT entry at index 0 is a
            // different code path than cross-REL entries like RESTART_CRSIN at index 6, so its
            // success alone doesn't prove the REL-targeting entries are intact).
            constexpr uint32_t kProfListMgDataPAddr = 0x8042A698u;
            const uint32_t arrayBase = Memory::Read32(kProfListMgDataPAddr);
            std::fprintf(stderr,
                "[nsmbw][diag] fProfListMg_c::m_data_p=0x%08X (expect near d_profileNP's "
                "0x807684C0 load address)\n",
                arrayBase);
            if (arrayBase != 0) {
                for (uint32_t idx = 0; idx <= 8; ++idx) {
                    const uint32_t entryAddr = arrayBase + idx * 4u;
                    uint32_t entryVal = 0;
                    bool ok = true;
                    try {
                        entryVal = Memory::Read32(entryAddr);
                    } catch (...) {
                        ok = false;
                    }
                    std::fprintf(stderr,
                        "[nsmbw][diag]   profileList[%u] @0x%08X = 0x%08X%s\n",
                        idx, entryAddr, entryVal, ok ? "" : " (unreadable)");
                }
            }
            std::fflush(stderr);
        }
        lastLoggedNextScene = nextScene;
        return 0;
    }

    std::fprintf(stderr,
        "[nsmbw][diag] createNextScene() call #%llu: dBase_c::createRoot(profile=0x%X) "
        "SUCCEEDED -> new scene=0x%08X\n",
        static_cast<unsigned long long>(callCount), nextScene, newScene);
    std::fflush(stderr);
    // TEMPORARY (GAME_SETUP-never-advances investigation): profile 0xA (GAME_SETUP) is not
    // decompiled in NSMBW-Decomp (only declared, no source), so the only way to find its real
    // execute()/preExecute() addresses is to read the live object's own vtable pointer (a C++
    // object's vtable ptr is its first 4 bytes) once, right after construction, and look up each
    // entry against this project's own function_map.txt (which covers all 4 RELs, not just
    // main.dol). Dumping once here instead of guessing an offset blind.
    if (nextScene == 0xAu) {
        g_nsmbwGameSetupScenePtr = newScene;
        try {
            std::fprintf(stderr, "[nsmbw][diag] GAME_SETUP scene=0x%08X raw bytes:\n", newScene);
            for (uint32_t row = 0; row < 8; ++row) {
                char line[128];
                int off = std::snprintf(line, sizeof(line), "[nsmbw][diag]   +0x%02X:", row * 16u);
                for (uint32_t col = 0; col < 16; col += 4) {
                    const uint32_t word = Memory::Read32(newScene + row * 16u + col);
                    off += std::snprintf(line + off, sizeof(line) - static_cast<size_t>(off), " %08X", word);
                }
                std::fprintf(stderr, "%s\n", line);
            }
            std::fflush(stderr);
        } catch (const Memory::AccessViolation& e) {
            std::fprintf(stderr, "[nsmbw][diag] GAME_SETUP raw dump CAUGHT AccessViolation reason=%.*s\n",
                static_cast<int>(e.reason().size()), e.reason().data());
            std::fflush(stderr);
        }
    }
    lastLoggedNextScene = nextScene;
    sameFailureStreak = 0;
    g_nsmbwCurrentSceneProfile = nextScene;
    g_nsmbwCurrentScenePtr = newScene;

    const uint16_t oldNowScene = Memory::Read16(kNowSceneAddr);
    Memory::Write16(kOldSceneAddr, oldNowScene);
    Memory::Write16(kNowSceneAddr, static_cast<uint16_t>(nextScene));
    Memory::Write16(kNextSceneAddr, static_cast<uint16_t>(kSceneInvalid));
    Memory::Write8(kOtherSceneFlgAddr, 1);

    return newScene;
}

PPC_NATIVE_OVERRIDE(800E1ED0, CreateNextScene_Diag_800E1ED0, uint32_t, (), ());
