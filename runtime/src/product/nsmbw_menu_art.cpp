// Reads the options menu's art straight from the extracted disc ([paths] dvd_root/files):
//   Layout/pauseMenu/pauseMenu.arc        window frame and button frame textures
//   Layout/select_cursor/select_cursor.arc the yellow corner cursor
//   EU/<lang>/Font/*.brfnt                the message font and the Mario font
// The formats, all big-endian:
//   .arc   U8 archive: a node table (directories and files) plus a string table. Some discs wrap
//          archives in Yaz0 compression; NSMBW's layout archives are plain U8, but both are handled.
//   .tpl   a list of GX textures. GX stores texels in small tiles (4x4 or 8x8 or 8x4 texels each,
//          depending on the format), so decoding walks tile by tile.
//   .brfnt NW4R font: FINF (metrics, pointers to the other blocks), TGLP (glyph sheets: one GX
//          texture per sheet, a grid of cells), CWDH (left / width / advance per glyph) and CMAP
//          (character code -> glyph index, as ranges, tables or code/index pairs).
// Layout offsets match the Python tools used to make the mockups (scratchpad arc_extract.py /
// render_kit.py), which were checked by rendering the stock pause menu and comparing it with a
// screenshot of the real one (2026-10-26).
#include "nsmbw_menu_art.h"

#include "runtime_config.h"

#include <aurora/gfx.h>
#include <aurora/imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <tuple>

namespace nsmbw_menu_art {
namespace fs = std::filesystem;

const Font::Glyph* Font::Find(uint32_t codepoint) const noexcept {
    const auto it = glyphs.find(codepoint);
    return it != glyphs.end() ? &it->second : nullptr;
}

namespace {

using Bytes = std::vector<uint8_t>;

uint16_t Be16(const Bytes& d, size_t o) { return o + 2 <= d.size() ? uint16_t(d[o] << 8 | d[o + 1]) : 0; }
uint32_t Be32(const Bytes& d, size_t o) {
    return o + 4 <= d.size() ? uint32_t(d[o]) << 24 | uint32_t(d[o + 1]) << 16 | uint32_t(d[o + 2]) << 8 | d[o + 3] : 0;
}

bool ReadFile(const fs::path& path, Bytes& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), {});
    return !out.empty();
}

Bytes Yaz0Decompress(const Bytes& d) {
    if (d.size() < 16 || std::memcmp(d.data(), "Yaz0", 4) != 0) return d;
    const uint32_t size = Be32(d, 4);
    Bytes out;
    out.reserve(size);
    size_t src = 16;
    while (out.size() < size && src < d.size()) {
        const uint8_t code = d[src++];
        for (int bit = 0; bit < 8 && out.size() < size && src < d.size(); ++bit) {
            if (code & (0x80 >> bit)) {
                out.push_back(d[src++]);
                continue;
            }
            if (src + 1 >= d.size()) return {};
            const uint8_t b1 = d[src], b2 = d[src + 1];
            src += 2;
            const size_t dist = ((b1 & 0xF) << 8 | b2) + 1;
            size_t n = b1 >> 4;
            n = n == 0 ? (src < d.size() ? d[src++] + 0x12 : 0) : n + 2;
            if (dist > out.size()) return {};
            for (size_t i = 0; i < n; ++i) out.push_back(out[out.size() - dist]);
        }
    }
    return out;
}

// U8 archive -> path inside the archive ("arc/timg/x.tpl") -> file bytes.
std::map<std::string, Bytes> ReadU8(const Bytes& d) {
    std::map<std::string, Bytes> files;
    if (Be32(d, 0) != 0x55AA382D) return files;
    const uint32_t root = Be32(d, 4);
    const uint32_t count = Be32(d, root + 8);
    const size_t strings = root + size_t(count) * 12;
    std::vector<std::pair<uint32_t, std::string>> dirs{{count, ""}}; // (end node, path)
    for (uint32_t i = 1; i < count; ++i) {
        while (dirs.size() > 1 && i >= dirs.back().first) dirs.pop_back();
        const size_t e = root + size_t(i) * 12;
        if (e + 12 > d.size()) break;
        const uint32_t nameOffset = Be32(d, e) & 0xFFFFFF;
        const uint32_t a = Be32(d, e + 4), b = Be32(d, e + 8);
        std::string name;
        for (size_t p = strings + nameOffset; p < d.size() && d[p] != 0; ++p) name += char(d[p]);
        const std::string path = dirs.back().second.empty() ? name : dirs.back().second + "/" + name;
        if (d[e] == 1) {
            dirs.emplace_back(b, path);
        } else if (size_t(a) + b <= d.size()) {
            files[path] = Bytes(d.begin() + a, d.begin() + a + b);
        }
    }
    return files;
}

