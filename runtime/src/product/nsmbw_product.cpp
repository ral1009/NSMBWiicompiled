// Minimal NSMBW product entry point.
//
// Deliberately NOT runtime/src/main.cpp: that file is MKW-specific end to end (it hardcodes
// kDefaultEntryAddress to MKW's own dol entry, and does ~1400 lines of MKW asset/window/aurora
// bring-up before ever touching guest code - see docs/MASTER_PLAN.md Phase 5/6). This is the
// Phase 5 milestone instead: initialize the embedded data sections, seed a CpuContext the way
// main.cpp's SeedCpuContext() does, and invoke NSMBW's real dol entry point (0x80004050, read
// directly from wiimj2d.dol's own header - see the chat history for how that was confirmed).
// No window, no aurora, no input: whatever the guest code needs from those will fail loudly
// through RuntimeCrash::FatalMissingGuestTarget (unimplemented HLE calls) rather than silently -
// that failure, once we see it, is Phase 6's actual starting point.
#include "abi_bridge.h"
#include "runtime_config.h"
#include "settings_overlay.h"
#include <cstring>
#include <cstdlib>
#include <vector>
#include <fstream>
#include "fiber_manager.h"
#include "memory.h"
#include "runtime_product.h"
#include "system_bridge.h"
#include "timebase_contract.h"

#include <aurora/aurora.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <ios>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#endif

extern "C" void InitializeDataSections();
extern "C" uint32_t g_dvdFstReservedBase;
extern "C" uint32_t g_dvdFstReservedSize;
extern "C" void DVDInit_8015EA1C();
// See GXManage.cpp for the full explanation: NSMBW's own guest GXInit call is never routed
// through the native GXInit() override (no NSMBW-address binding for it exists), so the shadow
// GX register-ID bytes GXInit() normally seeds internally never get set, which was silently
// misrouting GXSetColorUpdate/GXSetBlendMode/etc. writes to the wrong BP register. Seeded here,
// once, before any guest code can issue a GX call, independent of that binding gap.
extern "C" void GXInitShadowRegisterIds();

