#pragma once

#include <string>

namespace nsmbw_launcher {

// Makes sure Config.toml's [paths] dvd_root names an extracted disc of a version this build
// supports. Shows the setup window when it does not, or when `showWindow` is set (Shift held at
// start). Returns the game version tag to load ("P1"), or an empty string when the player quit.
std::wstring EnsureSetup(bool showWindow);

}  // namespace nsmbw_launcher
