// Diagnostic (not a fix): continuing past nsmbw_gamesetup_createpack_diag.cpp, which found
// GAME_SETUP's createPack() hook never fires more than once (if at all) for its own object -
// meaning create() most likely succeeds synchronously during construction (fBase_c::fBase_make
// calls res->runCreate() before even returning the pointer - NSMBW-Decomp source/dol/framework/
// f_base.cpp), moving it straight to LIFECYCLE_e::ACTIVE and onto the execute list, consistent
// with the already-observed checkChildProcessCreateState() "CLEAR" result for this scene (that
// check is only meaningful once a scene is alive enough to ask about its own children).
//
// So the real stall - "createNextScene() never gets called with a new target again" - must be
// inside GAME_SETUP's own per-frame execute(), not create(). Same technique as the createPack
// hook: fBase_c::executePack() (0x80162260, confirmed real address, fully decompiled) is
// `return commonPack(&fBase_c::execute, &fBase_c::preExecute, &fBase_c::postExecute);` - a single
// generic entry point every fBase_c subclass's per-frame execute cycle goes through, and it
// directly returns execute()'s result via a *virtual* member-function-pointer call, so hooking
// this one, well-understood, non-virtual function reveals GAME_SETUP's own execute() return value
// every frame without needing its own (undecompiled) address.
//
// Delegates to the REAL commonPack (0x80161E00 - a *different* address from this override's own
// 0x80162260, so no self-recursion, unlike the createPack hook's first broken attempt), using the
// same PMF triple table executePack() itself loads its constants from (same
// `lis r28,-0x7fcd; addi r28,r28,-0x6968` base = 0x80329698 seen in createPack()'s disassembly,
// confirmed by re-disassembling executePack() itself): preExecute triple @0x803296E0, execute
// triple @0x803296EC, postExecute triple @0x803296F8.
#include "hle_stubs.h"
#include <aurora/env.hpp>
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"

#include <cstdio>
#include <cstdlib>
#include <utility>

extern "C" uint32_t g_nsmbwGameSetupScenePtr;
extern "C" uint32_t g_nsmbwCurrentScenePtr;
extern "C" uint32_t g_nsmbwCurrentSceneProfile;

namespace {
constexpr uint32_t kCommonPackAddr = 0x80161E00u;
constexpr uint32_t kPreExecuteTriple = 0x803296E0u;
constexpr uint32_t kExecuteTriple = 0x803296ECu;
constexpr uint32_t kPostExecuteTriple = 0x803296F8u;
// dScene_c::setNextScene(ProfileName nextScene, unsigned long param, bool forceChange) - real,
// fully-decompiled address (NSMBW-Decomp source/dol/bases/d_scene.cpp), confirmed via
// function_map.txt. Used by the NSMBW_FORCE_GAMESETUP_ADVANCE experiment below.
constexpr uint32_t kSetNextSceneAddr = 0x800E1F50u;
}