namespace {

// Wii OS low-memory conventions (0x8000_0000-0x8000_3200ish) that SDK helpers (OSGetArenaLo/Hi,
// OSGetPhysicalMemSize, __OSBusClock, etc.) read directly instead of computing. Split deliberately
// into what's actually console-wide hardware fact versus what's specific to this one disc, unlike
// system_bridge.cpp's SeedLowMemDefaults() (MKW-only, never called from here) which mixes both:
//   - Bus/CPU clock, MEM1/MEM2 physical sizes, Hollywood revision, IPC/IOS-reserved region sizes:
//     fixed properties of the Wii console itself, identical for every title including this one.
//   - Disc game code, maker code, disc/version bytes: read directly from this project's own
//     retail disc image header (New Super Mario Bros. Wii (Europe) (Rev 1).rvz, offset 0x58 in
//     the RVZ container - "SMNP" + "01" + disc 0 + version 1), not guessed or copied from MKW.
//   - MEM1 arena lo: derived from wiimj2d.dol's own BSS end (its header's bss_addr+bss_size =
//     0x80351980+0xDE59C = 0x8042FF1C, 0x20-aligned up), since the arena conventionally starts
//     right after a game's static data - this is a reasoned derivation from NSMBW's own data, not
//     verified against real hardware, so treat it as a good-faith estimate, not a fact.
//   - Deliberately NOT seeded: IOS version/date stamps (MKW hardcodes IOS36; nothing in this
//     project confirms what IOS NSMBW actually boots under) and a precisely-tuned MEM1 arena hi
//     (reuses the same 0x817F0000 ceiling used for the stack pointer elsewhere in this file,
//     which is conservative but not derived from NSMBW's own arena-init code).
void SeedLowMemDefaults() {
    struct SeedEntry { uint32_t address; uint32_t value; const char* label; };
    const uint32_t mem1Size = static_cast<uint32_t>(Memory::kMem1Size);
    const uint32_t mem2Size = static_cast<uint32_t>(Memory::kMem2Size);
    const uint32_t mem1End = Memory::kMem1CachedBase + mem1Size;
    // _stack_addr, read from __init_registers (0x80004234-0x80004238: lis r1,-0x7fbd;
    // ori r1,r1,0xff20), the same instruction pair that sets r13 = 0x8042F980. The stack
    // sits above .bss (which ends at 0x8042FF1C) and grows down from here, so the arena
    // cannot start until its top. Previously this was the BSS end, which put the whole
    // stack inside the arena; OSInit's DCZeroRange(arenaLo, arenaHi - arenaLo) then wiped
    // the live stack it was running on.
    const uint32_t mem1ArenaLo = 0x8043FF20u;
    const uint32_t mem1ArenaHi = 0x817F0000u; // ARENA_HI, NSMBW-Decomp system_constants.h
    const uint32_t physicalMem2End = Memory::kMem2CachedBase + mem2Size;
    constexpr uint32_t kIosReservedSize = 0x20000u;
    constexpr uint32_t kIPCArenaSize = 0x20000u;
    const uint32_t iosReservedLo = physicalMem2End - kIosReservedSize;
    const uint32_t ipcBufHi = iosReservedLo;
    const uint32_t ipcBufLo = ipcBufHi - kIPCArenaSize;
    // Same reservation SystemBridge::Initialize() makes for MKW. NSMBW does not call that
    // function, so without this g_dvdFstReservedBase stays 0 and BuildAndPublishRuntimeFst()
    // has nowhere to put the FST. Arena hi is lowered to match so guest allocations cannot
    // reach it.
    constexpr uint32_t kDvdFstReserveSize = 0x200000u;
    g_dvdFstReservedSize = kDvdFstReserveSize;
    g_dvdFstReservedBase = ipcBufLo - kDvdFstReserveSize;
    constexpr uint32_t kGameCode = 0x534D4E50u;       // "SMNP", read from the real disc header
    constexpr uint32_t kMakerDiscVersion = 0x30310001u; // "01" maker, disc 0, version 1 (Rev 1)
    // Same retail-vs-NDEV test as OS__GetConsoleType_8019f33c (os_init.cpp) - kept in sync so a
    // guest read of this field and a guest call to OSGetConsoleType() never disagree.
    constexpr uint32_t kRetailMem2Size = 64u * 1024u * 1024u;
    const uint32_t consoleType = mem2Size == kRetailMem2Size ? 0x00000012u : 0x10000012u;

    const SeedEntry entries[] = {
        {0x80000000u, kGameCode, "Disc game code"},
        {0x80000004u, kMakerDiscVersion, "Disc maker/disc/version"},
        {0x8000002Cu, consoleType, "OSBootInfo consoleType"},
        // func_801AA940 (a one-time CPU/BAT bring-up routine, confirmed by reading its body)
        // unconditionally writes 0x80000000 here the first time it runs, caching a pointer to
        // OSBootInfo for func_801AA020's SI/DVD status decode. On real hardware that init runs
        // before any game code (as part of __start/OSInit, which this runtime skips in favor of
        // jumping straight to the DOL entry point). NSMBW's own boot order calls something that
        // reads this cache (via func_801AA020, from the sound-archive load path) before
        // func_801AA940 itself gets a chance to run, so without this the cache is seen as NULL and
        // that read permanently fails - the actual root cause of the OSSleepThread hang traced this
        // session. This isn't a guess: it's the exact value func_801AA940 would itself store here.
        // 0x8042F980 = SDA1_BASE (r13), from generated_nsmbw/generated/RuntimeConfig.h -
        // NSMBWCompiled's own target doesn't have that header on its include path (only
        // nsmbw_runtime_common does), so this is the literal value, not a re-derivation.
        {0x8042F980u - 20528u, 0x80000000u, "cached OSBootInfo* (func_801AA940 pre-seed)"},
        {0x80003180u, kGameCode, "OS app game code"},
        {0x80003194u, kGameCode, "OS app gamename"},
        {0x800000F8u, static_cast<uint32_t>(TimeBaseContract::kBusClockHz), "__OSBusClock"},
        {0x800000FCu, static_cast<uint32_t>(TimeBaseContract::kBusClockHz * 3), "__OSClock"},
        {0x80000028u, mem1Size, "Legacy MEM1 size"},
        {0x80000030u, mem1ArenaLo, "Legacy MEM1 arena lo"},
        {0x80000034u, mem1ArenaHi, "Legacy MEM1 arena hi"},
        {0x800000F0u, mem1Size, "Legacy simulated MEM1 size"},
        {0x80003100u, mem1Size, "Physical MEM1 size"},
        {0x80003104u, mem1Size, "Simulated MEM1 size"},
        {0x80003108u, mem1End, "MEM1 end"},
        {0x8000310Cu, mem1ArenaLo, "MEM1 arena lo"},
        {0x80003110u, mem1ArenaHi, "MEM1 arena hi"},
        {0x80003118u, mem2Size, "Physical MEM2 size"},
        {0x8000311Cu, mem2Size, "Simulated MEM2 size"},
        {0x80003120u, iosReservedLo, "MEM2 end"},
        {0x80003124u, Memory::kMem2CachedBase, "MEM2 arena lo"},
        {0x80003128u, g_dvdFstReservedBase, "MEM2 arena hi"},
        {0x80003130u, ipcBufLo, "IPC Buffer lo"},
        {0x80003134u, ipcBufHi, "IPC Buffer hi"},
        {0x80003138u, 0x00000002u, "Hollywood revision"},
        {0x80003148u, iosReservedLo, "IOS reserved lo"},
        {0x8000314Cu, physicalMem2End, "IOS reserved hi"},
    };
    int seeded = 0;
    for (const auto& entry : entries) {
        if (!Memory::Contains(entry.address, sizeof(uint32_t))) continue;
        if (Memory::Read32(entry.address) != 0) continue; // don't clobber real embedded data
        Memory::Write32(entry.address, entry.value);
        seeded++;
    }
    std::printf("[nsmbw] Seeded %d low-memory OS default(s).\n", seeded);
}

constexpr uint32_t kNsmbwEntryAddress = 0x80004050u;

// Mirrors main.cpp's SeedCpuContext(): r1 is the initial stack pointer. NSMBW's manifest
// (projects/nsmbw/nsmbw.yml) declares memory.base=0x80000000, memory.size=0x01800000, so guest
// RAM runs up to 0x81800000; 0x817F0000 leaves headroom below that top for whatever the game's
// own boot code further reserves, the same way MKW's own 0x81700000 sits below its RAM ceiling.
void SeedCpuContext(CpuContext& cpu) {
    cpu.gpr[1] = 0x817F0000u;
}

// One real Wii address that must run before any other guest code touches its module: main.dol's
// own .ctors table (confirmed against the runtime's own load log: "_ctors (736 bytes) @
// 0x802EDCE0", immediately followed by "_dtors ... @ 0x802EDFC0"), and each REL's own load
// address, which is not just a translation seed - it's the exact address of that REL's real,
// decompiled _prolog() function (source/runtime/rel_init.cpp: `void _prolog() {
// ModuleConstructorsX(_ctors); finalizeProlog(); }`). On a real Wii, main.dol's ctors run before
// main() and OSLink() calls _prolog() for each module right after relocating it; we don't
// implement real OSLink (every REL's relocated image is embedded statically instead), so this is
// what stands in for both steps.
constexpr uint32_t kDolCtorStart = 0x802EDCE0u;
constexpr uint32_t kDolCtorEnd = 0x802EDFC0u;
// d_en_bossNP was excluded for two stacked reasons this session, both now fixed:
//   1. RelFile.cs's ApplyRelocations mis-resolved cross-module relocations (reusing the
//      currently-relocating module's own baseAddress/Sections instead of the target module's),
//      leaving 13 of d_en_bossNP's 27 .ctors entries holding bogus values like 0x00040104. Fixed
//      via RelModuleInfo/moduleRegistry in RelFile.cs.
//   2. RelFile.cs computed each REL's resident image size from the raw on-disc .rel file length,
//      which also includes the relocation/import tables that only exist to drive linking. NSMBW-
//      Decomp's real loader (DynamicModuleControl::do_link, source/dol/cLib/c_dylink.cpp) reads
//      the whole file, links it, then shrinks the heap block to fixSize+bssSize - discarding that
//      tail. Using the full file length as "resident size" inflated every REL's footprint enough
//      that d_en_bossNP's range [0x80B1CA10, 0x80BB502C) landed entirely inside d_enemiesNP's
//      [0x809A2D90, 0x80BB7FF0). Fixed in RelFile.cs (ComputeMaxSectionExtent): BSS - and so the
//      total resident size - is now placed right after the last real section, matching
//      do_link()'s actual behavior. With that fix, all four RELs' real footprints are
//      non-overlapping and pack contiguously (small gaps of a few hundred bytes between them,
//      consistent with real heap allocator overhead) - confirmed by recomputing directly against
//      the retail .rel files before touching the code, not just by rebuilding and hoping.
// NSMBW-Decomp's docs/INTRODUCTION.md and source/dol/bases/d_s_boot.cpp confirm all four RELs are
// genuinely, permanently resident together for the whole session (loaded once at the health-and-
// safety boot screen, never unlinked) - this isn't a stage-conditional alternation, so keeping all
// four here is the architecturally correct model, not a simplification.
// Each REL's _prolog(), which is loadAddress + sectionTable[prologSection].offset +
// prologOffset - NOT the load address itself, which is the start of the REL header. Read out of
// the retail .rel headers: prologSection is 1 and prologOffset is 0 for all four, and section 1
// starts at file offset 0xF0 (0xE0 for d_profileNP), just past each header and section table.
// Cross-checked against the translated bodies: func_80768680 is d_profileNP's real _prolog (it
// loads its own _ctors at 0x8076A678 and returns to 0x80768698); func_807685A0 is header bytes,
// which the translator still decoded into a function, so the old addresses called something that
// looked callable and no REL constructor ever ran.
constexpr uint32_t kRelPrologAddresses[] = {
    0x8076D770u, // d_basesNP    _prolog (file base 0x8076D680 + section 1 at 0xF0, prolog offset 0)
    0x80B1CA10u, // d_en_bossNP  _prolog (file base 0x80B1C920 + section 1 at 0xF0, prolog offset 0)
    0x809A2D90u, // d_enemiesNP  _prolog (file base 0x809A2CA0 + section 1 at 0xF0, prolog offset 0)
    0x807685A0u, // d_profileNP  _prolog (file base 0x807684C0 + section 1 at 0xE0, prolog offset 0)
};

// A bad guest constructor must not take down the whole boot sequence - matches
// system_bridge.cpp's own SystemBridge::Initialize(), which runs main.dol's and StaticR.rel's
// ctors the same guarded way for the same reason (also not using real OSLink).
void InvokeGuestCallGuarded(uint32_t address, CpuContext& cpu, const char* label) {
    MkwJmpBuf jumpBuf;
    g_sehJumpTarget = &jumpBuf;
    if (MKW_SETJMP(jumpBuf) == 0) {
        cpu.gpr[1] = 0x817F0000u;
        InvokeIndirectCpu(address, &cpu);
    } else {
        std::fprintf(stderr,
            "[nsmbw] SEH exception invoking %s 0x%08X code=0x%X at=0x%zX - skipping.\n",
            label, address, g_sehLastExceptionCode,
            static_cast<size_t>(g_sehLastExceptionAddress));
    }
    g_sehJumpTarget = nullptr;
}

void RunGuestConstructors(CpuContext& cpu) {
    g_suppressSehReporting = true;

    std::printf("[nsmbw] Running main.dol static constructors...\n");
    int dolCount = 0;
    for (uint32_t addr = kDolCtorStart; addr < kDolCtorEnd; addr += 4) {
        uint32_t funcAddr = 0;
        try {
            funcAddr = Memory::Read32(addr);
        } catch (...) {
            continue;
        }
        if (funcAddr == 0 || funcAddr == 0xFFFFFFFFu) continue;
        if (!TranslatedFunctionRegistry::FindByAddressPtr(funcAddr)) continue;
        InvokeGuestCallGuarded(funcAddr, cpu, "main.dol ctor");
        dolCount++;
    }
    std::printf("[nsmbw] Executed %d main.dol static constructor(s).\n", dolCount);

    std::printf("[nsmbw] Running REL _prolog (static constructors) for each of the 4 RELs...\n");
    for (uint32_t prolog : kRelPrologAddresses) {
        InvokeGuestCallGuarded(prolog, cpu, "REL _prolog");
    }

    g_suppressSehReporting = false;
}

} // namespace

