#pragma once

#include "nsmbw_menu_art.h"

#include <imgui.h>

#include <cstdint>
#include <string_view>

// Draws NSMBW's pause-menu style with ImGui's low-level draw list (textured and coloured quads; no
// ImGui widgets). Positions are layout units like a BRLYT pane: origin at the centre of the screen,
// y up, 608 units across, which is how widescreen NSMBW scales its 608x456 layouts (the stock pause
// window, 398 units wide, measures two-thirds of the screen width in a real screenshot).
namespace nsmbw_menu_draw {

using Color = uint32_t; // 0xRRGGBBAA
constexpr Color Rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return uint32_t(r) << 24 | uint32_t(g) << 16 | uint32_t(b) << 8 | a;
}
constexpr Color kWhite = Rgb(255, 255, 255);
constexpr Color kBlack = Rgb(0, 0, 0);
// A text material's "black" colour has alpha 0 in the game's layouts (T_tuzukeru_00 is
// (17,45,17,0)): the glyph's alpha then comes from the texel alone, so the empty part of each glyph
// cell stays transparent. With an opaque black every glyph cell drew as a solid box.
constexpr Color kClear = Rgb(0, 0, 0, 0);

enum class Align { Left, Center, Right };

struct TextStyle {
    const nsmbw_menu_art::Font* font = nullptr; // message font when null
    Color black = kClear;  // material colour for the glyph's dark texels
    Color white = kWhite;  // and for its light texels (fill)
    Color top = kWhite;    // text colour gradient, multiplied in (TextBox top / bottom colours)
    Color bottom = kWhite;
    Align align = Align::Center;
    float alpha = 1.0f;
    bool outline = false;  // a black outline around the glyphs (the message font has none of its own)
};

class Canvas {
public:
    // The largest 16:9 area of the ImGui display, drawn on the foreground draw list.
    // A selected Button is 1.13x the size of the others, like the pause menu's (onButton). By
    // default it grows; with shrinkOthers the others shrink to 1/1.13 instead, for tightly packed
    // rows where a grown button overlapped its neighbours (developer's suggestion, 2026-10-28).
    // zoom scales the whole canvas about the screen centre (the window's in / out animation).
    explicit Canvas(bool shrinkOthers = false, float zoom = 1.0f);

    ImVec2 Px(float x, float y) const;
    float Scale() const { return m_scale; }
    // Layout-unit point under a screen position (for mouse hits).
    ImVec2 Units(ImVec2 screen) const;

    // Vertical gradient rectangle centred on (cx, cy).
    void Rect(float cx, float cy, float w, float h, Color top, Color bottom) const;
    // The whole display (not only the 16:9 area), e.g. to darken the game behind the menu.
    void FillDisplay(Color color) const;

    // A one-texture window frame (the pause window): the corner texture mirrored to all four
    // corners, edges stretched from its last row and column, tinted with black/white.
    void Window1(float cx, float cy, float w, float h, nsmbw_menu_art::Art corner, Color black, Color white,
                 float cornerUnits = 32.0f) const;
    // The pause-menu button frame (W_SButton): graButton up / upRight / under corners, untinted.
    void Window4(float cx, float cy, float w, float h, float cornerUnits = 24.0f) const;

    float Measure(std::string_view text, float size, const nsmbw_menu_art::Font* font = nullptr) const;
    // Draws text centred vertically on cy; returns its width in layout units.
    float Text(std::string_view text, float cx, float cy, float size, const TextStyle& style = {}) const;
    // Menu text: the fill colour with a black outline and, optionally, a soft black drop shadow
    // (where the game's pause buttons use a green one, T_tuzukeru_01; black was asked for instead).
    float Label(std::string_view text, float cx, float cy, float size, Align align = Align::Center,
                bool shadow = true, Color color = kWhite) const;
    // Mario font, with an optional top -> bottom colour gradient.
    float Mario(std::string_view text, float cx, float cy, float size, Color top = kWhite, Color bottom = kWhite,
                Align align = Align::Center) const;

    // The pause-menu button: on = selected (green gradient, 1.13x); dim = greyed out. `over` draws
    // the label after the glossy frame so the frame's white fade can't cover it. Returns its size.
    ImVec2 Button(std::string_view text, float cx, float cy, float w, float h, bool on, float size,
                  bool dim = false, bool over = false) const;
    // Plays the press animation on the button drawn at (cx, cy) with width w.
    void Press(float cx, float cy, float w) const;
    // The yellow four-corner selection cursor around a w x h box.
    void Cursor(float cx, float cy, float w, float h, float scale = 0.8f) const;
    // The pause window: 50% black behind a red rounded frame.
    void PauseWindow(float cx, float cy, float w, float h, uint8_t fillAlpha = 128) const;
    // A prompt glyph texture, size units square.
    void Image(ImTextureID texture, float cx, float cy, float size) const;

private:
    void Quad(ImTextureID texture, ImVec2 p0, ImVec2 p1, ImVec2 uv0, ImVec2 uv1, Color top, Color bottom) const;

    ImDrawList* m_list;
    ImVec2 m_displaySize;
    ImVec2 m_center;
    float m_scale;
    float m_selectedScale;
};

// Menu time in 60 fps frames, and the pause window's in / out scale that many frames in.
float Now();
float WindowIn(float frames);
float WindowOut(float frames);
constexpr float kWindowFrames = 8.0f;

} // namespace nsmbw_menu_draw
