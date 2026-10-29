// See nsmbw_menu_draw.h. The colours and sizes come from the disc's layout files
// (Layout/pauseMenu/pauseMenu.arc, pauseMenu_16.brlyt and its .brlan animations):
//   window   P_bg_00 = black at 128/255; W_N_pauseMenu_00 frame material black #320000, white #E66464
//   button   P_SBBase vertex colours: selected #009646 -> #F0FF32 (onButton end state, scale 1.13),
//            unselected #8CDC5A -> #FFFA80 (offButon end state, scale 1.0); frame W_SButton 7x10 units
//            larger than the base; P_shadowBlack (black 140/255) over a greyed-out button
//   label    shadow offset (4, -3) at size 32 and 80/255 alpha as T_tuzukeru_01, but black, and a
//            black outline instead of the game's dark-green edge (asked for on 2026-10-26)
//   cursor   select_cursor_04: msk_cursol corners at scale 0.8, shadows black 60/255 five units below
// Movement is the same files' animations, keyed in 60 fps frames (nw4r Hermite keys: frame, value,
// slope per frame):
//   window in   inWindow  N_pauseMenu_00 scale (0, 0, .401) (8, 1, -.021)
//   window out  outWindow N_pauseMenu_00 scale (0, .997, .029) (8, 0, -.307)
//   button on   onButton  P_SBBase scale (0, 1, .2) (3, 1.17, -.023) (5, 1.13, 0); colours over 5 frames
//   button off  offButon  P_SBBase scale (0, 1.13, 0) (3, 1, 0); colours over 3 frames
//   press       hitButton P_SBBase scale (0, 1.13, 0) (2, .95, -.105 | .056) (6, 1.13, 0)
//   cursor      select_cursor_04 loopCursor, 65 frames: each corner scales (20, 1) (23, 1.1)
//               (30, 1) (40, 1) (48, .9) (58, 1), slopes 0
#include "nsmbw_menu_draw.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <unordered_map>
#include <vector>

namespace nsmbw_menu_draw {
namespace art = nsmbw_menu_art;

namespace {
constexpr Color kOnTop = Rgb(0, 150, 70), kOnBottom = Rgb(240, 255, 50);
constexpr Color kOffTop = Rgb(140, 220, 90), kOffBottom = Rgb(255, 250, 128);
constexpr uint32_t kUntinted0 = 0x00000000u, kUntinted1 = 0xFFFFFFFFu; // lerp(0, 1, texel) = texel

ImU32 ToIm(Color c, float alpha = 1.0f) {
    const float a = float(c & 255) * std::clamp(alpha, 0.0f, 1.0f);
    return IM_COL32(c >> 24 & 255, c >> 16 & 255, c >> 8 & 255, int(a + 0.5f));
}

struct Key {
    float frame, value, slopeIn, slopeOut;
};
constexpr Key K(float f, float v, float s) { return {f, v, s, s}; }

// nw4r's Hermite interpolation between keys (a repeated frame is a break in the slope).
float Curve(std::initializer_list<Key> keys, float t) {
    const Key* k = keys.begin();
    const size_t n = keys.size();
    if (t <= k[0].frame) return k[0].value;
    if (t >= k[n - 1].frame) return k[n - 1].value;
    for (size_t i = 0; i + 1 < n; ++i) {
        const Key &a = k[i], &b = k[i + 1];
        if (t > b.frame || b.frame <= a.frame) continue;
        const float d = b.frame - a.frame, u = (t - a.frame) / d, u2 = u * u, u3 = u2 * u;
        return a.value * (2 * u3 - 3 * u2 + 1) + b.value * (3 * u2 - 2 * u3) + a.slopeOut * d * (u3 - 2 * u2 + u) +
               b.slopeIn * d * (u3 - u2);
    }
    return k[n - 1].value;
}

Color Mix(Color a, Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    Color out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        const float x = float(a >> sh & 255), y = float(b >> sh & 255);
        out |= uint32_t(x + (y - x) * t + 0.5f) << sh;
    }
    return out;
}

// Per button (by position and width): when it last became selected or not, and was last pressed.
struct ButtonState {
    bool on = false;
    float since = -1000.0f;
    float pressed = -1000.0f;
};
std::unordered_map<uint64_t, ButtonState> g_buttons;
uint64_t ButtonKey(float cx, float cy, float w) {
    return uint64_t(uint16_t(int16_t(std::lround(cx)))) | uint64_t(uint16_t(int16_t(std::lround(cy)))) << 16 |
           uint64_t(uint16_t(std::lround(w))) << 32;
}

// UTF-8 -> code points (the menu's own strings; invalid bytes are skipped).
std::vector<uint32_t> Decode(std::string_view s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        const uint8_t c = uint8_t(s[i]);
        // Continuation bytes after the lead byte: 0 for ASCII, 1-3 for multi-byte, -1 if invalid.
        const int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (n < 0 || i + size_t(n) >= s.size() + (n == 0 ? 1 : 0)) { ++i; continue; }
        uint32_t cp = n == 0 ? c : c & (0x3F >> n);
        for (int k = 1; k <= n; ++k) cp = cp << 6 | (uint8_t(s[i + k]) & 0x3F);
        out.push_back(cp);
        i += size_t(n) + 1;
    }
    return out;
}
} // namespace