// Runs main.dol's .ctors and every REL's _prolog exactly once, from guest context. Called by the
// 0x80004040 override (projects/nsmbw/native/nsmbw_guest_ctor_hook.cpp) so that it happens after
// __start's BSS clear rather than before it.
extern "C" void Nsmbw_RunGuestConstructorsOnce(CpuContext* cpu)
{
    static bool alreadyRan = false;
    if (alreadyRan || cpu == nullptr) {
        return;
    }
    alreadyRan = true;

    const uint32_t savedStack = cpu->gpr[1];
    RunGuestConstructors(*cpu);
    cpu->gpr[1] = savedStack; // constructors repoint r1 per-call; __start still needs its own
}

namespace {

// Minimal window bring-up (Phase 6 step 1: "confirm a window opens"). Deliberately not
// main.cpp's aurora_initialize() call: that one reads Config.toml (RuntimeConfigFile::*),
// applies MKW's widescreen/dynamic-aspect logic (ConfigureMkwDynamicAspect,
// VISetFrameBufferScale), and wires up texture-replacement/joystick settings NSMBW has no
// equivalent for yet. This is deliberately just enough to open a plain 640x480 window and clear
// it a few times so it's visible on screen before guest code runs (and likely crashes) - no GX
// pipeline, no guest-driven rendering, no input. Real GX draw calls reaching aurora is the next
// increment after this one, not this one.
void RunAuroraLogCallback(AuroraLogLevel level, const char* module, const char* message, unsigned int len) {
    const std::string_view moduleView = module ? std::string_view(module) : std::string_view{};
    const std::string_view messageView = message ? std::string_view(message, len) : std::string_view{};
    std::fprintf(stderr, "[aurora] [%d] [%.*s] %.*s\n", static_cast<int>(level),
                 static_cast<int>(moduleView.size()), moduleView.data(),
                 static_cast<int>(messageView.size()), messageView.data());
}

bool InitializeAuroraWindow(AuroraInfo& outInfo) {
    std::error_code ec;
    const auto dataDir = std::filesystem::current_path() / "nsmbw_data";
    const auto cacheDir = dataDir / "Cache";
    std::filesystem::create_directories(cacheDir, ec);
    static const std::string userPath = dataDir.string();
    static const std::string cachePath = cacheDir.string();

    AuroraConfig config{};
    config.appName = RuntimeProduct::Active().displayName.data();
    config.userPath = userPath.c_str();
    config.cachePath = cachePath.c_str();
    config.logCallback = &RunAuroraLogCallback;
    config.logLevel = LOG_INFO;
    config.windowWidth = 640;
    config.windowHeight = 480;
    config.desiredBackend = BACKEND_AUTO;
    config.allowJoystickBackgroundEvents = true;
    // We already own guest memory via Memory::Init() above; don't have aurora allocate its own.
    config.mem1Size = 0;
    config.mem2Size = 0;

    outInfo = aurora_initialize(0, nullptr, &config);
    return outInfo.window != nullptr;
}

// TEMPORARY diagnostic for the post-EXI/HW boot hang investigation (see abi_bridge.h's
// DiagRecentCalls comment for why RecompMod::CurrentTranslatedExecutionAddress() alone can't find
// it). Dumps the ring buffer of recently-dispatched guest addresses, newest first, resolved
// against the project's function-map symbol table (same table + floor-search system_bridge.cpp's
// crash reporter uses) so the actual nested call site is visible instead of just the outermost
// entry point. Remove once the hang's cause is confirmed.
extern "C" {
extern const uint32_t kGuestMapSymbolCount;
extern const uint32_t kGuestMapSymbolAddresses[];
extern const char* const kGuestMapSymbolNames[];
}

const char* DiagResolveSymbol(uint32_t address, uint32_t* symbolStart) {
    if (kGuestMapSymbolCount == 0) return nullptr;
    uint32_t lo = 0, hi = kGuestMapSymbolCount;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (kGuestMapSymbolAddresses[mid] <= address) lo = mid + 1; else hi = mid;
    }
    if (lo == 0) return nullptr;
    const uint32_t index = lo - 1;
    if (address - kGuestMapSymbolAddresses[index] >= 0x10000u) return nullptr;
    if (symbolStart) *symbolStart = kGuestMapSymbolAddresses[index];
    return kGuestMapSymbolNames[index];
}

