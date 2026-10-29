#pragma once

#include <filesystem>
#include <string>

// Disc files a product generates at startup, served in place of the disc's own (NSMBW: the pause
// menu layout with an Options button, nsmbw_layout_patch.cpp). The hook runs once inside DVDInit,
// after the disc and any overlay roots are indexed and before the FST is published, so a
// replacement may have a different size: the guest allocates its buffer from the published FST.
namespace RuntimeDvd {
using GeneratedFilesHook = void (*)();
void SetGeneratedFilesHook(GeneratedFilesHook hook) noexcept;

// Inside the hook: the host file currently served for a disc path ("/Layout/a/a.arc"), or an
// empty path when there is none.
std::filesystem::path HostPathFor(const std::string& dvdPath);
// Inside the hook: serve `hostPath` for `dvdPath` from now on. False when it can't be read.
bool ServeFile(const std::string& dvdPath, const std::filesystem::path& hostPath);
} // namespace RuntimeDvd