uint32_t Rgb565(uint16_t v) {
    const uint32_t r = (v >> 11 & 31) * 255 / 31, g = (v >> 5 & 63) * 255 / 63, b = (v & 31) * 255 / 31;
    return r << 24 | g << 16 | b << 8 | 255;
}
uint32_t Rgb5a3(uint16_t v) {
    if (v & 0x8000) {
        return ((v >> 10 & 31) * 255 / 31) << 24 | ((v >> 5 & 31) * 255 / 31) << 16 | ((v & 31) * 255 / 31) << 8 | 255;
    }
    return ((v >> 8 & 15) * 17) << 24 | ((v >> 4 & 15) * 17) << 16 | ((v & 15) * 17) << 8 | ((v >> 12 & 7) * 255 / 7);
}

// Decodes one GX texture of `format` from `d` at `offset`. Formats: 0 I4, 1 I8, 2 IA4, 3 IA8,
// 4 RGB565, 5 RGB5A3, 6 RGBA8, 14 CMPR. Intensity formats repeat the intensity into all four
// channels (I4/I8) or into RGB (IA4/IA8), as the GX texture unit does.
bool DecodeGx(const Bytes& d, size_t offset, uint32_t format, int w, int h, Image& out) {
    if (offset < d.size()) {
        out.gx.assign(d.begin() + std::ptrdiff_t(offset), d.end());
        out.gxFormat = format;
    }
    int bw, bh;
    switch (format) {
    case 0: case 14: bw = 8; bh = 8; break;
    case 1: case 2: bw = 8; bh = 4; break;
    case 3: case 4: case 5: case 6: bw = 4; bh = 4; break;
    default: return false;
    }
    out.width = w;
    out.height = h;
    out.rgba.assign(size_t(w) * h * 4, 0);
    auto put = [&](int x, int y, uint32_t c) {
        if (x >= w || y >= h) return;
        uint8_t* p = &out.rgba[(size_t(y) * w + x) * 4];
        p[0] = uint8_t(c >> 24); p[1] = uint8_t(c >> 16); p[2] = uint8_t(c >> 8); p[3] = uint8_t(c);
    };
    size_t p = offset;
    for (int by = 0; by < h; by += bh) {
        for (int bx = 0; bx < w; bx += bw) {
            if (format == 6) { // RGBA8: a tile is 32 bytes of AR pairs, then 32 of GB pairs
                if (p + 64 > d.size()) return false;
                for (int i = 0; i < 16; ++i) {
                    const uint8_t a = d[p + i * 2], r = d[p + i * 2 + 1], g = d[p + 32 + i * 2], b = d[p + 33 + i * 2];
                    put(bx + i % 4, by + i / 4, uint32_t(r) << 24 | uint32_t(g) << 16 | uint32_t(b) << 8 | a);
                }
                p += 64;
                continue;
            }
            if (format == 14) { // CMPR: four 4x4 DXT1 sub-blocks
                for (int sub = 0; sub < 4; ++sub, p += 8) {
                    if (p + 8 > d.size()) return false;
                    const uint16_t c0 = Be16(d, p), c1 = Be16(d, p + 2);
                    uint32_t pal[4] = {Rgb565(c0), Rgb565(c1), 0, 0};
                    auto mix = [&](int a, int b, int wa, int wb, int div) {
                        uint32_t r = 255;
                        for (int s = 24; s >= 8; s -= 8) {
                            r |= (((pal[a] >> s & 255) * wa + (pal[b] >> s & 255) * wb) / div) << s;
                        }
                        return r;
                    };
                    if (c0 > c1) { pal[2] = mix(0, 1, 2, 1, 3); pal[3] = mix(0, 1, 1, 2, 3); }
                    else { pal[2] = mix(0, 1, 1, 1, 2); pal[3] = 0; }
                    const int sx = bx + (sub % 2) * 4, sy = by + (sub / 2) * 4;
                    for (int y = 0; y < 4; ++y)
                        for (int x = 0; x < 4; ++x) put(sx + x, sy + y, pal[d[p + 4 + y] >> (6 - 2 * x) & 3]);
                }
                continue;
            }
            for (int y = 0; y < bh; ++y) {
                for (int x = 0; x < bw; ++x) {
                    uint32_t c = 0;
                    switch (format) {
                    case 0: { // I4: two texels per byte, high nibble first
                        const size_t i = (size_t(y) * bw + x);
                        if (p + i / 2 >= d.size()) return false;
                        const uint8_t v = d[p + i / 2], n = (i % 2 == 0 ? v >> 4 : v & 15) * 17;
                        c = uint32_t(n) << 24 | uint32_t(n) << 16 | uint32_t(n) << 8 | n;
                        break;
                    }
                    case 1: { const uint8_t n = d[p + y * bw + x]; c = uint32_t(n) * 0x01010101u; break; }
                    case 2: {
                        const uint8_t v = d[p + y * bw + x], i = (v & 15) * 17, a = (v >> 4) * 17;
                        c = uint32_t(i) << 24 | uint32_t(i) << 16 | uint32_t(i) << 8 | a;
                        break;
                    }
                    case 3: {
                        const uint8_t a = d[p + (y * bw + x) * 2], i = d[p + (y * bw + x) * 2 + 1];
                        c = uint32_t(i) << 24 | uint32_t(i) << 16 | uint32_t(i) << 8 | a;
                        break;
                    }
                    case 4: c = Rgb565(Be16(d, p + (y * bw + x) * 2)); break;
                    case 5: c = Rgb5a3(Be16(d, p + (y * bw + x) * 2)); break;
                    }
                    put(bx + x, by + y, c);
                }
            }
            p += 32; // every other format's tile is 32 bytes (8x8 x 4 bit, 8x4 x 8 bit, 4x4 x 16 bit)
        }
    }
    return true;
}