// Formats "0xADDR" or "0xADDR<symbol[+0xOFFSET]>" into a fixed buffer - shared by both the callee
// and the caller (LR) columns in DiagDumpRecentCalls below.
void DiagFormatAddress(uint32_t addr, char* out, size_t outSize) {
    uint32_t symStart = 0;
    const char* sym = DiagResolveSymbol(addr, &symStart);
    if (sym && symStart == addr) {
        std::snprintf(out, outSize, "0x%08X<%s>", addr, sym);
    } else if (sym) {
        std::snprintf(out, outSize, "0x%08X<%s+0x%X>", addr, sym, addr - symStart);
    } else {
        std::snprintf(out, outSize, "0x%08X", addr);
    }
}

void DiagDumpRecentCalls(int afterSeconds) {
    if (afterSeconds < 0) {
        std::fprintf(stderr,
                     "[nsmbw] diag: crashed - last dispatched guest calls (newest first, called-from "
                     "is ctx->lr at dispatch time):\n");
    } else {
        std::fprintf(stderr,
                     "[nsmbw] diag: still running after %ds, last dispatched guest calls (newest first, "
                     "called-from is ctx->lr at dispatch time):\n",
                     afterSeconds);
    }
    const uint32_t next = DiagRecentCalls::g_next.load(std::memory_order_relaxed);
    const uint32_t count = std::min<uint32_t>(next, static_cast<uint32_t>(DiagRecentCalls::kCapacity));
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t slot = (next - 1 - i) % static_cast<uint32_t>(DiagRecentCalls::kCapacity);
        char calleeStr[96];
        char callerStr[96];
        DiagFormatAddress(DiagRecentCalls::g_addrs[slot], calleeStr, sizeof(calleeStr));
        DiagFormatAddress(DiagRecentCalls::g_lrs[slot], callerStr, sizeof(callerStr));
        std::fprintf(stderr, "  [%u] %s called-from %s\n", i, calleeStr, callerStr);
    }
    std::fflush(stderr);
}

