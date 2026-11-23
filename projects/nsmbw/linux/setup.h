#pragma once

#include <filesystem>
#include <string>

struct LinuxGame {
    std::filesystem::path disc;
    std::filesystem::path library;
    std::string version;
};

// An empty result means the player closed setup.
LinuxGame PrepareLinuxGame(const std::filesystem::path& input, bool showSetup,
                           bool unattended, bool fastCpu);