// The first image of a TPL file.
bool DecodeTpl(const Bytes& d, Image& out) {
    if (Be32(d, 0) != 0x0020AF30 || Be32(d, 4) == 0) return false;
    const uint32_t header = Be32(d, Be32(d, 8));
    const int h = Be16(d, header), w = Be16(d, header + 2);
    return DecodeGx(d, Be32(d, header + 8), Be32(d, header + 4), w, h, out);
}

bool LoadBrfnt(const Bytes& d, Font& font) {
    const auto find = [&](const char* magic) -> size_t {
        for (size_t i = 0; i + 4 <= d.size(); i += 4) {
            if (std::memcmp(&d[i], magic, 4) == 0) return i;
        }
        return 0;
    };
    const size_t finf = find("FINF"), tglp = find("TGLP");
    if (finf == 0 || tglp == 0) return false;
    const size_t f = finf + 8;
    font.fallback = {0, int8_t(d[f + 4]), d[f + 5], int8_t(d[f + 6])};
    uint32_t widthPtr = Be32(d, f + 12), mapPtr = Be32(d, f + 16);
    font.height = d[f + 20];
    font.cellWidth = d[tglp + 8];
    font.cellHeight = d[tglp + 9];
    const uint32_t sheetSize = Be32(d, tglp + 12);
    const int sheetCount = Be16(d, tglp + 16);
    const uint32_t sheetFormat = Be16(d, tglp + 18) & 0x7FFF;
    font.sheetColumns = Be16(d, tglp + 20);
    font.sheetRows = Be16(d, tglp + 22);
    const int sheetW = Be16(d, tglp + 24), sheetH = Be16(d, tglp + 26);
    const uint32_t sheetData = Be32(d, tglp + 28);
    font.sheets.resize(size_t(sheetCount));
    for (int i = 0; i < sheetCount; ++i) {
        Image& sheet = font.sheets[size_t(i)];
        if (!DecodeGx(d, sheetData + size_t(i) * sheetSize, sheetFormat, sheetW, sheetH, sheet)) return false;
        if (sheet.gx.size() > sheetSize) sheet.gx.resize(sheetSize); // this sheet only, not the ones after it
    }
    std::map<uint16_t, Font::Glyph> widths;
    for (int guard = 0; widthPtr != 0 && widthPtr + 8 < d.size() && guard < 64; ++guard) {
        const uint16_t first = Be16(d, widthPtr), last = Be16(d, widthPtr + 2);
        for (uint32_t i = first; i <= last && widthPtr + 8 + (i - first) * 3 + 3 <= d.size(); ++i) {
            const size_t o = widthPtr + 8 + (i - first) * 3;
            widths[uint16_t(i)] = {uint16_t(i), int8_t(d[o]), d[o + 1], int8_t(d[o + 2])};
        }
        widthPtr = Be32(d, widthPtr + 4);
    }
    auto add = [&](uint32_t code, uint16_t index) {
        if (index == 0xFFFF) return;
        const auto it = widths.find(index);
        Font::Glyph g = it != widths.end() ? it->second : font.fallback;
        g.index = index;
        font.glyphs[code] = g;
    };
    for (int guard = 0; mapPtr != 0 && mapPtr + 12 < d.size() && guard < 256; ++guard) {
        const uint16_t first = Be16(d, mapPtr), last = Be16(d, mapPtr + 2), method = Be16(d, mapPtr + 4);
        const size_t m = mapPtr + 12;
        if (method == 0) { // a range mapped to consecutive glyphs
            for (uint32_t c = first; c <= last; ++c) add(c, uint16_t(c - first + Be16(d, m)));
        } else if (method == 1) { // a table, one glyph per code
            for (uint32_t c = first; c <= last; ++c) add(c, Be16(d, m + (c - first) * 2));
        } else { // code / glyph pairs
            const uint16_t n = Be16(d, m);
            for (uint16_t i = 0; i < n; ++i) add(Be16(d, m + 2 + i * 4), Be16(d, m + 4 + i * 4));
        }
        mapPtr = Be32(d, mapPtr + 8);
    }
    return !font.glyphs.empty();
}