#if defined(_WIN32)
// TEMPORARY diagnostic (same technique used to find func_801B4FE0/__OSReschedule earlier this
// session): suspend the main thread just long enough to read its native RIP/RSP, then resume it.
// DiagRecentCalls only sees InvokeIndirectCpu/registry dispatch - a hang built out of
// MKW_STATIC_TRANSLATED_CALL (a plain Entry(ctx) call, no bookkeeping) is invisible to it.
void DiagSampleMainThreadNativePc(HANDLE mainThread, int afterSeconds) {
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_CONTROL;
    if (::SuspendThread(mainThread) == static_cast<DWORD>(-1)) {
        std::fprintf(stderr, "[nsmbw] diag: still running after %ds - SuspendThread failed (%lu)\n",
                     afterSeconds, ::GetLastError());
        return;
    }
    const BOOL gotContext = ::GetThreadContext(mainThread, &ctx);
    ::ResumeThread(mainThread);
    if (!gotContext) {
        std::fprintf(stderr, "[nsmbw] diag: still running after %ds - GetThreadContext failed (%lu)\n",
                     afterSeconds, ::GetLastError());
        return;
    }
#if defined(_M_X64) || defined(__x86_64__)
    const uint64_t moduleBase = reinterpret_cast<uint64_t>(::GetModuleHandleW(nullptr));
    std::fprintf(stderr,
                 "[nsmbw] diag: still running after %ds - native RIP=0x%016llX RSP=0x%016llX "
                 "moduleBase=0x%016llX rvaFromBase=0x%llX\n",
                 afterSeconds, static_cast<unsigned long long>(ctx.Rip),
                 static_cast<unsigned long long>(ctx.Rsp), static_cast<unsigned long long>(moduleBase),
                 static_cast<unsigned long long>(ctx.Rip - moduleBase));
#endif
    std::fflush(stderr);
}
#endif