Canvas::Canvas(bool shrinkOthers, float zoom)
    : m_list(ImGui::GetForegroundDrawList()), m_displaySize(ImGui::GetIO().DisplaySize),
      m_selectedScale(shrinkOthers ? 1.0f : 1.13f) {
    m_center = ImVec2(m_displaySize.x * 0.5f, m_displaySize.y * 0.5f);
    // 608 units across a 16:9 area: 342 units tall. The zoom scales everything about the centre.
    m_scale = std::min(m_displaySize.x / 608.0f, m_displaySize.y / 342.0f) * zoom;
}

float Now() { return float(ImGui::GetTime() * 60.0); }

float WindowIn(float frames) { return Curve({K(0, 0, 0.401f), K(8, 1, -0.021f)}, frames); }
float WindowOut(float frames) { return Curve({K(0, 0.997f, 0.029f), K(8, 0, -0.307f)}, frames); }

void Canvas::Press(float cx, float cy, float w) const { g_buttons[ButtonKey(cx, cy, w)].pressed = Now(); }

ImVec2 Canvas::Px(float x, float y) const { return ImVec2(m_center.x + x * m_scale, m_center.y - y * m_scale); }

ImVec2 Canvas::Units(ImVec2 screen) const {
    return ImVec2((screen.x - m_center.x) / m_scale, (m_center.y - screen.y) / m_scale);
}

void Canvas::Quad(ImTextureID texture, ImVec2 p0, ImVec2 p1, ImVec2 uv0, ImVec2 uv1, Color top, Color bottom) const {
    if (texture == ImTextureID{} || p1.x <= p0.x || p1.y <= p0.y) return;
    m_list->PushTextureID(texture);
    m_list->PrimReserve(6, 4);
    const ImDrawIdx i = ImDrawIdx(m_list->_VtxCurrentIdx);
    m_list->PrimWriteIdx(i);
    m_list->PrimWriteIdx(ImDrawIdx(i + 1));
    m_list->PrimWriteIdx(ImDrawIdx(i + 2));
    m_list->PrimWriteIdx(i);
    m_list->PrimWriteIdx(ImDrawIdx(i + 2));
    m_list->PrimWriteIdx(ImDrawIdx(i + 3));
    m_list->PrimWriteVtx(p0, uv0, ToIm(top));
    m_list->PrimWriteVtx(ImVec2(p1.x, p0.y), ImVec2(uv1.x, uv0.y), ToIm(top));
    m_list->PrimWriteVtx(p1, uv1, ToIm(bottom));
    m_list->PrimWriteVtx(ImVec2(p0.x, p1.y), ImVec2(uv0.x, uv1.y), ToIm(bottom));
    m_list->PopTextureID();
}

void Canvas::Rect(float cx, float cy, float w, float h, Color top, Color bottom) const {
    const ImVec2 p0 = Px(cx - w / 2, cy + h / 2), p1 = Px(cx + w / 2, cy - h / 2);
    m_list->AddRectFilledMultiColor(ImVec2(std::round(p0.x), std::round(p0.y)), ImVec2(std::round(p1.x), std::round(p1.y)),
                                    ToIm(top), ToIm(top), ToIm(bottom), ToIm(bottom));
}

void Canvas::FillDisplay(Color color) const { m_list->AddRectFilled(ImVec2(0, 0), m_displaySize, ToIm(color)); }

