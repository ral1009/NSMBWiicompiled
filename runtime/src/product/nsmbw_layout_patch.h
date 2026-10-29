#pragma once

// Adds the Options button to NSMBW's own pause-menu layouts by serving patched copies of their
// archives (Layout/pauseMenu/pauseMenu.arc). The copies are built at startup from the disc's own
// files and written to the app data directory; nothing derived from the disc ships with the
// program. See nsmbw_layout_patch.cpp for the edits and projects/nsmbw/native/nsmbw_pause_options.cpp
// for the game code that drives the new button.
namespace nsmbw_layout_patch {
// Registers the DVD hook. Once at startup, before DVDInit.
void Register() noexcept;
} // namespace nsmbw_layout_patch

// True once the patched pause menu is being served. The pause-window overrides only drive a third
// button when it is: registering animations for a group the layout lacks would crash the game.
extern "C" bool NsmbwPauseMenuHasOptions();
// The same for the world-map menu (corseSelectMenu.arc, nsmbw_map_options.cpp).
extern "C" bool NsmbwMapMenuHasOptions();