void StartDiagWatchdog(std::atomic<bool>& finished) {
#if defined(_WIN32)
    HANDLE mainThreadHandle = nullptr;
    ::DuplicateHandle(::GetCurrentProcess(), ::GetCurrentThread(), ::GetCurrentProcess(),
                       &mainThreadHandle, 0, FALSE, DUPLICATE_SAME_ACCESS);
#endif
    std::thread([&finished
#if defined(_WIN32)
                 , mainThreadHandle
#endif
    ] {
        const auto start = std::chrono::steady_clock::now();
        for (int deadlineSeconds : {6, 15, 30}) {
            const auto deadline = start + std::chrono::seconds(deadlineSeconds);
            while (std::chrono::steady_clock::now() < deadline) {
                if (finished.load(std::memory_order_relaxed)) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            if (finished.load(std::memory_order_relaxed)) return;
            DiagDumpRecentCalls(deadlineSeconds);
#if defined(_WIN32)
            if (mainThreadHandle) {
                DiagSampleMainThreadNativePc(mainThreadHandle, deadlineSeconds);
            }
#endif
        }
    }).detach();
}

#if defined(_WIN32)
// TEMPORARY diagnostic for investigating a deterministic native SIGSEGV reached after clearing
// the func_801AF710/func_801AF900 boot blockers - it's not a guest Memory::AccessViolation (those
// are already caught below), so it must be a raw native fault; this reports exactly where before
// the OS terminates the process, instead of a bare "Segmentation fault" with no address. Remove
// once that investigation is resolved.
LONG WINAPI DiagVectoredExceptionHandler(EXCEPTION_POINTERS* info) {
    if (info && info->ExceptionRecord &&
        info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        const auto* record = info->ExceptionRecord;
        const uint64_t faultAddr = record->NumberParameters >= 2
                                        ? static_cast<uint64_t>(record->ExceptionInformation[1])
                                        : 0;
        const uint64_t accessType = record->NumberParameters >= 1
                                         ? static_cast<uint64_t>(record->ExceptionInformation[0])
                                         : 0;
        const uint64_t rip = reinterpret_cast<uint64_t>(record->ExceptionAddress);
        const uint64_t moduleBase = reinterpret_cast<uint64_t>(::GetModuleHandleW(nullptr));
        std::fprintf(stderr,
                     "[nsmbw] diag: NATIVE ACCESS VIOLATION - accessType=%llu(0=read,1=write) "
                     "faultAddr=0x%016llX nativeRIP=0x%016llX rvaFromBase=0x%llX\n",
                     static_cast<unsigned long long>(accessType),
                     static_cast<unsigned long long>(faultAddr),
                     static_cast<unsigned long long>(rip),
                     static_cast<unsigned long long>(rip - moduleBase));
        std::fflush(stderr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

} // namespace

namespace {

// Fresh-state policy for every NSMBW launch (requested 2026-09-19, after a crashed run left the
// app in a state where it would not start again):
//
//  * The game's NAND save (title 00010004/534d4e50 = SMNP, the PAL disc) is deleted, so each run
//    boots through the "save data created" dialog into the same clean state.
//  * Config.toml's video/window keys are put back to windowed 640x480 at 1x. The crashed run had
//    ended maximized/borderless at an 8x render scale, and those values are applied at startup.
//  * The shader/pipeline caches (nsmbw_data/Cache/*.db, SQLite with write-ahead logs) are wiped
//    ONLY if the previous run did not exit cleanly. That cache is what actually broke startup:
//    the abort left its WAL half-written and every later launch aborted inside pipeline prewarm
//    while loading it (Windows event log: 0xC0000409 in ucrtbase, right after "Using framebuffer
//    size"; a fresh Cache/ booted immediately). Wiping it unconditionally would cost a full shader
//    recompile and skipped draws on every launch, so a marker file records whether the last run
//    reached a clean exit instead.
//
// This runs before aurora_initialize (which reads Config.toml) and before any guest code (which
// reads the NAND). Set NSMBW_KEEP_STATE=1 to skip all three for a run that should carry state over.
constexpr const char* kCrashMarkerName = ".last_run_unclean";

void ResetPersistentStateForCleanRun(const std::filesystem::path& cacheDir) {
    if (std::getenv("NSMBW_KEEP_STATE") != nullptr) {
        std::printf("[nsmbw] NSMBW_KEEP_STATE set: keeping save, config and pipeline cache from the previous run.\n");
        return;
    }
    std::error_code ec;

    const auto saveDir = RuntimeConfigFile::ApplicationDataDirectory() / "NAND" / "title" / "00010004" / "534d4e50";
    if (std::filesystem::exists(saveDir, ec)) {
        const auto removed = std::filesystem::remove_all(saveDir, ec);
        std::printf("[nsmbw] Reset: removed NSMBW NAND save (%llu entries) at %s\n",
                    static_cast<unsigned long long>(removed), saveDir.string().c_str());
    }

    const bool wroteVideo = RuntimeConfigFile::SetResolutionMultiplier(1.0f) &&
                            RuntimeConfigFile::SetDisplayMode("windowed") &&
                            RuntimeConfigFile::SetWindowSize(640u, 480u);
    std::printf("[nsmbw] Reset: Config.toml video/window keys -> windowed 640x480 @1x (%s)\n",
                wroteVideo ? "ok" : "FAILED");

    const auto marker = cacheDir / kCrashMarkerName;
    if (std::filesystem::exists(marker, ec)) {
        std::size_t removed = 0;
        for (const auto& entry : std::filesystem::directory_iterator(cacheDir, ec)) {
            const auto name = entry.path().filename().string();
            if (name.rfind("pipeline_cache.db", 0) == 0 || name.rfind("dawn_cache.db", 0) == 0) {
                if (std::filesystem::remove(entry.path(), ec)) ++removed;
            }
        }
        std::printf("[nsmbw] Reset: previous run did not exit cleanly - wiped %zu pipeline/shader cache file(s)\n", removed);
    }
    // Armed for this run; cleared by the atexit hook below on a clean exit.
    std::ofstream(marker.string()) << "1\n";
}

} // namespace

int main() {
    // Unbuffered so log order is trustworthy across an abnormal termination (abort() from an
    // uncaught exception does not flush buffered stdio) - needed to tell whether a crash happens
    // before or after other printf-based milestones instead of guessing from apparent line order.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    {
        std::error_code ec;
        const auto cacheDir = std::filesystem::current_path() / "nsmbw_data" / "Cache";
        std::filesystem::create_directories(cacheDir, ec);
        ResetPersistentStateForCleanRun(cacheDir);
        static const std::string markerPath = (cacheDir / kCrashMarkerName).string();
        std::atexit(+[] {
            std::error_code ec2;
            std::filesystem::remove(markerPath, ec2);
        });
    }
#if defined(_WIN32)
    ::AddVectoredExceptionHandler(1, DiagVectoredExceptionHandler);
#endif
    // Confirmed the hard way: without this, InitializeDataSections()'s first memcpy into guest
    // memory is a raw access violation, since nothing has mapped that address space yet.
    // SystemBridge::Initialize() (system_bridge.cpp) does the same allocation, but bundled
    // together with a static-constructor loop hardcoded to MKW's own .ctors range - not reused
    // here for that reason (see RunGuestConstructors below for NSMBW's own version instead).
    Memory::Init(Memory::Config::WiiDefaults());

    std::printf("[nsmbw] Initializing embedded data sections...\n");
    InitializeDataSections();
    SeedLowMemDefaults();
    // Must precede the guest entry point. NSMBW's own DVDInit (0x801CAE70) inlines
    // __DVDFSInit, which latches OSBootInfo.FSTLocation once and early-returns when it reads
    // zero - so the FST has to be published before the guest starts, or every disc path lookup
    // fails for the rest of the run. This is MKW's DVDInit HLE, reused for its index build and
    // BuildAndPublishRuntimeFst(); its trailing __DVDFSInit call targets MKW's address and is
    // already guarded by a registry lookup, so it simply no-ops here.
    DVDInit_8015EA1C();

    // NSMBW's own boot sequence draws its Wii Remote strap warning as its first real content -
    // MKW's black "WiiCompiled" title card (meant to cover the brief gap before guest rendering
    // starts) was instead covering that warning screen and then disappearing over it partway
    // through. See settings_overlay::DisableStartupScreen's own comment for why this is an
    // opt-out rather than a change to the card's default behavior.
    settings_overlay::DisableStartupScreen();

    std::printf("[nsmbw] Opening window...\n");
    AuroraInfo auroraInfo{};
    const bool windowOpened = InitializeAuroraWindow(auroraInfo);
    if (windowOpened) {
        // FatalMissingGuestTarget (nsmbw_runtime_shims.cpp) calls std::exit() directly on the
        // crash path this milestone actually hits - atexit() is what makes that path (not just
        // this function's own normal/catch returns below) still shut aurora down cleanly.
        std::atexit(+[] { aurora_shutdown(); });
        std::printf("[nsmbw] Window open (backend=%d, %ux%u). Clearing a few frames...\n",
                    static_cast<int>(auroraInfo.backend), auroraInfo.windowSize.width,
                    auroraInfo.windowSize.height);
        // Just enough frames to be visible on screen for a moment before guest code runs (and,
        // right now, crashes) - no real GX rendering yet, so there's nothing to draw beyond aurora's
        // own clear color.
        for (int frame = 0; frame < 120; ++frame) {
            aurora_update();
            if (aurora_begin_frame()) {
                aurora_end_frame();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    } else {
        std::fprintf(stderr, "[nsmbw] aurora_initialize did not produce a window; continuing without one.\n");
    }

    // Must precede any guest GX call - see the declaration above and GXManage.cpp for why.
    GXInitShadowRegisterIds();

    // DIAGNOSTIC (temporary): NSMBW_AUTO_SKIP_STRAP - dInfo_c::mGameFlag (0x8042A260, confirmed via
    // projects/nsmbw/function_map.txt) has a real, game-supported GAME_FLAG_AUTO_SKIP bit (bit 19,
    // 0x80000; see NSMBW-Decomp's d_info.hpp: "Whether to automatically skip the Wii strap and
    // controller information screens"). dScBoot_c::executeState_WiiStrapDispEndWait's own
    // mAutoAdvanceTimer (should expire after 1200 frames / 20s) has not fired after 120+ real
    // seconds of observed runtime - this sets the game's own documented skip flag to test whether
    // boot progresses past WiiStrap/ControllerInformation at all, to determine what's actually
    // reachable, independent of (and without yet explaining) why the auto-advance timer itself
    // isn't firing. Not a permanent fix. Remove once the real timer/state-machine issue is found.
    if (std::getenv("NSMBW_AUTO_SKIP_STRAP") != nullptr) {
        constexpr uint32_t kGameFlagAddr = 0x8042A260u;
        constexpr uint32_t kGameFlagAutoSkip = 1u << 19;
        try {
            const uint32_t existing = Memory::Read32(kGameFlagAddr);
            Memory::Write32(kGameFlagAddr, existing | kGameFlagAutoSkip);
            std::printf("[nsmbw] NSMBW_AUTO_SKIP_STRAP: set dInfo_c::mGameFlag |= GAME_FLAG_AUTO_SKIP (0x%08X -> 0x%08X)\n",
                        existing, existing | kGameFlagAutoSkip);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[nsmbw] NSMBW_AUTO_SKIP_STRAP: failed to set mGameFlag: %s\n", e.what());
        }
    }

    InitializePersistentCpuContext();
    CpuContext& cpu = GetPersistentCpuContext();
    SeedCpuContext(cpu);

    // Mirrors main.cpp's own init order (Initialize() right after SeedCpuContext, before
    // CpuContextScope) - confirmed missing here: NSMBW's real main() spawns its game-loop thread
    // via OSCreateThread/OSResumeThread (see the shard-level fix in func_800CA080/func_801B5270),
    // which needs a live GuestFiberManager to create the host fiber that thread actually runs on.
    // Without this, every Fiber::GuestFiberManager::IsInitialized() guard in the OS thread HLE
    // silently no-ops, so the new thread's fiber was never created at all - a separate, compounding
    // gap on top of the OSCreateThread/OSResumeThread guest-address mismatch (this product's shared
    // os_thread.cpp hooks are registered at MKW's link addresses, not NSMBW's).
    Fiber::GuestFiberManager::Initialize();

    CpuContextScope cpuScope(&cpu);

    // Deliberately NOT run here any more. main.dol's .ctors and the four REL _prolog()s all
    // write guest BSS, and NSMBW's own __start (0x80004050) calls __init_data (0x80004250) which
    // memsets BSS clean at 0x80004600 - so anything initialized before the entry point was wiped
    // before main ever saw it. Proven with a watchpoint on fProfListMg_c::m_data_p (0x8042A698):
    // d_profileNP's _prolog set it to 0x8076A828, then func_80004600 zeroed it, and fBase_make
    // later virtual-called through the null list. They now run from the 0x80004040 hook in
    // projects/nsmbw/native/nsmbw_guest_ctor_hook.cpp, which sits after __init_data and
    // immediately before __start calls main (0x800CA080).

    std::printf("[nsmbw] Invoking entry point 0x%08X...\n", kNsmbwEntryAddress);
    // FatalMissingGuestTarget (an unimplemented HLE call) already exits cleanly on its own - this
    // catches the other two ways guest execution can end early: an out-of-bounds/MMIO guest
    // memory access, and any other host-side exception, matching main.cpp's own two catch blocks
    // for the same reasons (so a Phase 6 boot crash is diagnosable output, not a raw libc++abi
    // "terminating due to uncaught exception" abort).
    std::atomic<bool> diagFinished{false};
    StartDiagWatchdog(diagFinished);
    try {
        InvokeIndirectCpu(kNsmbwEntryAddress, &cpu);
        diagFinished.store(true, std::memory_order_relaxed);
    } catch (const Memory::AccessViolation& ex) {
        diagFinished.store(true, std::memory_order_relaxed);
        DiagDumpRecentCalls(-1); // TEMPORARY - see DiagRecentCalls's comment in abi_bridge.h. -1
                                 // marks this as a crash-triggered dump, not a watchdog timeout one.
        std::ostringstream details;
        details << "addr=0x" << std::hex << std::uppercase << ex.address()
                << " len=0x" << ex.length() << std::dec << std::nouppercase
                << " reason=" << ex.reason();
        RuntimeCrash::WriteCrashArtifacts("access_violation", details.str(), nullptr);
        ShowRuntimeFatalPopup("a guest memory access was out of bounds", details.str());
        std::fprintf(stderr, "[nsmbw] guest memory access violation: %s\n", details.str().c_str());
        return 1;
    } catch (const std::exception& ex) {
        diagFinished.store(true, std::memory_order_relaxed);
        RuntimeCrash::WriteCrashArtifacts("exception", ex.what(), nullptr);
        ShowRuntimeFatalPopup("a runtime exception occurred", ex.what());
        std::fprintf(stderr, "[nsmbw] runtime exception: %s\n", ex.what());
        return 1;
    }

    std::printf("[nsmbw] Entry point returned; r3=0x%08X\n", cpu.gpr[3]);
    return 0;
}
