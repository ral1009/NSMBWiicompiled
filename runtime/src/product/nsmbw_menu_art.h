#pragma once

#include <imgui.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

// The game's own menu art, read from the extracted disc at runtime: the pause-menu window frame and
// button frame, the selection cursor and the two fonts. The options menu (nsmbw_options_menu.cpp)
// draws with these so it looks like the game's own pause menu. Nothing here ships with the build.
namespace nsmbw_menu_art {

struct Image {
    int width = 0; // the game's own size; glyph and frame coordinates are in these texels
    int height = 0;
    std::vector<uint8_t> rgba; // decoded texels, 4 bytes each
    // The texture as the game stores it, to find its HD texture-pack version (which Tinted uses
    // instead when the pack is on and has one; the pack keys files by a hash of these bytes).
    std::vector<uint8_t> gx;
    uint32_t gxFormat = 0;
};

// A BRFNT font: glyph images on sheets plus per-glyph widths.
struct Font {
    struct Glyph {
        uint16_t index = 0; // cell number across all sheets
        int8_t left = 0;    // blank texels before the ink
        uint8_t width = 0;  // ink width
        int8_t advance = 0; // pen advance
    };
    int cellWidth = 0;
    int cellHeight = 0;
    int sheetColumns = 0; // cells per sheet row
    int sheetRows = 0;
    int height = 0; // FINF height; a text pane's font size is relative to it
    Glyph fallback{};
    std::vector<Image> sheets;
    std::unordered_map<uint32_t, Glyph> glyphs;

    const Glyph* Find(uint32_t codepoint) const noexcept;
};

enum class Art {
    PauseFrame,    // pauseMenu im_kadomaruFlame_00: rounded window corner
    ButtonUp,      // pauseMenu im_graButton_up_24_00: button top-left corner
    ButtonUpRight, // im_graButton_upRight_24_00: top-right corner
    ButtonUnder,   // im_graButton_under_24_00: bottom-left corner
    Cursor,        // select_cursor msk_cursol_00: one corner of the selection cursor
    Count,
};

// Reads everything from the disc the first time it is called. False (with one log line) when a file
// is missing or can't be parsed; the menu is then not drawn.
bool Load() noexcept;
const Image& Get(Art art) noexcept;
const Font& MessageFont() noexcept; // mj2d00_MessageFont_32_I4: body text and button labels
const Font& MarioFont() noexcept;   // mj2d01_marioFont_64_IA4: titles and numbers

// An nw4r material's colour mapping applied to an image: each texel's colour becomes
// lerp(black, white, texel) and its alpha lerp(black alpha, white alpha, texel alpha). Colours are
// 0xRRGGBBAA. Uses the HD texture pack's version of the image when there is one (same layout,
// higher resolution, so texel coordinates still map). Cached per image and colour pair; uploaded on
// first use, so call from the ImGui frame.
ImTextureID Tinted(const Image& image, uint32_t black, uint32_t white) noexcept;

} // namespace nsmbw_menu_art
