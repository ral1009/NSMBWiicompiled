#pragma once

#include <cstdint>

// Skipping the opening cutscene and hint movies with the - button (nsmbw_movie_skip.cpp).
// The first press shows "(-) Skip" in the bottom-right corner, a second press while it shows skips.
namespace nsmbw_movie_skip {
// Draws the prompt while it is showing. Call from the ImGui frame.
void Draw() noexcept;
}

// Called by the KPAD override (nsmbw_kpad_overrides.cpp) on the game thread, once per player per
// VI frame, with that player's WPAD hold bits: counts - presses while a skippable scene runs and
// returns the bits with - removed, so the game never sees them there.
extern "C" uint32_t NsmbwMovieSkipFilterInput(uint32_t chan, uint32_t hold);
// What the KPAD override should do on the game thread this frame: 0 nothing, 1 end the opening,
// 2 leave the hint movie. Returns each request once.
extern "C" int32_t NsmbwMovieSkipTake();
