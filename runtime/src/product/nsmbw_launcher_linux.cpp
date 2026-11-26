#include "setup.h"

#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <sys/resource.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

extern "C" bool MkwHostCpuMeetsLevel(unsigned level, char* missing, unsigned missingSize);

namespace fs = std::filesystem;

namespace {

void EnsureStackSize() {
    // Translated guest calls need the same 16 MiB stack reserved by the Windows launcher.
    constexpr rlim_t needed = 16 * 1024 * 1024;
    rlimit limit{};
    if (getrlimit(RLIMIT_STACK, &limit) != 0) throw std::runtime_error("Could not read the stack limit.");
    if (limit.rlim_cur == RLIM_INFINITY || limit.rlim_cur >= needed) return;
    if (limit.rlim_max != RLIM_INFINITY && limit.rlim_max < needed) {
        throw std::runtime_error("The game needs a 16 MiB stack. Increase the hard stack limit.");
    }
    limit.rlim_cur = needed;
    if (setrlimit(RLIMIT_STACK, &limit) != 0) throw std::runtime_error("Could not increase the stack limit.");
}

int Run(int argc, char** argv, bool& unattended) {
    fs::path disc;
    bool setup = std::getenv("NSMBW_SETUP_SHOW") != nullptr;
    if (const char* image = std::getenv("NSMBW_SETUP_IMAGE"); image && *image) {
        disc = image;
        unattended = true;
    }
    if (const char* folder = std::getenv("NSMBW_SETUP_FOLDER"); folder && *folder) {
        disc = folder;
        unattended = true;
    }
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") {
            std::puts("Usage: NSMBWCompiled [--disc <image or folder>] [--setup]\n"
                      "Select a .wbfs, .iso, .rvz or extracted disc on first launch.\n"
                      "Hold Shift at startup or use --setup to change the disc or HD textures.");
            return 0;
        }
        if (arg == "--setup") setup = true;
        else if (arg == "--disc" && i + 1 < argc) {
            disc = argv[++i];
            unattended = true;
        } else throw std::runtime_error("Unknown or incomplete option: " + arg + ". See --help.");
    }

    char missing[512]{};
    const char* forced = std::getenv("MKW_FORCE_UNSUPPORTED_CPU");
    const bool forceCompat = forced && *forced && *forced != '0';
    const bool cpuIsV3 = MkwHostCpuMeetsLevel(3, missing, sizeof(missing)) && !forceCompat;
    unsetenv("MKW_FORCE_UNSUPPORTED_CPU");
    if (!cpuIsV3 && !MkwHostCpuMeetsLevel(2, missing, sizeof(missing))) {
        throw std::runtime_error(std::string("This processor needs x86-64-v2 support. Missing: ") + missing);
    }

    const auto selected = PrepareLinuxGame(disc, setup, unattended, cpuIsV3);
    if (selected.disc.empty()) return 0;
    const auto& library = selected.library;
    EnsureStackSize();
    void* game = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!game) throw std::runtime_error("Could not load " + library.string() + ": " + dlerror());
    auto entry = reinterpret_cast<int (*)()>(dlsym(game, "NsmbwGameMain"));
    if (!entry) throw std::runtime_error("Missing NsmbwGameMain export in " + library.string());
    // The runtime owns background threads and exit handlers; keep it loaded until process exit.
    return entry();
}

}  // namespace

int main(int argc, char** argv) {
    bool unattended = argc > 1 || std::getenv("NSMBW_AUTO_PRESS_SELFTEST") != nullptr;
    try {
        return Run(argc, argv, unattended);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[launcher] %s\n", error.what());
        if (!unattended) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "NSMBWCompiled", error.what(), nullptr);
        return 1;
    }
}
