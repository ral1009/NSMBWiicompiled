// Host ISA guard. Every other product target builds with MKW_HOST_MARCH_FLAG (x86-64-v3, or v2
// for the compat exe), so a machine below that level would otherwise die on an
// illegal-instruction fault with no explanation. MKW_HOST_ISA_LEVEL (2 or 3) comes from CMake.
// This TU alone skips that flag (own CMake object library, excluded from unity build/PCH) and
// runs from a priority-101 C initializer, ahead of every C++ dynamic initializer and thus the
// first AVX2 code that could execute. Keep it free of anything that could pull in vectorized
// code: no iostreams, no std::string, no runtime-wide headers.
//
// A v3 exe that finds the CPU lacking starts "<its name>-compat.exe" from its own folder when
// that file exists (the v2 build, shipped beside it), so players never have to pick an exe.

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <cpuid.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

void HostCpuId(unsigned leaf, unsigned subleaf, unsigned regs[4]) {
    unsigned eax = 0, ebx = 0, ecx = 0, edx = 0;
    __cpuid_count(leaf, subleaf, eax, ebx, ecx, edx);
    regs[0] = eax;
    regs[1] = ebx;
    regs[2] = ecx;
    regs[3] = edx;
}

unsigned HostCpuIdMaxLeaf(unsigned base) {
    unsigned regs[4] = {0, 0, 0, 0};
    HostCpuId(base, 0, regs);
    return regs[0];
}

// XGETBV through inline assembly rather than the _xgetbv intrinsic: the GNU
// driver gates that intrinsic behind -mxsave, which this file is specifically
// compiled without. Only reachable once CPUID has reported OSXSAVE.
uint64_t ReadXcr0() {
    unsigned eax = 0, edx = 0;
    __asm__ __volatile__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
    return (static_cast<uint64_t>(edx) << 32) | eax;
}

struct CpuFeature {
    const char* name;
    unsigned leaf;
    unsigned subleaf;
    unsigned reg;  // index into the eax/ebx/ecx/edx array filled by HostCpuId
    unsigned bit;
    bool isOsXsave;
    unsigned level;  // lowest x86-64 level (2 or 3) that requires this feature
};

#if !defined(MKW_HOST_ISA_LEVEL)
#define MKW_HOST_ISA_LEVEL 3
#endif

// The x86-64 psABI levels, spelled out so the error message can name the exact instruction sets
// the machine lacks rather than only "AVX2", which is merely the best known member of the set.
// v3 includes all of v2.
constexpr CpuFeature kRequiredFeatures[] = {
    {"SSE3", 1, 0, 2, 0, false, 2},
    {"SSSE3", 1, 0, 2, 9, false, 2},
    {"FMA", 1, 0, 2, 12, false, 3},
    {"CMPXCHG16B", 1, 0, 2, 13, false, 2},
    {"SSE4.1", 1, 0, 2, 19, false, 2},
    {"SSE4.2", 1, 0, 2, 20, false, 2},
    {"MOVBE", 1, 0, 2, 22, false, 3},
    {"POPCNT", 1, 0, 2, 23, false, 2},
    {"OSXSAVE", 1, 0, 2, 27, true, 3},
    {"AVX", 1, 0, 2, 28, false, 3},
    {"F16C", 1, 0, 2, 29, false, 3},
    {"BMI1", 7, 0, 1, 3, false, 3},
    {"AVX2", 7, 0, 1, 5, false, 3},
    {"BMI2", 7, 0, 1, 8, false, 3},
    {"LAHF-SAHF", 0x80000001u, 0, 2, 0, false, 2},
    {"LZCNT", 0x80000001u, 0, 2, 5, false, 3},
};

// Fixed-capacity text accumulation: no allocation, no exceptions, nothing that
// could route through code this file is trying to stay ahead of.
struct TextBuffer {
    char data[1024] = {};
    size_t used = 0;

    void Append(const char* text) {
        if (text == nullptr) {
            return;
        }
        while (*text != '\0' && used + 1 < sizeof(data)) {
            data[used++] = *text++;
        }
        data[used] = '\0';
    }
};