std::array<Image, size_t(Art::Count)> g_art;
Font g_messageFont;
Font g_marioFont;
int g_state = 0; // 0 not tried, 1 loaded, -1 failed

bool LoadArchiveImage(const fs::path& arc, const char* name, Art art) {
    Bytes data;
    if (!ReadFile(arc, data)) {
        std::fprintf(stderr, "[nsmbw][menu] can't read %s\n", arc.string().c_str());
        return false;
    }
    for (const auto& [path, bytes] : ReadU8(Yaz0Decompress(data))) {
        if (path.size() >= std::strlen(name) && path.compare(path.size() - std::strlen(name), std::string::npos, name) == 0) {
            if (DecodeTpl(bytes, g_art[size_t(art)])) return true;
            break;
        }
    }
    std::fprintf(stderr, "[nsmbw][menu] %s: no usable %s\n", arc.string().c_str(), name);
    return false;
}

// The fonts are byte-identical in every EU language folder; English first, else any.
fs::path FindFontDir(const fs::path& files) {
    const fs::path english = files / "EU" / "EngEU" / "Font";
    if (fs::exists(english)) return english;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(files / "EU", ec)) {
        if (fs::exists(entry.path() / "Font")) return entry.path() / "Font";
    }
    return english;
}

bool LoadFont(const fs::path& path, Font& font) {
    Bytes data;
    if (ReadFile(path, data) && LoadBrfnt(data, font)) return true;
    std::fprintf(stderr, "[nsmbw][menu] can't load font %s\n", path.string().c_str());
    return false;
}

} // namespace

