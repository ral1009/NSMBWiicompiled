#include "disc_setup.h"
#include "runtime_config.h"

#include <SDL3/SDL.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/file.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
fs::path LauncherDirectory() {
    auto dir = RuntimeConfigFile::ExecutableDirectory();
    if (!dir) throw std::runtime_error("Could not locate the launcher.");
    return *dir;
}

bool HasDiscFiles(const fs::path& path) {
    return !path.empty() && fs::is_directory(path / "files") && fs::is_regular_file(path / "sys/fst.bin");
}

// Work beside the final directory so a validated import can be published in a single rename.
struct ImportDirectory {
    fs::path path;
    int lock = -1;
    explicit ImportDirectory(const fs::path& parent) {
        fs::create_directories(parent);
        auto name = (parent / ".import-XXXXXX").string();
        if (!mkdtemp(name.data())) throw std::system_error(errno, std::generic_category(), "Create import folder");
        path = name;
    }
    void lockDestination() {
        lock = open((path.parent_path() / ".setup.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (lock < 0 || flock(lock, LOCK_EX) != 0)
            throw std::system_error(errno, std::generic_category(), "Lock import folder");
    }
    void publish(const fs::path& src, const fs::path& dest) {
        if (!fs::exists(dest)) fs::rename(src, dest);
        else if (syscall(SYS_renameat2, AT_FDCWD, src.c_str(), AT_FDCWD, dest.c_str(), RENAME_EXCHANGE) != 0)
            throw std::system_error(errno, std::generic_category(), "Replace previous import");
    }
    ~ImportDirectory() {
        if (lock >= 0) close(lock);
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};
} // namespace

LinuxGame CheckLinuxDisc(const fs::path& input, bool fastCpu) {
    fs::path root = input;
    if (!HasDiscFiles(root)) {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(input, ec)) {
            if (HasDiscFiles(entry.path())) { root = entry.path(); break; }
        }
        if (!HasDiscFiles(root))
            throw std::runtime_error("Select a disc image or the extracted folder containing sys and files.");
    }
    std::ifstream boot(root / "sys/boot.bin", std::ios::binary);
    char header[8]{};
    if (!boot.read(header, sizeof(header))) throw std::runtime_error("Could not read the disc header.");
    const std::string id(header, 6);
    const unsigned rev = static_cast<unsigned char>(header[7]);
    if ((id != "SMNE01" && id != "SMNP01") || rev != 1)
        throw std::runtime_error("Unsupported disc: " + id + " revision " + std::to_string(rev) +
                                 ". Use NSMB Wii USA or PAL, revision 1.");
    const std::string version = id == "SMNE01" ? "E1" : "P1";
    const auto stem = LauncherDirectory() / ("NSMBWCompiled-" + version);
    const fs::path fast = stem.string() + ".so";
    const fs::path compat = stem.string() + "-compat.so";
    if (fastCpu && fs::is_regular_file(fast)) return {fs::absolute(root), fast, version};
    if (fs::is_regular_file(compat)) return {fs::absolute(root), compat, version};
    if (fs::is_regular_file(fast)) throw std::runtime_error("This CPU needs the compat game library: " + compat.filename().string());
    throw std::runtime_error("Game library missing for this disc: " + fast.filename().string());
}

LinuxGame ImportLinuxDisc(const fs::path& input, bool fastCpu) {
    LinuxGame result;
    if (fs::is_regular_file(input)) {
        ImportDirectory staging(RuntimeConfigFile::ApplicationDataDirectory() / "discs");
        const auto tool = LauncherDirectory() / "nodtool";
        const auto image = fs::absolute(input);
        const auto output = staging.path / "disc";
        const char* args[] = {tool.c_str(), "extract", "-q", image.c_str(), output.c_str(), nullptr};
        auto* process = SDL_CreateProcess(args, false);
        if (!process) throw std::runtime_error(std::string("Cannot start the bundled disc extractor: ") + SDL_GetError());
        int status = -1;
        bool finished = SDL_WaitProcess(process, true, &status);
        SDL_DestroyProcess(process);
        if (!finished || status != 0)
            throw std::runtime_error("Could not extract the Wii disc. Check the image and available disk space.");
        result = CheckLinuxDisc(output, fastCpu);
        const auto destination = staging.path.parent_path() / result.version;
        staging.lockDestination();
        staging.publish(result.disc, destination);
        result.disc = destination;
    } else {
        result = CheckLinuxDisc(input, fastCpu);
    }
    RuntimeConfigFile::EnsureConfigFile();
    if (!RuntimeConfigFile::WriteSetting("paths", "dvd_root",
            RuntimeConfigFile::FormatString(RuntimeConfigFile::PathToUtf8(result.disc))))
        throw std::runtime_error("Could not save the disc location.");
    return result;
}

fs::path LinuxTexturePack() {
    return RuntimeConfigFile::ApplicationDataDirectory() / "nsmbw_data/texture_replacements/SMN";
}

void InstallLinuxTexturePack(const fs::path& input) {
    fs::path src = fs::canonical(input);
    if (src.filename() != "SMN") {
        src /= fs::is_directory(src / "SMN") ? "SMN" : "Load/Textures/SMN";
    }
    if (!fs::is_directory(src) || fs::is_empty(src))
        throw std::runtime_error("Choose a texture pack containing an SMN folder, or SMN itself.");
    const auto dest = LinuxTexturePack();
    const auto relative = fs::weakly_canonical(dest).lexically_relative(fs::canonical(src));
    if (relative != ".") {
        if (!relative.empty() && *relative.begin() != "..")
            throw std::runtime_error("Pick a texture pack outside the installation folder.");
        ImportDirectory staging(dest.parent_path());
        staging.lockDestination();
        const auto out = staging.path / "SMN";
        const auto options = fs::copy_options::recursive | fs::copy_options::skip_symlinks;
        if (fs::is_directory(dest)) fs::copy(dest, out, options);
        fs::copy(src, out, options | fs::copy_options::overwrite_existing);
        staging.publish(out, dest);
    }
    if (!RuntimeConfigFile::SetTextureReplacements(true))
        throw std::runtime_error("Could not enable the HD texture pack in Config.toml.");
}
