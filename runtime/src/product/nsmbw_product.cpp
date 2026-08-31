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
#include "memory.h"
#include "runtime_product.h"
#include "system_bridge.h"
#include "timebase_contract.h"

#include <aurora/aurora.h>

#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <ios>
#include <sstream>
#include <thread>

extern "C" void InitializeDataSections();

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
    const uint32_t mem1ArenaLo = 0x8042FF20u; // wiimj2d.dol BSS end (0x8042FF1C), aligned to 0x20
    const uint32_t mem1ArenaHi = 0x817F0000u; // same ceiling used for the stack pointer elsewhere
    const uint32_t physicalMem2End = Memory::kMem2CachedBase + mem2Size;
    constexpr uint32_t kIosReservedSize = 0x20000u;
    constexpr uint32_t kIPCArenaSize = 0x20000u;
    const uint32_t iosReservedLo = physicalMem2End - kIosReservedSize;
    const uint32_t ipcBufHi = iosReservedLo;
    const uint32_t ipcBufLo = ipcBufHi - kIPCArenaSize;
    constexpr uint32_t kGameCode = 0x534D4E50u;       // "SMNP", read from the real disc header
    constexpr uint32_t kMakerDiscVersion = 0x30310001u; // "01" maker, disc 0, version 1 (Rev 1)

    const SeedEntry entries[] = {
        {0x80000000u, kGameCode, "Disc game code"},
        {0x80000004u, kMakerDiscVersion, "Disc maker/disc/version"},
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
        {0x80003128u, iosReservedLo, "MEM2 arena hi"},
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
constexpr uint32_t kRelPrologAddresses[] = {
    0x8076D770u, // d_basesNP
    0x80B1CA10u, // d_en_bossNP
    0x809A2D90u, // d_enemiesNP
    0x807685A0u, // d_profileNP
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

} // namespace

int main() {
    // Confirmed the hard way: without this, InitializeDataSections()'s first memcpy into guest
    // memory is a raw access violation, since nothing has mapped that address space yet.
    // SystemBridge::Initialize() (system_bridge.cpp) does the same allocation, but bundled
    // together with a static-constructor loop hardcoded to MKW's own .ctors range - not reused
    // here for that reason (see RunGuestConstructors below for NSMBW's own version instead).
    Memory::Init(Memory::Config::WiiDefaults());

    std::printf("[nsmbw] Initializing embedded data sections...\n");
    InitializeDataSections();
    SeedLowMemDefaults();

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

    InitializePersistentCpuContext();
    CpuContext& cpu = GetPersistentCpuContext();
    SeedCpuContext(cpu);
    CpuContextScope cpuScope(&cpu);

    RunGuestConstructors(cpu);
    SeedCpuContext(cpu); // restore r1 - constructors run with it repointed per-call

    std::printf("[nsmbw] Invoking entry point 0x%08X...\n", kNsmbwEntryAddress);
    // FatalMissingGuestTarget (an unimplemented HLE call) already exits cleanly on its own - this
    // catches the other two ways guest execution can end early: an out-of-bounds/MMIO guest
    // memory access, and any other host-side exception, matching main.cpp's own two catch blocks
    // for the same reasons (so a Phase 6 boot crash is diagnosable output, not a raw libc++abi
    // "terminating due to uncaught exception" abort).
    try {
        InvokeIndirectCpu(kNsmbwEntryAddress, &cpu);
    } catch (const Memory::AccessViolation& ex) {
        std::ostringstream details;
        details << "addr=0x" << std::hex << std::uppercase << ex.address()
                << " len=0x" << ex.length() << std::dec << std::nouppercase
                << " reason=" << ex.reason();
        RuntimeCrash::WriteCrashArtifacts("access_violation", details.str(), nullptr);
        ShowRuntimeFatalPopup("a guest memory access was out of bounds", details.str());
        std::fprintf(stderr, "[nsmbw] guest memory access violation: %s\n", details.str().c_str());
        return 1;
    } catch (const std::exception& ex) {
        RuntimeCrash::WriteCrashArtifacts("exception", ex.what(), nullptr);
        ShowRuntimeFatalPopup("a runtime exception occurred", ex.what());
        std::fprintf(stderr, "[nsmbw] runtime exception: %s\n", ex.what());
        return 1;
    }

    std::printf("[nsmbw] Entry point returned; r3=0x%08X\n", cpu.gpr[3]);
    return 0;
}