bool Load() noexcept {
    if (g_state != 0) return g_state > 0;
    const fs::path files = RuntimeConfigFile::ResolvedDvdRoot() / "files";
    const fs::path pause = files / "Layout" / "pauseMenu" / "pauseMenu.arc";
    const fs::path cursor = files / "Layout" / "select_cursor" / "select_cursor.arc";
    const fs::path fonts = FindFontDir(files);
    const bool ok = LoadArchiveImage(pause, "timg/im_kadomaruFlame_00.tpl", Art::PauseFrame) &&
                    LoadArchiveImage(pause, "timg/im_graButton_up_24_00.tpl", Art::ButtonUp) &&
                    LoadArchiveImage(pause, "timg/im_graButton_upRight_24_00.tpl", Art::ButtonUpRight) &&
                    LoadArchiveImage(pause, "timg/im_graButton_under_24_00.tpl", Art::ButtonUnder) &&
                    LoadArchiveImage(cursor, "timg/msk_cursol_00.tpl", Art::Cursor) &&
                    LoadFont(fonts / "mj2d00_MessageFont_32_I4.brfnt", g_messageFont) &&
                    LoadFont(fonts / "mj2d01_marioFont_64_IA4.brfnt", g_marioFont);
    g_state = ok ? 1 : -1;
    if (ok) {
        std::fprintf(stderr, "[nsmbw][menu] art loaded: %zu + %zu glyphs\n", g_messageFont.glyphs.size(),
                     g_marioFont.glyphs.size());
    }
    return ok;
}

const Image& Get(Art art) noexcept { return g_art[size_t(art)]; }
const Font& MessageFont() noexcept { return g_messageFont; }
const Font& MarioFont() noexcept { return g_marioFont; }

namespace {
struct Pixels {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
};

// The HD pack's version of an image, looked up once (empty when the pack is off or lacks it).
const Pixels& HdVersion(const Image& image) {
    static std::map<const Image*, Pixels> cache;
    if (const auto it = cache.find(&image); it != cache.end()) return it->second;
    Pixels& hd = cache[&image];
    if (!image.gx.empty()) {
        aurora_texture_replacement_rgba8(
            image.gx.data(), uint32_t(image.width), uint32_t(image.height), image.gxFormat,
            +[](void* user, uint32_t w, uint32_t h, const void* rgba) {
                auto* p = static_cast<Pixels*>(user);
                p->width = w;
                p->height = h;
                p->rgba.assign(static_cast<const uint8_t*>(rgba), static_cast<const uint8_t*>(rgba) + size_t(w) * h * 4);
            },
            &hd);
        std::fprintf(stderr, "[nsmbw][menu] %dx%d format %u: %s\n", image.width, image.height, image.gxFormat,
                     hd.rgba.empty() ? "no pack version" : ("pack " + std::to_string(hd.width) + "x" + std::to_string(hd.height)).c_str());
    }
    return hd;
}
} // namespace

ImTextureID Tinted(const Image& image, uint32_t black, uint32_t white) noexcept {
    static std::map<std::tuple<const Image*, uint32_t, uint32_t>, ImTextureID> cache;
    const auto key = std::make_tuple(&image, black, white);
    if (const auto it = cache.find(key); it != cache.end()) return it->second;
    const Pixels& hd = HdVersion(image);
    const bool useHd = !hd.rgba.empty();
    const std::vector<uint8_t>& src = useHd ? hd.rgba : image.rgba;
    const uint32_t w = useHd ? hd.width : uint32_t(image.width), h = useHd ? hd.height : uint32_t(image.height);
    std::vector<uint8_t> px(src.size());
    for (size_t i = 0; i + 3 < px.size(); i += 4) {
        for (int c = 0; c < 4; ++c) {
            const int lo = black >> (24 - c * 8) & 255, hi = white >> (24 - c * 8) & 255;
            px[i + c] = uint8_t(lo + (hi - lo) * src[i + c] / 255);
        }
    }
    const ImTextureID id = w > 0 ? aurora_imgui_add_texture(w, h, px.data()) : ImTextureID{};
    cache.emplace(key, id);
    return id;
}

} // namespace nsmbw_menu_art