// True when the host has every feature of x86-64 level `level` (2 or 3). Otherwise
// `missing` holds the absent feature names, comma separated.
bool CollectMissingFeaturesForLevel(unsigned level, TextBuffer& missing) {
    const unsigned maxBasic = HostCpuIdMaxLeaf(0);
    const unsigned maxExtended = HostCpuIdMaxLeaf(0x80000000u);

    bool ok = true;
    bool haveOsXsave = false;

    for (const CpuFeature& feature : kRequiredFeatures) {
        if (feature.level > level) {
            continue;
        }
        const bool leafAvailable = (feature.leaf & 0x80000000u) != 0
                                       ? feature.leaf <= maxExtended
                                       : feature.leaf <= maxBasic;

        bool present = false;
        if (leafAvailable) {
            unsigned regs[4] = {0, 0, 0, 0};
            HostCpuId(feature.leaf, feature.subleaf, regs);
            present = (regs[feature.reg] & (1u << feature.bit)) != 0;
        }

        if (present) {
            haveOsXsave = haveOsXsave || feature.isOsXsave;
            continue;
        }

        if (!ok) {
            missing.Append(", ");
        }
        missing.Append(feature.name);
        ok = false;
    }

    // CPUID reporting AVX is not sufficient: the OS also has to have enabled
    // XMM and YMM state saving or every VEX-encoded instruction faults. This is
    // the same guard a compiler's own runtime feature dispatch applies.
    if (ok && haveOsXsave) {
        constexpr uint64_t kXmmAndYmmState = 0x6u;
        if ((ReadXcr0() & kXmmAndYmmState) != kXmmAndYmmState) {
            missing.Append("operating system support for AVX register state (XCR0 YMM bits)");
            ok = false;
        }
    }

    return ok;
}

// True when the host can run this build.
bool CollectMissingBaselineFeatures(TextBuffer& missing) {
    return CollectMissingFeaturesForLevel(MKW_HOST_ISA_LEVEL, missing);
}

// stdio is NOT usable from a .CRT$XIC initializer - the UCRT has not stood it
// up yet, and fprintf(stderr, ...) faults there. Verified on this toolchain:
// WriteFile on the raw standard-error handle and MessageBoxA both work, printf
// does not. Anything added to this reporting path has to respect that.
//
// The POSIX path runs from an __attribute__((constructor)) instead, ahead of libc's own startup
// guarantees; ::write() on the raw fd is the same kind of allocation-free, libc-init-independent
// primitive as WriteFile is on Windows, so the same restriction is honored here.
void WriteStdErrEarly(const char* text) {
#if defined(_WIN32)
    const HANDLE handle = ::GetStdHandle(STD_ERROR_HANDLE);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return;
    }
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    DWORD written = 0;
    ::WriteFile(handle, text, static_cast<DWORD>(length), &written, nullptr);
#else
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    (void)::write(STDERR_FILENO, text, length);
#endif
}

#if defined(_WIN32)
bool EnvFlagSet(const char* name) {
    char value[8] = {};
    const DWORD length = ::GetEnvironmentVariableA(name, value, sizeof(value));
    return length > 0 && length < sizeof(value) && value[0] != '0';
}
#endif

#if defined(_WIN32) && MKW_HOST_ISA_LEVEL >= 3
// Runs "<this exe minus .exe>-compat.exe" with the same arguments, waits for it and leaves with
// its exit code (so scripts and the console see one program), if that file exists. Returns only
// when there is nothing to start. Kernel32 calls only, for the same reason as WriteStdErrEarly.
// MKW_NO_COMPAT_FALLBACK=1 skips this, to see the error message on a machine that has the exe.
void TryRunCompatExe() {
    if (EnvFlagSet("MKW_NO_COMPAT_FALLBACK")) {
        return;
    }
    static wchar_t path[32768];
    const DWORD length = ::GetModuleFileNameW(nullptr, path, 32768 - 16);
    if (length == 0 || length >= 32768 - 16) {
        return;
    }
    DWORD stem = length;
    if (stem >= 4 && path[stem - 4] == L'.') {
        stem -= 4;  // drop ".exe" (any case)
    }
    const wchar_t suffix[] = L"-compat.exe";
    for (DWORD i = 0; i < sizeof(suffix) / sizeof(suffix[0]); ++i) {
        path[stem + i] = suffix[i];
    }
    if (::GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        return;
    }

    // New command line: the compat exe's quoted path, then everything after our own argv[0]
    // (Windows rule: a quoted argv[0] ends at the next quote, an unquoted one at whitespace).
    const wchar_t* rest = ::GetCommandLineW();
    if (*rest == L'"') {
        ++rest;
        while (*rest != L'\0' && *rest != L'"') ++rest;
        if (*rest == L'"') ++rest;
    } else {
        while (*rest != L'\0' && *rest != L' ' && *rest != L'\t') ++rest;
    }
    static wchar_t commandLine[32768 * 2];
    size_t used = 0;
    commandLine[used++] = L'"';
    for (const wchar_t* p = path; *p != L'\0'; ++p) commandLine[used++] = *p;
    commandLine[used++] = L'"';
    for (const wchar_t* p = rest; *p != L'\0' && used + 1 < sizeof(commandLine) / sizeof(wchar_t); ++p)
        commandLine[used++] = *p;
    commandLine[used] = L'\0';

    // The child inherits our environment; the test hook must not make it fail as well.
    ::SetEnvironmentVariableA("MKW_FORCE_UNSUPPORTED_CPU", nullptr);
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    // Inherit handles so the child writes to the same console / redirected log files.
    if (!::CreateProcessW(path, commandLine, nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup,
                          &process)) {
        return;
    }
    WriteStdErrEarly("[runtime] This CPU lacks AVX2; running the compat build instead.\n");
    ::CloseHandle(process.hThread);
    ::WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    ::GetExitCodeProcess(process.hProcess, &exitCode);
    ::ExitProcess(exitCode);
}
#endif