void Canvas::Window1(float cx, float cy, float w, float h, art::Art corner, Color black, Color white,
                     float cornerUnits) const {
    const art::Image& img = art::Get(corner);
    if (img.width == 0) return;
    const ImTextureID tex = art::Tinted(img, black, white);
    const ImVec2 a = Px(cx - w / 2, cy + h / 2), b = Px(cx + w / 2, cy - h / 2);
    const float x0 = std::round(a.x), y0 = std::round(a.y), x1 = std::round(b.x), y1 = std::round(b.y);
    const float c = std::round(cornerUnits * m_scale);
    // Last column / row, sampled at the texel centre so the stretch doesn't blend in the edge.
    const float u = (float(img.width) - 0.5f) / float(img.width), v = (float(img.height) - 0.5f) / float(img.height);
    Quad(tex, {x0, y0}, {x0 + c, y0 + c}, {0, 0}, {1, 1}, kWhite, kWhite);
    Quad(tex, {x1 - c, y0}, {x1, y0 + c}, {1, 0}, {0, 1}, kWhite, kWhite);
    Quad(tex, {x0, y1 - c}, {x0 + c, y1}, {0, 1}, {1, 0}, kWhite, kWhite);
    Quad(tex, {x1 - c, y1 - c}, {x1, y1}, {1, 1}, {0, 0}, kWhite, kWhite);
    Quad(tex, {x0 + c, y0}, {x1 - c, y0 + c}, {u, 0}, {u, 1}, kWhite, kWhite);
    Quad(tex, {x0 + c, y1 - c}, {x1 - c, y1}, {u, 1}, {u, 0}, kWhite, kWhite);
    Quad(tex, {x0, y0 + c}, {x0 + c, y1 - c}, {0, v}, {1, v}, kWhite, kWhite);
    Quad(tex, {x1 - c, y0 + c}, {x1, y1 - c}, {1, v}, {0, v}, kWhite, kWhite);
}

void Canvas::Window4(float cx, float cy, float w, float h, float cornerUnits) const {
    const art::Image& up = art::Get(art::Art::ButtonUp);
    const art::Image& upRight = art::Get(art::Art::ButtonUpRight);
    const art::Image& under = art::Get(art::Art::ButtonUnder);
    if (up.width == 0 || upRight.width == 0 || under.width == 0) return;
    const ImTextureID tUp = art::Tinted(up, kUntinted0, kUntinted1);
    const ImTextureID tUpRight = art::Tinted(upRight, kUntinted0, kUntinted1);
    const ImTextureID tUnder = art::Tinted(under, kUntinted0, kUntinted1);
    const ImVec2 a = Px(cx - w / 2, cy + h / 2), b = Px(cx + w / 2, cy - h / 2);
    const float x0 = std::round(a.x), y0 = std::round(a.y), x1 = std::round(b.x), y1 = std::round(b.y);
    const float c = std::round(cornerUnits * m_scale);
    const float e = (float(up.width) - 0.5f) / float(up.width); // last texel column / row of a 24x24 corner
    Quad(tUp, {x0, y0}, {x0 + c, y0 + c}, {0, 0}, {1, 1}, kWhite, kWhite);
    Quad(tUpRight, {x1 - c, y0}, {x1, y0 + c}, {0, 0}, {1, 1}, kWhite, kWhite);
    Quad(tUnder, {x0, y1 - c}, {x0 + c, y1}, {0, 0}, {1, 1}, kWhite, kWhite);
    Quad(tUnder, {x1 - c, y1 - c}, {x1, y1}, {1, 0}, {0, 1}, kWhite, kWhite); // bottom-right: mirrored
    Quad(tUp, {x0 + c, y0}, {x1 - c, y0 + c}, {e, 0}, {e, 1}, kWhite, kWhite);
    Quad(tUnder, {x0 + c, y1 - c}, {x1 - c, y1}, {e, 0}, {e, 1}, kWhite, kWhite);
    Quad(tUp, {x0, y0 + c}, {x0 + c, y1 - c}, {0, e}, {1, e}, kWhite, kWhite);
    Quad(tUpRight, {x1 - c, y0 + c}, {x1, y1 - c}, {0, e}, {1, e}, kWhite, kWhite);
}

float Canvas::Measure(std::string_view text, float size, const art::Font* font) const {
    const art::Font& f = font != nullptr ? *font : art::MessageFont();
    if (f.height == 0) return 0.0f;
    const float k = size / float(f.height);
    float w = 0.0f;
    for (uint32_t cp : Decode(text)) {
        const art::Font::Glyph* g = f.Find(cp);
        w += float(g != nullptr ? g->advance : f.fallback.advance) * k;
    }
    return w;
}

