#pragma once
#include "setup.h"

LinuxGame CheckLinuxDisc(const std::filesystem::path& input, bool fastCpu);
LinuxGame ImportLinuxDisc(const std::filesystem::path& input, bool fastCpu);
std::filesystem::path LinuxTexturePack();
void InstallLinuxTexturePack(const std::filesystem::path& input);