extern "C" uint32_t GameSetupExecutePack_Diag_80162260(uint32_t thisPtr)
{
    auto& cpu = GetPersistentCpuContext();
    cpu.gpr[3] = thisPtr;
    cpu.gpr[4] = kPreExecuteTriple;
    cpu.gpr[5] = kExecuteTriple;
    cpu.gpr[6] = kPostExecuteTriple;
    InvokeIndirectCpu(kCommonPackAddr, &cpu);
    const uint32_t result = cpu.gpr[3];

    // GENERIC CHAIN-ADVANCE EXPERIMENT (not a fix): NSMBW_FORCE_GAMESETUP_ADVANCE below only
    // handles GAME_SETUP specifically (filtered by g_nsmbwGameSetupScenePtr) and jumping straight
    // from GAME_SETUP to a distant target (e.g. WORLD_MAP) crashed with a missing-translated-
    // function fault - skipping AUTO_SELECT/SELECT skipped whatever state setup they do first.
    // This instead advances ONE profile at a time from whatever scene is currently active
    // (g_nsmbwCurrentScenePtr/g_nsmbwCurrentSceneProfile, updated on every real
    // createNextScene() transition), using the same "this scene's own execute() gate has been
    // reporting SUCCEEDED for a while" readiness signal as the trigger, so each intermediate
    // scene still gets to run its own setup before we force it onward. NSMBW_CHAIN_ADVANCE=1
    // enables it; each distinct profile is only ever force-advanced once (advancedProfiles guards
    // against re-triggering the same scene every frame once it's already been pushed).
    // Only ever engage from GAME_SETUP (0xA) onward - an earlier version of this experiment had no
    // such floor and force-advanced BOOT(0) itself after its own execute() gate cleared (~60
    // frames in, well before WiiStrap/RESTART_CRSIN/CRSIN/STAGE/GAME_SETUP had run their real
    // setup naturally). That skipped whatever those stages initialize, and the game ran off
    // writing sequentially through gigabytes of unmapped guest address space before finally
    // crashing on an MMIO boundary - a real, hard failure, not a diagnostic artifact. Profiles
    // before GAME_SETUP already progress fine on their own; only GAME_SETUP-and-later are actually
    // stuck and need this.
    if (AURORA_ENV("NSMBW_CHAIN_ADVANCE") != nullptr && thisPtr == g_nsmbwCurrentScenePtr &&
        g_nsmbwCurrentScenePtr != 0 && g_nsmbwCurrentSceneProfile >= 0xAu) {
        static uint32_t lastProfile = 0xFFFFFFFFu;
        static uint32_t consecutiveSuccess = 0;
        static uint32_t advancedProfiles[16] = {0};
        static int advancedCount = 0;
        if (g_nsmbwCurrentSceneProfile != lastProfile) {
            lastProfile = g_nsmbwCurrentSceneProfile;
            consecutiveSuccess = 0;
        }
        if (result == 1) {
            ++consecutiveSuccess;
        } else {
            consecutiveSuccess = 0;
        }
        bool alreadyAdvanced = false;
        for (int i = 0; i < advancedCount; ++i) {
            if (advancedProfiles[i] == g_nsmbwCurrentSceneProfile) { alreadyAdvanced = true; break; }
        }
        if (!alreadyAdvanced && consecutiveSuccess >= 60 && advancedCount < 16) {
            advancedProfiles[advancedCount++] = g_nsmbwCurrentSceneProfile;
            const uint32_t targetProfile = g_nsmbwCurrentSceneProfile + 1u;
            std::fprintf(stderr,
                "[nsmbw][diag] NSMBW_CHAIN_ADVANCE: scene profile=0x%X ready (%u consecutive "
                "execute() SUCCESS) - calling dScene_c::setNextScene(profile=0x%X, param=0, "
                "forceChange=true)\n",
                g_nsmbwCurrentSceneProfile, consecutiveSuccess, targetProfile);
            std::fflush(stderr);
            auto& chainCpu = GetPersistentCpuContext();
            chainCpu.gpr[3] = targetProfile;
            chainCpu.gpr[4] = 0;
            chainCpu.gpr[5] = 1; // forceChange = true
            InvokeIndirectCpu(kSetNextSceneAddr, &chainCpu);
        }
    }

    if (thisPtr == g_nsmbwGameSetupScenePtr && g_nsmbwGameSetupScenePtr != 0) {
        static uint64_t callCount = 0;
        ++callCount;
        if (callCount <= 10 || (callCount % 120) == 0) {
            std::fprintf(stderr,
                "[nsmbw][diag] GAME_SETUP executePack() call #%llu this=0x%08X -> result=%u\n",
                static_cast<unsigned long long>(callCount), thisPtr, result);
            std::fflush(stderr);
        }
        // ONE-TIME: resolve GAME_SETUP's own concrete execute() address by replicating exactly
        // what the __ptmf_scall trampoline (0x802DCEEC) does with the "execute" PMF triple -
        // word0=offset of the relevant vtable pointer within the object, word1=vtable index
        // (>=0 means virtual), word2=this-adjustment - so this override's own resolved-but-
        // never-read vtable slot can finally be read directly, without guessing an offset.
        static bool resolved = false;
        if (!resolved) {
            resolved = true;
            try {
                const uint32_t word0 = Memory::Read32(kExecuteTriple);
                const uint32_t word1 = Memory::Read32(kExecuteTriple + 4);
                const uint32_t word2 = Memory::Read32(kExecuteTriple + 8);
                const uint32_t adjustedThis = thisPtr + word2;
                if (static_cast<int32_t>(word1) >= 0) {
                    const uint32_t vtablePtr = Memory::Read32(adjustedThis + word0);
                    const uint32_t funcAddr = Memory::Read32(vtablePtr + word1);
                    std::fprintf(stderr,
                        "[nsmbw][diag] GAME_SETUP execute() RESOLVED: word0=0x%X word1=0x%X "
                        "word2=0x%X vtablePtr=0x%08X funcAddr=0x%08X\n",
                        word0, word1, word2, vtablePtr, funcAddr);
                    // Dump the WHOLE vtable, not just this one slot - cross-referencing every
                    // entry against function_map.txt (same technique that correctly identified
                    // LASTACTOR as dLastActor_c in nsmbw_lastactor_hunt_diag.cpp) tells us, by
                    // elimination, which slots GAME_SETUP's own (undecompiled) class overrides
                    // versus which fall back to known dBase_c/dScene_c/fBase_c generics - far more
                    // reliable than trusting this file's own PMF-triple-to-slot-name assumptions,
                    // which turned out backwards for LASTACTOR's execute()/preExecute() pair.
                    std::fprintf(stderr, "[nsmbw][diag] GAME_SETUP vtable @0x%08X:\n", vtablePtr);
                    for (uint32_t off = 0; off < 0x100u; off += 4u) {
                        const uint32_t entry = Memory::Read32(vtablePtr + off);
                        std::fprintf(stderr, "[nsmbw][diag]   [+0x%02X] = 0x%08X\n", off, entry);
                    }
                    std::fflush(stderr);

                    // Cross-referencing this vtable against function_map.txt the same way that
                    // correctly identified LASTACTOR as dLastActor_c shows [+0x20]=execute() and
                    // [+0x24]=preExecute() (matching dLastActor_c's own slot layout exactly) - so
                    // 0x809183B0 (dumped above) is actually GAME_SETUP's preExecute(), NOT
                    // execute() as this file assumed earlier. The REAL, still-unexamined execute()
                    // is [+0x20]=0x809183F0, and create() is [+0x08]=0x80918330 - dumping both live
                    // (same REL-relocation-avoidance reasoning as before).
                    constexpr uint32_t kRealExecuteAddr = 0x809183F0u;
                    constexpr uint32_t kRealCreateAddr = 0x80918330u;
                    // create()'s own body calls 0x80919560(this) directly (not virtually) - a
                    // REL-resident (GAME_SETUP-class-specific) function, distinct from the
                    // generic-looking main.dol utility calls around it (0x800b0db0, 0x800e4940,
                    // 0x8016f570, 0x800b2fb0, 0x801be430). Since both create() and execute()
                    // themselves turned out to unconditionally return SUCCEEDED with the only real
                    // gate being preExecute() (already read), this is the next most promising
                    // still-unexamined GAME_SETUP-specific function - possibly where the actual
                    // "load save data, then decide what's next" work happens.
                    constexpr uint32_t kCreateHelperAddr = 0x80919560u;
                    for (auto pair : {std::pair<const char*, uint32_t>{"execute()", kRealExecuteAddr},
                                       std::pair<const char*, uint32_t>{"create()", kRealCreateAddr},
                                       std::pair<const char*, uint32_t>{"create()'s 0x80919560 helper", kCreateHelperAddr}}) {
                        char hex2[3 * 320 + 1] = {0};
                        int hex2off = 0;
                        for (int i = 0; i < 320; ++i) {
                            const uint8_t b = Memory::Read8(pair.second + static_cast<uint32_t>(i));
                            hex2off += std::snprintf(hex2 + hex2off, sizeof(hex2) - static_cast<size_t>(hex2off),
                                                      "%02x", b);
                        }
                        std::fprintf(stderr, "[nsmbw][diag] GAME_SETUP REAL %s bytes @0x%08X: %s\n",
                            pair.first, pair.second, hex2);
                    }
                    std::fflush(stderr);
                    // funcAddr is not in main.dol (a REL-resident function - GAME_SETUP's own
                    // subclass lives in the same REL as g_profile_GAME_SETUP), so it can't be
                    // read from the static original/wiimj2d.dol file the way main.dol functions
                    // were disassembled elsewhere this investigation - a raw REL file's bytes
                    // aren't relocated yet, so a plain offline disassembly would show wrong
                    // targets for any call/load referencing another module. Sidestepping that by
                    // dumping the LIVE, already-relocated bytes straight from guest memory instead
                    // - the REL loader has already patched every cross-module reference by the
                    // time this code is actually running, so these bytes disassemble correctly
                    // with no relocation work needed on our end.
                    char hexdump[3 * 256 + 1] = {0};
                    int hexoff = 0;
                    for (int i = 0; i < 256; ++i) {
                        const uint8_t b = Memory::Read8(funcAddr + static_cast<uint32_t>(i));
                        hexoff += std::snprintf(hexdump + hexoff, sizeof(hexdump) - static_cast<size_t>(hexoff),
                                                 "%02x", b);
                    }
                    std::fprintf(stderr, "[nsmbw][diag] GAME_SETUP execute() bytes @0x%08X: %s\n",
                        funcAddr, hexdump);

                    // execute() (above) turned out to be just a plain 0/1 readiness gate with no
                    // call to dScene_c::setNextScene() anywhere in its body - per fBase_c's
                    // commonPack pattern (f_base.cpp), the code that REACTS to execute()'s
                    // SUCCESS/NOT_READY state and would actually request the next scene lives in
                    // postExecute() instead (a separate virtual override, called unconditionally
                    // every frame with the resulting MAIN_STATE_e). Resolving and dumping that one
                    // too, the same way, using postExecute's own PMF triple @0x803296F8.
                    const uint32_t postWord0 = Memory::Read32(kPostExecuteTriple);
                    const uint32_t postWord1 = Memory::Read32(kPostExecuteTriple + 4);
                    const uint32_t postWord2 = Memory::Read32(kPostExecuteTriple + 8);
                    const uint32_t postAdjustedThis = thisPtr + postWord2;
                    if (static_cast<int32_t>(postWord1) >= 0) {
                        const uint32_t postVtablePtr = Memory::Read32(postAdjustedThis + postWord0);
                        const uint32_t postFuncAddr = Memory::Read32(postVtablePtr + postWord1);
                        std::fprintf(stderr,
                            "[nsmbw][diag] GAME_SETUP postExecute() RESOLVED: word0=0x%X word1=0x%X "
                            "word2=0x%X vtablePtr=0x%08X funcAddr=0x%08X\n",
                            postWord0, postWord1, postWord2, postVtablePtr, postFuncAddr);
                        char postHex[3 * 256 + 1] = {0};
                        int postHexOff = 0;
                        for (int i = 0; i < 256; ++i) {
                            const uint8_t b = Memory::Read8(postFuncAddr + static_cast<uint32_t>(i));
                            postHexOff += std::snprintf(postHex + postHexOff,
                                sizeof(postHex) - static_cast<size_t>(postHexOff), "%02x", b);
                        }
                        std::fprintf(stderr, "[nsmbw][diag] GAME_SETUP postExecute() bytes @0x%08X: %s\n",
                            postFuncAddr, postHex);
                    } else {
                        std::fprintf(stderr,
                            "[nsmbw][diag] GAME_SETUP postExecute() is non-virtual: funcAddr=0x%08X\n", postWord0);
                    }
                    std::fflush(stderr);
                } else {
                    std::fprintf(stderr,
                        "[nsmbw][diag] GAME_SETUP execute() is non-virtual: funcAddr=0x%08X\n", word0);
                }
                std::fflush(stderr);
            } catch (const Memory::AccessViolation& e) {
                std::fprintf(stderr, "[nsmbw][diag] GAME_SETUP execute() resolve CAUGHT AccessViolation reason=%.*s\n",
                    static_cast<int>(e.reason().size()), e.reason().data());
                std::fflush(stderr);
            }
        }

        // EXPERIMENT (not a real fix): both execute() and postExecute() turned out to be either
        // generic dScene_c code or a plain readiness gate with no call to setNextScene() anywhere
        // - the actual "we're done, go to the next scene" decision lives in the LASTACTOR child
        // dScene_c::postCreate() auto-creates for every scene (NSMBW-Decomp source/dol/bases/
        // d_scene.cpp:57), which is itself undecompiled and would need this same
        // resolve-the-vtable-live process repeated for an object we haven't even located yet -
        // open-ended further reverse-engineering with no guaranteed stopping point.
        //
        // Pragmatic alternative: once GAME_SETUP's own execute() gate has been reporting
        // SUCCEEDED for a while (its two internal readiness checks - dScene_c::preExecute() and a
        // dWarningManager_c check - both genuinely cleared, confirmed by the RESOLVED dump above),
        // directly call the real dScene_c::setNextScene() ourselves instead of waiting for
        // whatever undecompiled LASTACTOR logic normally would. This does not reimplement or
        // guess at that logic - it just supplies the one missing side effect natively, using
        // dScene_c's own real function (0x800E1F50) with forceChange=true so it isn't itself
        // blocked by an unrelated soft-reset/safety-wait gate. NSMBW_FORCE_GAMESETUP_ADVANCE picks
        // the target fProf::PROFILE_NAME_e (decimal) - e.g. 1 for AUTO_SELECT, the profile
        // immediately after GAME_SETUP in the enum. This is a guess to be confirmed empirically by
        // what actually renders, not a known-correct value - no decompiled source calls
        // setNextScene(GAME_SETUP's successor, ...), since whatever does that isn't decompiled.
        if (const char* advanceEnv = AURORA_ENV("NSMBW_FORCE_GAMESETUP_ADVANCE")) {
            static uint32_t consecutiveSuccess = 0;
            static bool forced = false;
            if (result == 1) {
                ++consecutiveSuccess;
            } else {
                consecutiveSuccess = 0;
            }
            if (!forced && consecutiveSuccess >= 60) {
                forced = true;
                const uint32_t targetProfile = static_cast<uint32_t>(std::strtoul(advanceEnv, nullptr, 10));
                std::fprintf(stderr,
                    "[nsmbw][diag] NSMBW_FORCE_GAMESETUP_ADVANCE: calling dScene_c::setNextScene("
                    "profile=%u, param=0, forceChange=true) after %u consecutive execute() SUCCESS\n",
                    targetProfile, consecutiveSuccess);
                std::fflush(stderr);
                auto& advCpu = GetPersistentCpuContext();
                advCpu.gpr[3] = targetProfile;
                advCpu.gpr[4] = 0;
                advCpu.gpr[5] = 1; // forceChange = true
                InvokeIndirectCpu(kSetNextSceneAddr, &advCpu);
            }
        }
    }

    return result;
}

PPC_NATIVE_OVERRIDE(80162260, GameSetupExecutePack_Diag_80162260, uint32_t, (uint32_t thisPtr), (thisPtr));