float Canvas::Text(std::string_view text, float cx, float cy, float size, const TextStyle& style) const {
    const art::Font& f = style.font != nullptr ? *style.font : art::MessageFont();
    if (f.height == 0 || f.sheets.empty()) return 0.0f;
    const float width = Measure(text, size, &f);
    const float k = size / float(f.height) * m_scale;
    const ImVec2 at = Px(cx, cy);
    float pen = style.align == Align::Left ? at.x : style.align == Align::Right ? at.x - width * m_scale
                                                                                 : at.x - width * m_scale / 2;
    const float gw = float(f.cellWidth) * k, gh = float(f.cellHeight) * k, y = at.y - gh / 2;
    const int perSheet = std::max(1, f.sheetColumns * f.sheetRows);
    const auto scaleAlpha = [&](Color c) { return (c & ~0xFFu) | uint32_t(float(c & 255) * style.alpha + 0.5f); };
    if (style.outline) {
        // The glyphs' silhouette in black at eight offsets around the fill, 2.2% of the text size
        // (3.5% read as too heavy next to the game's own text, 2026-10-29).
        TextStyle sil = style;
        sil.outline = false;
        sil.black = kClear;
        sil.white = kBlack;
        sil.top = sil.bottom = kWhite;
        const float o = size * 0.022f;
        for (const auto [dx, dy] : {std::pair{-1, -1}, std::pair{0, -1}, std::pair{1, -1}, std::pair{-1, 0},
                                    std::pair{1, 0}, std::pair{-1, 1}, std::pair{0, 1}, std::pair{1, 1}}) {
            Text(text, cx + float(dx) * o, cy + float(dy) * o, size, sil);
        }
    }
    const Color top = scaleAlpha(style.top), bottom = scaleAlpha(style.bottom);
    for (uint32_t cp : Decode(text)) {
        const art::Font::Glyph* g = f.Find(cp);
        if (g == nullptr) {
            pen += float(f.fallback.advance) * k;
            continue;
        }
        const int sheet = g->index / perSheet, cell = g->index % perSheet;
        if (sheet < int(f.sheets.size())) {
            const art::Image& img = f.sheets[size_t(sheet)];
            // Cells sit at a pitch of one texel more than their size.
            const float sx = float((cell % f.sheetColumns) * (f.cellWidth + 1));
            const float sy = float((cell / f.sheetColumns) * (f.cellHeight + 1));
            const ImTextureID tex = art::Tinted(img, style.black, style.white);
            const float x = pen + float(g->left) * k;
            Quad(tex, {x, y}, {x + gw, y + gh}, {sx / float(img.width), sy / float(img.height)},
                 {(sx + float(f.cellWidth)) / float(img.width), (sy + float(f.cellHeight)) / float(img.height)}, top,
                 bottom);
        }
        pen += float(g->advance) * k;
    }
    return width;
}

float Canvas::Label(std::string_view text, float cx, float cy, float size, Align align, bool shadow, Color color) const {
    if (shadow) {
        TextStyle s;
        s.white = kBlack;
        s.align = align;
        s.alpha = 80.0f / 255.0f;
        Text(text, cx + 4 * size / 32, cy - 3 * size / 32, size, s);
    }
    TextStyle s;
    s.white = color;
    s.align = align;
    s.outline = true;
    return Text(text, cx, cy, size, s);
}

float Canvas::Mario(std::string_view text, float cx, float cy, float size, Color top, Color bottom, Align align) const {
    TextStyle s;
    s.font = &art::MarioFont();
    s.top = top;
    s.bottom = bottom;
    s.align = align;
    return Text(text, cx, cy, size, s);
}

