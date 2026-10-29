#pragma once

#include <cstdint>

// The NSMBW options menu: a full-screen menu drawn with the game's own pause-menu art
// (nsmbw_menu_art / nsmbw_menu_draw), opened with F10. Graphics, Audio and Advanced change the real
// settings (settings_overlay::settings); Controls shows which controller drives each player.
// Keyboard: arrows move and change, Enter / Z select, Esc / Backspace go back, Q / E switch tabs.
// Controller: d-pad or left stick, A select, B back, LB / RB tabs, Start closes.
namespace nsmbw_options_menu {
// Hands the menu to settings_overlay (F10 opens it; Shift+F10 the old F10 bar). Once at startup.
void Register() noexcept;
// The next menu sound to play (an NSMBW SOUND_e id), or -1. Taken on the game thread.
int32_t TakeSound() noexcept;
}

// The same for the KPAD override (projects/nsmbw/native), which plays it with the game's
// SndAudioMgr::startSystemSe.
extern "C" int32_t NsmbwOptionsMenuTakeSound();