[[noreturn]] void ReportUnsupportedCpu(const char* missing) {
#if defined(_WIN32) && MKW_HOST_ISA_LEVEL >= 3
    TryRunCompatExe();
#endif
    TextBuffer message;
#if MKW_HOST_ISA_LEVEL >= 3
    message.Append(
        "This build needs a processor that supports AVX2 and the rest of the "
        "x86-64-v3 instruction set.\n\nMissing on this machine: ");
    message.Append(missing);
    message.Append(
        "\n\nx86-64-v3 covers Intel Core processors from Haswell (4th "
        "generation, 2013) onward and AMD processors from Excavator (2015) onward. "
        "Older processors need the compat build (the -compat.exe).");
#else
    message.Append(
        "This build needs a processor that supports SSE4.2 and the rest of the "
        "x86-64-v2 instruction set.\n\nMissing on this machine: ");
    message.Append(missing);
    message.Append(
        "\n\nx86-64-v2 covers Intel Core processors from Nehalem (2008) onward and AMD "
        "processors from Bulldozer (2011) onward.");
#endif

    // The tag matches RT_TAG_RUNTIME in runtime_log.h. It is spelled out here
    // because this translation unit must not include runtime-wide headers (see
    // the file comment): runtime_log.h pulls in <iostream> and memory.h, and
    // this code runs before any C++ dynamic initializer.
    WriteStdErrEarly("[runtime] ");
    WriteStdErrEarly(message.data);
    WriteStdErrEarly("\n");

#if defined(_WIN32)
    ::MessageBoxA(nullptr, message.data, "WiiCompiled - Unsupported Processor",
                  MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TASKMODAL);
    // Leave through the OS rather than exit(): the C++ dynamic initializers
    // have not run yet, so there is no constructed program state to unwind and
    // the teardown path itself lives in AVX2 translation units.
    ::ExitProcess(1u);
#else
    // Same reasoning as the Windows path above: no C++ dynamic initializer has run yet, so
    // _exit() (skips atexit/global destructors, unlike exit()) is the correct way out.
    ::_exit(1);
#endif
}

}  // namespace

// For the NSMBW launcher, which picks the game DLL to load by CPU level: a query that never exits.
// `missing` (may be null) receives the absent features, comma separated.
extern "C" bool MkwHostCpuMeetsLevel(unsigned level, char* missing, unsigned missingSize) {
    TextBuffer names;
    const bool ok = CollectMissingFeaturesForLevel(level, names);
    if (missing != nullptr && missingSize > 0) {
        unsigned i = 0;
        for (; i + 1 < missingSize && names.data[i] != '\0'; ++i) missing[i] = names.data[i];
        missing[i] = '\0';
    }
    return ok;
}

#if !defined(MKW_CPU_BASELINE_NO_CTOR)
extern "C" int MkwHostCpuBaselineInit() {
    TextBuffer missing;
    bool supported = CollectMissingBaselineFeatures(missing);
#if defined(_WIN32)
    // Test hook: act as if the CPU failed the check, to exercise the compat hand-off and the
    // message on a machine that passes it.
    if (supported && EnvFlagSet("MKW_FORCE_UNSUPPORTED_CPU")) {
        missing.Append("(none - MKW_FORCE_UNSUPPORTED_CPU is set)");
        supported = false;
    }
#endif
    if (!supported) {
        ReportUnsupportedCpu(missing.data);
    }
    return 0;
}

// Priorities 0-100 are reserved for the implementation; 101 is the earliest a
// user constructor can request, which puts this ahead of every default-priority
// constructor in the image.
__attribute__((constructor(101))) static void MkwHostCpuBaselineCtor() {
    MkwHostCpuBaselineInit();
}
#endif  // !MKW_CPU_BASELINE_NO_CTOR