ImVec2 Canvas::Button(std::string_view text, float cx, float cy, float w, float h, bool on, float size, bool dim,
                      bool over) const {
    // The game's on / off / press animations: its scale (1.0 unselected .. 1.13 selected) and how far
    // the colours have moved towards the selected ones. A button seen for the first time starts
    // settled, so a screen doesn't open with every button animating.
    const float now = Now();
    auto [it, added] = g_buttons.try_emplace(ButtonKey(cx, cy, w));
    ButtonState& st = it->second;
    if (added) st.on = on;
    if (st.on != on) {
        st.on = on;
        st.since = now;
    }
    const float t = now - st.since;
    float scale = on ? Curve({K(0, 1.0f, 0.2f), K(3, 1.17f, -0.023f), K(5, 1.13f, 0)}, t)
                     : Curve({K(0, 1.13f, 0), K(3, 1.0f, 0)}, t);
    const float mix = on ? std::min(t / 5.0f, 1.0f) : 1.0f - std::min(t / 3.0f, 1.0f);
    if (const float p = now - st.pressed; on && p >= 0 && p < 6) {
        scale = Curve({K(0, 1.13f, 0), {2, 0.95f, -0.105f, 0.056f}, K(6, 1.13f, 0)}, p);
    }
    // 1.13 is the selected size; shrinkOthers draws that at 1.0 and the others smaller.
    const float k = scale * m_selectedScale / 1.13f, bw = w * k, bh = h * k;
    // Text wider than the button (a long controller name) is drawn smaller to fit inside it.
    float textSize = size * k;
    if (const float tw = text.empty() ? 0.0f : Measure(text, textSize); tw > bw - 8) textSize *= (bw - 8) / tw;
    Rect(cx, cy, bw, bh, Mix(kOffTop, kOnTop, mix), Mix(kOffBottom, kOnBottom, mix));
    if (!text.empty() && !over) Label(text, cx, cy, textSize, Align::Center, mix > 0.5f);
    // The frame in proportion to the button: W_SButton is 8 x 10 units larger than the game's
    // 53-unit-tall button with 24-unit corners. At full size on these smaller buttons its black
    // edge read about twice as thick as the game's.
    const float f = h / 53.0f;
    Window4(cx, cy, (w + 8 * f) * k, (h + 10 * f) * k, 24.0f * f * k);
    if (!text.empty() && over) Label(text, cx, cy, textSize, Align::Center, mix > 0.5f);
    if (dim) Rect(cx, cy, (w + 2) * k, (h + 4) * k, Rgb(0, 0, 0, 140), Rgb(0, 0, 0, 140));
    return ImVec2(bw, bh);
}

void Canvas::Cursor(float cx, float cy, float w, float h, float scale) const {
    const art::Image& img = art::Get(art::Art::Cursor);
    if (img.width == 0) return;
    const ImTextureID tex = art::Tinted(img, kUntinted0, kUntinted1);
    const ImTextureID shadow = art::Tinted(img, Rgb(0, 0, 0, 0), Rgb(0, 0, 0, 60));
    // loopCursor: every corner pulses on a 65-frame loop.
    const float loop = std::fmod(Now(), 65.0f);
    const float pulse = Curve({K(20, 1, 0), K(23, 1.1f, 0), K(30, 1, 0), K(40, 1, 0), K(48, 0.9f, 0), K(58, 1, 0)}, loop);
    const float s = std::round(36.0f * scale * pulse * m_scale);
    for (const auto [sx, sy] : {std::pair{-1, 1}, std::pair{1, 1}, std::pair{-1, -1}, std::pair{1, -1}}) {
        const ImVec2 uv0(sx > 0 ? 1.0f : 0.0f, sy < 0 ? 1.0f : 0.0f), uv1(1.0f - uv0.x, 1.0f - uv0.y);
        for (const auto [t, dy] : {std::pair{shadow, -5.0f}, std::pair{tex, 0.0f}}) {
            const ImVec2 p = Px(cx + float(sx) * w / 2, cy + float(sy) * h / 2 + dy);
            Quad(t, {p.x - s / 2, p.y - s / 2}, {p.x + s / 2, p.y + s / 2}, uv0, uv1, kWhite, kWhite);
        }
    }
}

void Canvas::PauseWindow(float cx, float cy, float w, float h, uint8_t fillAlpha) const {
    Rect(cx, cy, w - 12, h - 14, Rgb(0, 0, 0, fillAlpha), Rgb(0, 0, 0, fillAlpha));
    Window1(cx, cy, w, h, art::Art::PauseFrame, Rgb(50, 0, 0, 0), Rgb(230, 100, 100, 255));
}

void Canvas::Image(ImTextureID texture, float cx, float cy, float size) const {
    const ImVec2 p = Px(cx, cy);
    const float s = size * m_scale;
    Quad(texture, {p.x - s / 2, p.y - s / 2}, {p.x + s / 2, p.y + s / 2}, {0, 0}, {1, 1}, kWhite, kWhite);
}

} // namespace nsmbw_menu_draw
