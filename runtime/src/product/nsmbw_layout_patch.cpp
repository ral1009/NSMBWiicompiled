// See nsmbw_layout_patch.h.
//
// The in-course pause window (Pausewindow_c, layout pauseMenu_16) has two buttons, Continue and
// Exit, each a picture pane with its text, frame and shadow beneath it (P_SBBase_00 / _02), a group
// listing those four panes (B00_tuzukuButton / B02_menuButton), and per-pane tracks in the four
// button animations (onButton, idleButton, offButon, hitButton; their pat1 section names the
// groups they apply to, and each group gets its own AnimTransform when the game binds them).
// A third button is the same thing again: the Exit subtree cloned under new names (without
// P_shadowBlack, Exit's grey-out), a B03_optionButton group, and renamed copies of Exit's
// animation tracks. The clone takes Exit's place and Exit moves one button lower, so the order
// reads Continue / Options / Exit; the window and its background grow by one button (70 units,
// the spacing between the two originals) downwards.
//
// Formats (nw4r::lyt, as read by the game's own loader; all big-endian):
//   U8 archive: 0x20-byte header, 12-byte nodes (type, 24-bit name offset, data offset, size),
//     name strings, file data 32-byte aligned.
//   BRLYT: 16-byte header (magic, BOM, version, file size, header size, section count), then
//     sections (magic, size). A pane section has flags at +8, its name at +12 (16 bytes), the
//     translation at +36 and the size at +68; pas1 / pae1 open and close a pane's children, and
//     a usd1 directly after a pane is its user data. txt1 keeps its UTF-16 text at the offset in
//     +0x58, with the buffer and string byte counts (terminator included) at +0x4C / +0x4E.
//     grp1: name (16 bytes) at +8, pane count at +24, pane names (16 bytes each) from +28.
//   BRLAN: same header, then pat1 (group list: count at +10, groups offset at +16, 20 bytes per
//     group) and pai1 (entry count at +14, entry offset table at the offset in +16; each entry is
//     self-contained - name (20 bytes), then tags at offsets relative to the entry).
#include "nsmbw_layout_patch.h"

#include "dvd_generated_files.h"
#include "runtime_config.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

std::filesystem::path NsmbwDataDirPath(); // nsmbw_product.cpp: nsmbw_data next to the exe

namespace nsmbw_layout_patch {
namespace {
namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

uint32_t Get32(const Bytes& b, size_t o) { return uint32_t(b[o]) << 24 | uint32_t(b[o + 1]) << 16 | uint32_t(b[o + 2]) << 8 | b[o + 3]; }
uint16_t Get16(const Bytes& b, size_t o) { return uint16_t(b[o] << 8 | b[o + 1]); }
void Put32(Bytes& b, size_t o, uint32_t v) {
    b[o] = uint8_t(v >> 24); b[o + 1] = uint8_t(v >> 16); b[o + 2] = uint8_t(v >> 8); b[o + 3] = uint8_t(v);
}
void Put16(Bytes& b, size_t o, uint16_t v) { b[o] = uint8_t(v >> 8); b[o + 1] = uint8_t(v); }
float GetF(const Bytes& b, size_t o) { const uint32_t u = Get32(b, o); float f; std::memcpy(&f, &u, 4); return f; }
void PutF(Bytes& b, size_t o, float f) { uint32_t u; std::memcpy(&u, &f, 4); Put32(b, o, u); }
std::string Name(const Bytes& b, size_t o, size_t max) {
    std::string s;
    for (size_t i = 0; i < max && b[o + i] != 0; ++i) s += char(b[o + i]);
    return s;
}
void PutName(Bytes& b, size_t o, size_t max, const std::string& s) {
    for (size_t i = 0; i < max; ++i) b[o + i] = i < s.size() ? uint8_t(s[i]) : 0;
}

struct Failure {
    std::string what;
};
void Require(bool ok, const char* what) {
    if (!ok) throw Failure{what};
}

// ---- U8 archive ---------------------------------------------------------------------------------

struct Archive {
    Bytes header;                  // nodes and names, unchanged
    std::vector<std::string> path; // per node, "arc/blyt/x.brlyt" ("" for directories)
    std::vector<Bytes> data;       // per node (files only)
};

Archive ReadArchive(const Bytes& a) {
    Require(a.size() >= 0x20 && Get32(a, 0) == 0x55AA382Du, "not a U8 archive");
    const uint32_t root = Get32(a, 4), headerSize = Get32(a, 8);
    Archive out;
    out.header.assign(a.begin() + root, a.begin() + root + headerSize);
    const uint32_t count = Get32(a, root + 8), names = root + count * 12;
    out.path.resize(count);
    out.data.resize(count);
    std::vector<std::pair<uint32_t, std::string>> dirs; // (end node, path prefix)
    for (uint32_t i = 0; i < count; ++i) {
        while (!dirs.empty() && i >= dirs.back().first) dirs.pop_back();
        const size_t n = root + i * 12;
        const std::string name = Name(a, names + (Get32(a, n) & 0xFFFFFFu), 256);
        const std::string prefix = dirs.empty() ? "" : dirs.back().second;
        if (a[n] == 1) {
            dirs.push_back({Get32(a, n + 8), i == 0 ? "" : prefix + name + "/"});
        } else {
            out.path[i] = prefix + name;
            const uint32_t off = Get32(a, n + 4), size = Get32(a, n + 8);
            Require(off + size <= a.size(), "archive file out of range");
            out.data[i].assign(a.begin() + off, a.begin() + off + size);
        }
    }
    return out;
}

Bytes WriteArchive(const Archive& arc) {
    Bytes header = arc.header;
    const uint32_t dataStart = (0x20 + uint32_t(header.size()) + 31) & ~31u;
    Bytes out(dataStart, 0);
    uint32_t pos = dataStart;
    for (size_t i = 0; i < arc.path.size(); ++i) {
        if (arc.path[i].empty()) continue;
        Put32(header, i * 12 + 4, pos);
        Put32(header, i * 12 + 8, uint32_t(arc.data[i].size()));
        out.insert(out.end(), arc.data[i].begin(), arc.data[i].end());
        pos += uint32_t(arc.data[i].size());
        const uint32_t aligned = (pos + 31) & ~31u;
        out.resize(aligned, 0);
        pos = aligned;
    }
    Put32(out, 0, 0x55AA382Du);
    Put32(out, 4, 0x20);
    Put32(out, 8, uint32_t(header.size()));
    Put32(out, 12, dataStart);
    std::copy(header.begin(), header.end(), out.begin() + 0x20);
    return out;
}

Bytes& File(Archive& arc, const std::string& path) {
    for (size_t i = 0; i < arc.path.size(); ++i) {
        if (arc.path[i] == path) return arc.data[i];
    }
    throw Failure{"missing " + path};
}

// ---- Sectioned files (BRLYT / BRLAN) ------------------------------------------------------------

struct Sectioned {
    Bytes header; // 16 bytes
    std::vector<Bytes> sections;
};

Sectioned ReadSectioned(const Bytes& f) {
    Sectioned s;
    s.header.assign(f.begin(), f.begin() + 16);
    size_t p = Get16(f, 12);
    for (uint16_t i = 0, n = Get16(f, 14); i < n; ++i) {
        const uint32_t size = Get32(f, p + 4);
        Require(size >= 8 && p + size <= f.size(), "bad section");
        s.sections.emplace_back(f.begin() + p, f.begin() + p + size);
        p += size;
    }
    return s;
}

Bytes WriteSectioned(const Sectioned& s) {
    Bytes out = s.header;
    for (const Bytes& sec : s.sections) out.insert(out.end(), sec.begin(), sec.end());
    Put32(out, 8, uint32_t(out.size()));
    Put16(out, 12, 16);
    Put16(out, 14, uint16_t(s.sections.size()));
    return out;
}

std::string Magic(const Bytes& sec) { return std::string(sec.begin(), sec.begin() + 4); }
bool IsPane(const Bytes& sec) {
    const std::string m = Magic(sec);
    return m == "pan1" || m == "pic1" || m == "txt1" || m == "wnd1" || m == "bnd1";
}
std::string PaneName(const Bytes& sec) { return Name(sec, 12, 16); }

// Section index of a pane, and one past its subtree (user data and children included).
std::pair<size_t, size_t> FindSubtree(const Sectioned& l, const std::string& pane) {
    for (size_t i = 0; i < l.sections.size(); ++i) {
        if (!IsPane(l.sections[i]) || PaneName(l.sections[i]) != pane) continue;
        size_t j = i + 1;
        if (j < l.sections.size() && Magic(l.sections[j]) == "usd1") ++j;
        if (j < l.sections.size() && Magic(l.sections[j]) == "pas1") {
            int depth = 0;
            do {
                const std::string m = Magic(l.sections[j]);
                if (m == "pas1") ++depth;
                if (m == "pae1") --depth;
                ++j;
            } while (depth > 0 && j < l.sections.size());
        }
        return {i, j};
    }
    throw Failure{"missing pane " + pane};
}

Bytes& Pane(Sectioned& l, const std::string& pane) { return l.sections[FindSubtree(l, pane).first]; }

// Copies a pane's subtree right after it. Panes are renamed through `names`; a pane mapped to ""
// is left out with its own subtree.
void CloneSubtree(Sectioned& l, const std::string& pane, const std::map<std::string, std::string>& names) {
    const auto [begin, end] = FindSubtree(l, pane);
    std::vector<Bytes> copy;
    for (size_t i = begin; i < end; ++i) {
        const Bytes& sec = l.sections[i];
        if (IsPane(sec)) {
            const auto it = names.find(PaneName(sec));
            Require(it != names.end(), "clone: pane without a new name");
            if (it->second.empty()) {
                i = FindSubtree(l, PaneName(sec)).second - 1;
                continue;
            }
            Bytes renamed = sec;
            PutName(renamed, 12, 16, it->second);
            copy.push_back(std::move(renamed));
        } else {
            copy.push_back(sec);
        }
    }
    l.sections.insert(l.sections.begin() + std::ptrdiff_t(end), copy.begin(), copy.end());
}

void MovePane(Sectioned& l, const std::string& pane, float y) { PutF(Pane(l, pane), 40, y); }
void GrowPane(Sectioned& l, const std::string& pane, float extraHeight, float dy) {
    Bytes& sec = Pane(l, pane);
    PutF(sec, 72, GetF(sec, 72) + extraHeight);
    PutF(sec, 40, GetF(sec, 40) + dy);
}

void SetText(Sectioned& l, const std::string& pane, const std::string& text) {
    Bytes& sec = Pane(l, pane);
    Require(Magic(sec) == "txt1", "SetText on a non-text pane");
    const uint32_t textOffset = Get32(sec, 0x58);
    Bytes out(sec.begin(), sec.begin() + textOffset);
    for (char c : text) {
        out.push_back(0);
        out.push_back(uint8_t(c));
    }
    out.push_back(0);
    out.push_back(0);
    const uint16_t bytes = uint16_t((text.size() + 1) * 2);
    Put16(out, 0x4C, bytes);
    Put16(out, 0x4E, bytes);
    out.resize((out.size() + 3) & ~size_t(3), 0);
    Put32(out, 4, uint32_t(out.size()));
    sec = std::move(out);
}

void AddGroup(Sectioned& l, const std::string& name, const std::vector<std::string>& panes) {
    // Groups sit between grs1 and gre1 under RootGroup; a new one goes just before the last gre1.
    size_t at = l.sections.size();
    while (at > 0 && Magic(l.sections[at - 1]) != "gre1") --at;
    Require(at > 0, "layout has no group list");
    Bytes sec(28 + 16 * panes.size(), 0);
    std::memcpy(sec.data(), "grp1", 4);
    Put32(sec, 4, uint32_t(sec.size()));
    PutName(sec, 8, 16, name);
    Put16(sec, 24, uint16_t(panes.size()));
    for (size_t i = 0; i < panes.size(); ++i) PutName(sec, 28 + 16 * i, 16, panes[i]);
    l.sections.insert(l.sections.begin() + std::ptrdiff_t(at - 1), std::move(sec));
}

// BRLAN: adds `group` to pat1 and, in pai1, a renamed copy of every entry named in `names`.
void ExtendAnimation(Sectioned& a, const std::string& group, const std::map<std::string, std::string>& names) {
    for (Bytes& sec : a.sections) {
        if (Magic(sec) == "pat1") {
            const uint16_t count = Get16(sec, 10);
            const uint32_t groups = Get32(sec, 16);
            Require(groups + 20u * count <= sec.size(), "bad pat1");
            Bytes out(sec.begin(), sec.begin() + groups + 20 * count);
            Bytes entry(20, 0);
            PutName(entry, 0, 16, group);
            out.insert(out.end(), entry.begin(), entry.end());
            Put16(out, 10, uint16_t(count + 1));
            Put32(out, 4, uint32_t(out.size()));
            sec = std::move(out);
        } else if (Magic(sec) == "pai1") {
            const uint16_t count = Get16(sec, 14);
            const uint32_t table = Get32(sec, 16);
            std::vector<Bytes> entries;
            for (uint16_t i = 0; i < count; ++i) {
                const uint32_t start = Get32(sec, table + 4 * i);
                const uint32_t end = i + 1 < count ? Get32(sec, table + 4 * (i + 1)) : uint32_t(sec.size());
                entries.emplace_back(sec.begin() + start, sec.begin() + end);
            }
            for (uint16_t i = 0; i < count; ++i) {
                const auto it = names.find(Name(entries[i], 0, 20));
                if (it == names.end()) continue;
                Bytes copy = entries[i];
                PutName(copy, 0, 20, it->second);
                entries.push_back(std::move(copy));
            }
            // Header and texture names stay; the offset table and entries are rewritten.
            Bytes out(sec.begin(), sec.begin() + table);
            const uint32_t first = table + 4 * uint32_t(entries.size());
            out.resize(first, 0);
            for (const Bytes& e : entries) {
                Put32(out, table + 4 * (&e - entries.data()), uint32_t(out.size()));
                out.insert(out.end(), e.begin(), e.end());
            }
            Put16(out, 14, uint16_t(entries.size()));
            Put32(out, 4, uint32_t(out.size()));
            sec = std::move(out);
        }
    }
}

// ---- Adding a file, a texture and a material -----------------------------------------------------

// Inserts a file node as the last entry of directory `dir` ("arc/timg"). U8 nodes are one flat list:
// a directory node holds its parent's index and the index one past its last descendant, so every
// directory index and end at or after the insertion point moves up by one.
void AddArchiveFile(Archive& arc, const std::string& dir, const std::string& name, Bytes data) {
    const uint32_t count = Get32(arc.header, 8);
    Bytes nodes(arc.header.begin(), arc.header.begin() + count * 12);
    Bytes names(arc.header.begin() + count * 12, arc.header.end());
    // Find the directory: its path is the common prefix of its files.
    uint32_t at = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (nodes[i * 12] == 1 && i > 0) {
            const uint32_t end = Get32(nodes, i * 12 + 8);
            for (uint32_t j = i + 1; j < end; ++j) {
                if (arc.path[j].rfind(dir + "/", 0) == 0) at = end;
            }
        }
    }
    Require(at != 0, "archive directory not found");
    for (uint32_t i = 0; i < count; ++i) {
        if (nodes[i * 12] != 1) continue;
        if (i > 0 && Get32(nodes, i * 12 + 4) >= at) Put32(nodes, i * 12 + 4, Get32(nodes, i * 12 + 4) + 1);
        if (Get32(nodes, i * 12 + 8) >= at) Put32(nodes, i * 12 + 8, Get32(nodes, i * 12 + 8) + 1);
    }
    Bytes node(12, 0);
    Put32(node, 0, uint32_t(names.size())); // type 0 (file) in the top byte, name offset below
    nodes.insert(nodes.begin() + at * 12, node.begin(), node.end());
    names.insert(names.end(), name.begin(), name.end());
    names.push_back(0);
    arc.header = nodes;
    arc.header.insert(arc.header.end(), names.begin(), names.end());
    arc.path.insert(arc.path.begin() + at, dir + "/" + name);
    arc.data.insert(arc.data.begin() + at, std::move(data));
}

Bytes& Section(Sectioned& l, const char* magic) {
    for (Bytes& sec : l.sections) {
        if (Magic(sec) == magic) return sec;
    }
    throw Failure{std::string("missing section ") + magic};
}

// txl1: count at +8, 8-byte entries from +12 (name offset relative to +12, then 0), then names.
// Returns the new texture's index.
uint16_t AddTexture(Sectioned& l, const std::string& name) {
    Bytes& sec = Section(l, "txl1");
    const uint16_t count = Get16(sec, 8);
    std::vector<std::string> list;
    for (uint16_t i = 0; i < count; ++i) list.push_back(Name(sec, 12 + Get32(sec, 12 + 8 * i), 256));
    list.push_back(name);
    Bytes out(12 + 8 * list.size(), 0);
    std::memcpy(out.data(), "txl1", 4);
    Put16(out, 8, uint16_t(list.size()));
    for (size_t i = 0; i < list.size(); ++i) {
        Put32(out, 12 + 8 * i, uint32_t(out.size() - 12));
        out.insert(out.end(), list[i].begin(), list[i].end());
        out.push_back(0);
    }
    out.resize((out.size() + 3) & ~size_t(3), 0);
    Put32(out, 4, uint32_t(out.size()));
    sec = std::move(out);
    return uint16_t(list.size() - 1);
}

// mat1: count at +8, offset table (from the section start) at +12. Appends a copy of material
// `from` under `name`, with its first texture map pointing at `texture`. Returns its index.
uint16_t CloneMaterial(Sectioned& l, const std::string& fromName, const std::string& name, uint16_t texture) {
    Bytes& sec = Section(l, "mat1");
    const uint16_t count = Get16(sec, 8);
    std::vector<Bytes> mats;
    for (uint16_t i = 0; i < count; ++i) {
        const uint32_t start = Get32(sec, 12 + 4 * i);
        const uint32_t end = i + 1 < count ? Get32(sec, 12 + 4 * (i + 1)) : uint32_t(sec.size());
        mats.emplace_back(sec.begin() + start, sec.begin() + end);
    }
    const Bytes* from = nullptr;
    for (const Bytes& m : mats) {
        if (Name(m, 0, 20) == fromName) from = &m;
    }
    Require(from != nullptr && from->size() >= 0x44 && (Get32(*from, 0x3C) & 0xF) >= 1, "material to clone");
    Bytes copy = *from;
    PutName(copy, 0, 20, name);
    Put16(copy, 0x40, texture); // first TexMap: texture index (then wrap S / T)
    mats.push_back(std::move(copy));
    Bytes out(12 + 4 * mats.size(), 0);
    std::memcpy(out.data(), "mat1", 4);
    Put16(out, 8, uint16_t(mats.size()));
    for (size_t i = 0; i < mats.size(); ++i) {
        Put32(out, 12 + 4 * i, uint32_t(out.size()));
        out.insert(out.end(), mats[i].begin(), mats[i].end());
    }
    Put32(out, 4, uint32_t(out.size()));
    sec = std::move(out);
    return uint16_t(mats.size() - 1);
}

// ---- Mario in the character selector -------------------------------------------------------------
//
// The Add/Drop Players selector (charaChangeSelectContents_05) has, on each of its two cards
// (N_CharaP_00 / _01), a picture pane per character it can offer: P_lui, P_kinoB, P_kinoY (150x150
// RGB5A3 portraits). Mario was never offered. This adds Im_plofileMario_00.tpl, a material per card
// cloned from Luigi's with that texture, and P_mario_00 / _01 cloned from P_lui_00 / _01 (same
// place and size, hidden like the others until the game's code shows one). No animation names a
// portrait pane (only PbuttonShine and the card nodes), so nothing else changes. The code that
// shows these panes is projects/nsmbw/native/nsmbw_p1_character.cpp.

std::optional<Bytes> ReadFile(const fs::path& path); // below

// A 150x150 RGB5A3 TPL with Mario: nsmbw_data/portraits/Im_plofileMario_00.tpl when present
// (projects/nsmbw/tools/make_portrait_tpl.py makes one from a PNG), otherwise the disc's own Mario
// head from the score screen (pointResult.arc, im_mario_00, 128x128) centred on a transparent card.
constexpr int kPortraitSide = 150;
constexpr size_t kPortraitPixels = size_t((kPortraitSide + 3) / 4) * ((kPortraitSide + 3) / 4) * 32;

std::optional<Bytes> FindArchiveFile(const Bytes& a, const std::string& name) {
    const Archive arc = ReadArchive(a);
    for (size_t i = 0; i < arc.path.size(); ++i) {
        const size_t slash = arc.path[i].rfind('/');
        if (arc.path[i].substr(slash == std::string::npos ? 0 : slash + 1) == name) return arc.data[i];
    }
    return std::nullopt;
}

// First image of a TPL: (pixel data offset, width, height, format), or nothing.
std::optional<std::array<uint32_t, 4>> TplImage(const Bytes& t) {
    if (t.size() < 0x40 || Get32(t, 0) != 0x0020AF30u || Get32(t, 4) == 0) return std::nullopt;
    const uint32_t header = Get32(t, Get32(t, 8));
    if (header + 12 > t.size()) return std::nullopt;
    return std::array<uint32_t, 4>{Get32(t, header + 8), Get16(t, header + 2), Get16(t, header), Get32(t, header + 4)};
}

size_t Rgb5a3Index(int x, int y, int w) {
    return (size_t(y / 4) * ((w + 3) / 4) + size_t(x / 4)) * 32 + size_t((y % 4) * 4 + (x % 4)) * 2;
}

std::optional<Bytes> MarioPortrait(const Bytes& lui) {
    const fs::path custom = ::NsmbwDataDirPath() / "portraits" / "Im_plofileMario_00.tpl";
    if (auto t = ReadFile(custom)) {
        const auto img = TplImage(*t);
        if (img && (*img)[1] == kPortraitSide && (*img)[2] == kPortraitSide && (*img)[3] == 5 &&
            (*img)[0] + kPortraitPixels <= t->size()) {
            std::fprintf(stderr, "[nsmbw][layout] selector: Mario portrait from %s\n", custom.string().c_str());
            return t;
        }
        std::fprintf(stderr, "[nsmbw][layout] selector: %s is not a 150x150 RGB5A3 TPL\n", custom.string().c_str());
    }
    // Disc fallback: Luigi's TPL as the container (same size and format), its pixels replaced.
    const auto lu = TplImage(lui);
    const fs::path pr = RuntimeDvd::HostPathFor("/Layout/pointResult/pointResult.arc");
    const auto prArc = pr.empty() ? std::nullopt : ReadFile(pr);
    const auto head = prArc ? FindArchiveFile(*prArc, "im_mario_00.tpl") : std::nullopt;
    const auto hi = head ? TplImage(*head) : std::nullopt;
    if (!lu || !hi || (*hi)[3] != 5 || (*hi)[1] > kPortraitSide || (*hi)[2] > kPortraitSide) return std::nullopt;
    Bytes out = lui;
    std::fill(out.begin() + (*lu)[0], out.begin() + (*lu)[0] + kPortraitPixels, uint8_t(0)); // transparent
    const int w = int((*hi)[1]), h = int((*hi)[2]);
    const int ox = (kPortraitSide - w) / 2, oy = kPortraitSide - h - 4; // centred, near the bottom
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t s = (*hi)[0] + Rgb5a3Index(x, y, w), d = (*lu)[0] + Rgb5a3Index(x + ox, y + oy, kPortraitSide);
            if (s + 1 < head->size()) {
                out[d] = (*head)[s];
                out[d + 1] = (*head)[s + 1];
            }
        }
    }
    std::fprintf(stderr, "[nsmbw][layout] selector: Mario portrait from the disc's score screen\n");
    return out;
}

Bytes PatchCharaContents(const Bytes& original) {
    Archive arc = ReadArchive(original);
    const auto mario = MarioPortrait(File(arc, "arc/timg/Im_plofileLuiji_00.tpl"));
    Require(mario.has_value(), "no Mario portrait");
    AddArchiveFile(arc, "arc/timg", "Im_plofileMario_00.tpl", *mario);
    Bytes& lytFile = File(arc, "arc/blyt/characterChangeSelectContents_05.brlyt");
    Sectioned lyt = ReadSectioned(lytFile);
    const uint16_t texture = AddTexture(lyt, "Im_plofileMario_00.tpl");
    for (const char* card : {"00", "01"}) {
        const std::string lui = std::string("P_lui_") + card, mar = std::string("P_mario_") + card;
        const uint16_t material = CloneMaterial(lyt, lui, mar, texture);
        CloneSubtree(lyt, lui, {{lui, mar}});
        Put16(Pane(lyt, mar), 0x5C, material); // pic1: material index
    }
    lytFile = WriteSectioned(lyt);
    return WriteArchive(arc);
}

// ---- The pause window ---------------------------------------------------------------------------

constexpr float kButtonStep = 70.0f; // P_SBBase_00 at y 33, P_SBBase_02 at -37

Bytes PatchPauseMenu(const Bytes& original) {
    Archive arc = ReadArchive(original);

    Bytes& lytFile = File(arc, "arc/blyt/pauseMenu_16.brlyt");
    Sectioned lyt = ReadSectioned(lytFile);
    CloneSubtree(lyt, "P_SBBase_02",
                 {{"P_SBBase_02", "P_SBBase_03"}, {"T_modoru_01", "T_option_01"}, {"T_modoru_00", "T_option_00"},
                  {"W_SButton_2", "W_SButton_3"}, {"P_shadow_02", "P_shadow_03"}, {"P_shadowBlack", ""}});
    // The clone keeps Exit's place; Exit moves one button down.
    MovePane(lyt, "P_SBBase_02", -37.0f - kButtonStep);
    GrowPane(lyt, "W_N_pauseMenu_00", kButtonStep, -kButtonStep / 2);
    GrowPane(lyt, "P_bg_00", kButtonStep, -kButtonStep / 2);
    // ...and the whole window back up by half that, so the taller window stays centred on screen.
    // N_pauseMenu_00 is only ever scaled by the animations (inWindow / outWindow), never moved.
    MovePane(lyt, "N_pauseMenu_00", kButtonStep / 2);
    SetText(lyt, "T_option_00", "Options");
    SetText(lyt, "T_option_01", "Options");
    AddGroup(lyt, "B03_optionButton", {"P_shadow_03", "P_SBBase_03", "T_option_01", "T_option_00"});
    lytFile = WriteSectioned(lyt);

    for (const char* anim : {"onButton", "idleButton", "offButon", "hitButton"}) {
        Bytes& file = File(arc, std::string("arc/anim/pauseMenu_16_") + anim + ".brlan");
        Sectioned a = ReadSectioned(file);
        ExtendAnimation(a, "B03_optionButton",
                        {{"P_SBBase_02", "P_SBBase_03"}, {"P_shadow_02", "P_shadow_03"},
                         {"T_modoru_01", "T_option_01"}, {"T_modoru_00", "T_option_00"}});
        file = WriteSectioned(a);
    }
    return WriteArchive(arc);
}

// ---- The "1 + shake" bubble --------------------------------------------------------------------
//
// nsmbw_button_glyphs paints the Spin control over the bubble's remote frames, for every bubble
// that shows them. The hop comes from the panes, not the texture: these three animations key
// P_UpDownImage_00 and its after-image between y 56 and 44, which reads as a shake for a wiggling
// remote and as a jitter for a button. Only this variant uses these three files, and only they make
// T_guide_02 visible (alpha 255; every other animation keys it to 0), so all of this stays inside
// the "1 + spin" bubble. The plain bubble's own hop (loopUpdownOpposite) is left alone.

// Replaces every key of one of a pane's tracks with one value and a zero slope. `kind` is the tag:
// RLPA for its transform (target 0 = x, 1 = y), RLVC for its colours (target 16 = alpha).
// pai1: entry count at +14, offset table at the offset in +16. Entry: tag count at +20, tag offsets
// from +24 (relative to the entry). Tag: magic, group count at +4, group offsets from +8 (relative
// to the tag). Group: target at +1, key count at +4, key offset at +8 (relative to the group).
// A key is frame, value, slope, 4 bytes each.
void FlattenTrack(Sectioned& a, const std::string& pane, const std::string& kind, uint8_t target, float value) {
    bool found = false;
    for (Bytes& sec : a.sections) {
        if (Magic(sec) != "pai1") continue;
        const size_t table = Get32(sec, 16);
        for (uint16_t e = 0, entries = Get16(sec, 14); e < entries; ++e) {
            const size_t entry = Get32(sec, table + e * 4u);
            if (Name(sec, entry, 20) != pane) continue;
            for (uint8_t t = 0, tags = sec[entry + 20]; t < tags; ++t) {
                const size_t tag = entry + Get32(sec, entry + 24 + t * 4u);
                if (std::string(sec.begin() + tag, sec.begin() + tag + 4) != kind) continue;
                for (uint8_t g = 0, groups = sec[tag + 4]; g < groups; ++g) {
                    const size_t group = tag + Get32(sec, tag + 8 + g * 4u);
                    if (sec[group + 1] != target) continue;
                    const size_t keys = group + Get32(sec, group + 8);
                    for (uint16_t k = 0, count = Get16(sec, group + 4); k < count; ++k) {
                        PutF(sec, keys + k * 12u + 4, value);
                        PutF(sec, keys + k * 12u + 8, 0.0f);
                    }
                    found = true;
                }
            }
        }
    }
    Require(found, "bubble animation track");
}

// T_guide_02 ("+") sits at (1, 32.1) with its child T_guide_01 (the 1 button) at (-26, -2). The group
// moves right so it stays centred over the player once the spin button comes closer, and the "+"
// moves up to the 1 button's line (the child moves down by as much, so the 1 button stays put). The
// spin pane's values put its glyph on that line with the same gap as the 1 button's. All measured
// from a 2000 px screenshot at 2.38 px per unit: 5.5 px, 6 px and 11 px off before these values.
constexpr float kPlusX = 10.0f;
constexpr float kPlusY = 34.4f;
constexpr float kOneButtonY = -4.3f;
constexpr float kSpinPaneX = 36.4f;
constexpr float kSpinPaneY = 54.5f;

Bytes PatchFukidashi(const Bytes& original) {
    Archive arc = ReadArchive(original);

    Bytes& lytFile = File(arc, "arc/blyt/fukidashiInfo_16.brlyt");
    Sectioned lyt = ReadSectioned(lytFile);
    PutF(Pane(lyt, "T_guide_02"), 36, kPlusX);
    MovePane(lyt, "T_guide_02", kPlusY);
    MovePane(lyt, "T_guide_01", kOneButtonY);
    lytFile = WriteSectioned(lyt);

    for (const char* anim : {"inFukidashi2", "loop1PlusShake", "outFukidashi2"}) {
        Bytes& file = File(arc, std::string("arc/anim/fukidashiInfo_16_") + anim + ".brlan");
        Sectioned a = ReadSectioned(file);
        for (const char* pane : {"P_UpDownImage_00", "P_afterImageUD"}) {
            FlattenTrack(a, pane, "RLPA", 0, kSpinPaneX);
            FlattenTrack(a, pane, "RLPA", 1, kSpinPaneY);
        }
        // The after-image pulses to alpha 128 on frames 3..10 of the loop. It trailed the moving
        // remote; on a still glyph it only darkens the edges on every pulse.
        FlattenTrack(a, "P_afterImageUD", "RLVC", 16, 0.0f);
        file = WriteSectioned(a);
    }
    return WriteArchive(arc);
}

// ---- The world-map menu ------------------------------------------------------------------------
//
// dCourseSelectMenu_c (layout corseSelectMenu_11, opened with + on the map): Star Coins, Add/Drop
// Players and Save / Quick Save as green buttons P_SBBase_01..03 65 units apart, then a divider
// (P_line_00) and Title Screen in orange (P_SBBase_04) on its own lower panel (P_winbg_00), all
// inside P_winbg_01. Options is a copy of Save placed after it, the divider and the Title Screen
// section move one button down and the window grows by one button downwards (P_winbg_01 moves down
// by half and its children are put back by the same half).
// The "Back" hint below the window (N_back, sliding in and out by its own translate-y keys) stays
// where the game has it, and the taller window moves up until it ends just above the hint
// (N_corseSelect_00 is only ever scaled by its animations, so moving it sticks). Developer's
// screenshots, 2026-10-29: the hint pushed down sat against the bottom of the screen; the window
// moved up by the whole extra height touched the top; scaling it to fit was turned down. So the
// window keeps 22 units above it and ends 5 units over the hint's top edge (-169.5).
constexpr float kMapButtonStep = 65.0f; // P_SBBase_01 at y 35, _02 at -29, _03 at -94

Bytes PatchCourseSelectMenu(const Bytes& original) {
    Archive arc = ReadArchive(original);
    Bytes& lytFile = File(arc, "arc/blyt/corseSelectMenu_11.brlyt");
    Sectioned lyt = ReadSectioned(lytFile);
    CloneSubtree(lyt, "P_SBBase_03",
                 {{"P_SBBase_03", "P_SBBase_05"}, {"T_corseSelectS03", "T_corseSelectS05"},
                  {"T_corseSelect_03", "T_corseSelect_06"}, {"W_SButton_03", "W_SButton_05"},
                  {"P_shadow_03", "P_shadow_05"}});
    const float half = kMapButtonStep / 2;
    MovePane(lyt, "P_SBBase_05", -94.0f - kMapButtonStep);
    MovePane(lyt, "P_SBBase_04", -181.0f - kMapButtonStep);
    GrowPane(lyt, "P_winbg_01", kMapButtonStep, -half);
    GrowPane(lyt, "W_corseSelect_01", kMapButtonStep, 0.0f);
    GrowPane(lyt, "N_corseSelect_01", 0.0f, half);
    GrowPane(lyt, "P_winbg_00", 0.0f, half - kMapButtonStep);
    GrowPane(lyt, "P_line_00", 0.0f, half - kMapButtonStep);
    // The grown panel's centre is `half` below the menu's origin and its edges (305 + 65) / 2 from
    // there: origin 53 puts the top at 205.5 (the layout's top is 228) and the bottom at -164.5.
    MovePane(lyt, "N_corseSelect_00", 53.0f);
    SetText(lyt, "T_corseSelectS05", "Options");
    SetText(lyt, "T_corseSelect_06", "Options");
    AddGroup(lyt, "B05_Button", {"P_SBBase_05", "T_corseSelectS05", "T_corseSelect_06", "P_shadow_05"});
    lytFile = WriteSectioned(lyt);

    for (const char* anim : {"onButton", "idleButton", "offButon", "hitButton"}) {
        Bytes& file = File(arc, std::string("arc/anim/corseSelectMenu_11_") + anim + ".brlan");
        Sectioned a = ReadSectioned(file);
        ExtendAnimation(a, "B05_Button",
                        {{"P_SBBase_03", "P_SBBase_05"}, {"T_corseSelectS03", "T_corseSelectS05"},
                         {"T_corseSelect_03", "T_corseSelect_06"}, {"P_shadow_03", "P_shadow_05"}});
        file = WriteSectioned(a);
    }
    return WriteArchive(arc);
}

// ---- Wide-screen backgrounds -------------------------------------------------------------------
//
// At ratios wider than 16:9 the game's 2D layouts are drawn 1:1 in canvas units for every pane
// under a "location adjust" pane (pane flag bit 2; nsmbw_product.cpp ApplyAspectSetting sets the
// adjust factor to 608 / canvas width). Menu backgrounds in those subtrees - black panels, shadows,
// stripes, skies, framed windows - are about 900 units wide, enough to pass the edges of the 832-unit
// 16:9 screen but not the 1092 units of 21:9: the developer's 21:9 screenshots (2026-11-07) showed
// the file select, player count and course intro backgrounds ending at ~80% of the width, with
// window borders visible. This widens every picture or window pane at least kWideMinWidth units
// wide in such a subtree to the canvas width plus a margin, keeping its centre where it was (the
// translation is moved by half the growth for left- or right-anchored panes). A window pane keeps
// its border art at the ends, so its border goes back off-screen; a picture's texture is spread
// over the wider pane, which is invisible for the flat and gradient fills and mild for the few
// scenery strips (the course intro's world bars, 950 -> ~1160 units at 21:9).
constexpr float kWideMinWidth = 790.0f;
constexpr float kWideMargin = 64.0f;
float g_wideCanvasWidth = 0.0f; // 0 = not wider than 16:9

int WidenBackgroundsIn(Sectioned& lyt) {
    int widened = 0;
    std::vector<bool> stack;  // per open pas1: is everything under it in an adjusted subtree?
    bool lastPaneAdjusted = false, inAdjusted = false;
    for (Bytes& sec : lyt.sections) {
        const std::string m = Magic(sec);
        if (m == "pas1") {
            stack.push_back(inAdjusted);
            inAdjusted = lastPaneAdjusted;
            continue;
        }
        if (m == "pae1") {
            if (!stack.empty()) {
                inAdjusted = stack.back();
                stack.pop_back();
            }
            continue;
        }
        if (!IsPane(sec)) continue;
        const bool adjusted = inAdjusted || (sec[8] & 0x04) != 0;
        lastPaneAdjusted = adjusted;
        if (!adjusted || (m != "pic1" && m != "wnd1")) continue;
        const float width = GetF(sec, 68);
        const float target = g_wideCanvasWidth + kWideMargin;
        if (width < kWideMinWidth || width >= target) continue;
        const float grow = target - width;
        const int originX = sec[9] % 3;  // 0 left, 1 centre, 2 right
        if (originX == 0) PutF(sec, 36, GetF(sec, 36) - grow / 2);
        if (originX == 2) PutF(sec, 36, GetF(sec, 36) + grow / 2);
        PutF(sec, 68, target);
        ++widened;
    }
    return widened;
}

Bytes WidenBackgrounds(const Bytes& original) {
    Archive arc = ReadArchive(original);
    int widened = 0;
    for (size_t i = 0; i < arc.path.size(); ++i) {
        const std::string& p = arc.path[i];
        if (p.size() < 6 || p.compare(p.size() - 6, 6, ".brlyt") != 0) continue;
        Sectioned lyt = ReadSectioned(arc.data[i]);
        const int n = WidenBackgroundsIn(lyt);
        if (n > 0) {
            arc.data[i] = WriteSectioned(lyt);
            widened += n;
        }
    }
    // Not an error: these layouts' wide panes sit outside the adjusted subtree, so the game already
    // stretches them across the whole screen.
    Require(widened > 0, "no adjusted background pane (already full width)");
    return WriteArchive(arc);
}

// Layouts with ~900-unit backgrounds under a location-adjust pane (a survey of every layout on the
// PAL disc, 2026-11-14). worldSelectMap has none adjusted (the whole layout stretches already).
constexpr const char* kWideBackgroundLayouts[] = {
    "MultiCourseSelect", "batteryInfo", "characterChange", "continue", "controllerInformation",
    "corseSelectUIGuide", "easyPairing", "errorInfo", "fileSelectBase", "fileSelectPlayer",
    "infoWindow", "letterWindow", "modelPlayGuide", "pointResultMulti", "preGame", "sequenceBG",
    "stockItem", "timeUp", "wipeDokan", "worldCollectionCoin", "yesnoWindow",
};

std::optional<Bytes> ReadFile(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    return Bytes(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

bool Generate(const char* dvdPath, const char* outName, Bytes (*patch)(const Bytes&)) {
    const fs::path source = RuntimeDvd::HostPathFor(dvdPath);
    const auto original = source.empty() ? std::nullopt : ReadFile(source);
    if (!original) {
        std::fprintf(stderr, "[nsmbw][layout] %s not found; served unpatched\n", dvdPath);
        return false;
    }
    try {
        const Bytes patched = patch(*original);
        const fs::path dir = RuntimeConfigFile::ApplicationDataDirectory() / "generated";
        std::error_code ec;
        fs::create_directories(dir, ec);
        const fs::path out = dir / outName;
        {
            std::ofstream f(out, std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(patched.data()), std::streamsize(patched.size()));
            if (!f) {
                std::fprintf(stderr, "[nsmbw][layout] cannot write %s\n", out.string().c_str());
                return false;
            }
        }
        return RuntimeDvd::ServeFile(dvdPath, out);
    } catch (const Failure& f) {
        // A disc whose layout differs from the one this was written for: keep the original.
        std::fprintf(stderr, "[nsmbw][layout] %s not patched: %s\n", dvdPath, f.what.c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[nsmbw][layout] %s not patched: %s\n", dvdPath, e.what());
    }
    return false;
}

bool g_pauseMenuPatched = false;
bool g_mapMenuPatched = false;
bool g_selectorHasMario = false;

// Test only: NSMBW_DEBUG_SWAP_AREAS=<stage, e.g. 03-02>[:<area, default 2>] serves that course with
// area 1 and that area swapped (the archive's "course1*" and "course2*" names exchanged; same length, so nothing moves),
// so a scripted run that enters the course starts in area 2. Added 2026-11-19 to reach World 3-2's
// underground pipe area for the ultrawide zone-edge checks.
char g_swapAreaB = '2'; // NSMBW_DEBUG_SWAP_AREAS=<stage>[:<area>[:<entrance>]] - the area swapped with area 1
int g_swapStartEntrance = -1; // and, optionally, that area's start entrance
Bytes SwapAreas12(const Bytes& in) {
    Bytes out = in;
    // Start entrance: byte 13 of the area's options block (course<N>.bin block 2; layout as in Reggie:
    // events u32 x2, wrap u16, time s16, credits u8, start entrance u8). Found through the U8 node
    // table before the names change.
    if (g_swapStartEntrance >= 0 && out.size() > 0x20) {
        const auto be32 = [&](size_t o) { return (uint32_t(out[o]) << 24) | (uint32_t(out[o + 1]) << 16) | (uint32_t(out[o + 2]) << 8) | out[o + 3]; };
        const uint32_t root = be32(4);
        const uint32_t count = be32(root + 8);
        const size_t strings = root + count * 12;
        const std::string want = std::string("course") + g_swapAreaB + ".bin";
        for (uint32_t i = 1; i < count; ++i) {
            const size_t node = root + i * 12;
            if (out[node] != 0) continue; // a directory
            const uint32_t nameOff = be32(node) & 0xFFFFFFu;
            if (want != reinterpret_cast<const char*>(&out[strings + nameOff])) continue;
            const uint32_t data = be32(node + 4);
            const uint32_t block2 = be32(data + 8);
            out[data + block2 + 13] = static_cast<uint8_t>(g_swapStartEntrance);
            std::fprintf(stderr, "[nsmbw][layout] test: %s start entrance -> %d%c", want.c_str(), g_swapStartEntrance, 10);
        }
    }
    static const char a[] = "course1";
    const size_t n = sizeof(a) - 1;
    for (size_t i = 0; i + n <= out.size(); ++i) {
        if (std::memcmp(&out[i], a, n - 1) != 0) continue;
        if (out[i + n - 1] == '1') { out[i + n - 1] = static_cast<uint8_t>(g_swapAreaB); i += n - 1; }
        else if (out[i + n - 1] == static_cast<uint8_t>(g_swapAreaB)) { out[i + n - 1] = '1'; i += n - 1; }
    }
    return out;
}

void GenerateFiles() {
    if (const char* stage = std::getenv("NSMBW_DEBUG_SWAP_AREAS"); stage != nullptr && *stage != 0) {
        std::string name = stage;
        if (const auto colon = name.find(':'); colon != std::string::npos) {
            g_swapAreaB = name.size() > colon + 1 ? name[colon + 1] : '2';
            if (const auto c2 = name.find(':', colon + 1); c2 != std::string::npos) g_swapStartEntrance = std::atoi(name.c_str() + c2 + 1);
            name.resize(colon);
        }
        const std::string dvd = std::string("/Stage/") + name + ".arc";
        const std::string out = std::string("areaswap_") + name + ".arc";
        std::fprintf(stderr, "[nsmbw][layout] test: %s areas 1/2 swapped: %s\n", dvd.c_str(),
                     Generate(dvd.c_str(), out.c_str(), &SwapAreas12) ? "served" : "failed");
    }
    g_pauseMenuPatched = Generate("/Layout/pauseMenu/pauseMenu.arc", "pauseMenu.arc", &PatchPauseMenu);
    g_mapMenuPatched =
        Generate("/Layout/corseSelectMenu/corseSelectMenu.arc", "corseSelectMenu.arc", &PatchCourseSelectMenu);
    g_selectorHasMario = Generate("/Layout/charaChangeSelectContents/charaChangeSelectContents.arc",
                                  "charaChangeSelectContents.arc", &PatchCharaContents);
    // The Wii Remote icon set keeps the game's own bubble: its hop belongs to the remote art.
    if (RuntimeConfigFile::ButtonIcons("auto") != "wii") {
        Generate("/Layout/fukidashiInfo/fukidashiInfo.arc", "fukidashiInfo.arc", &PatchFukidashi);
    }
    // Wider than 16:9: menu backgrounds widened to the canvas (see WidenBackgrounds). None of these
    // layouts is patched above, so each starts from the disc's file.
    const auto& aspect = RuntimeConfigFile::RunningAspect();
    if (!aspect.fourThree && aspect.num * 9 > aspect.den * 16) {
        g_wideCanvasWidth = float(832.0 * (double(aspect.num) / aspect.den) / (16.0 / 9.0));
        int served = 0;
        for (const char* name : kWideBackgroundLayouts) {
            const std::string dvd = std::string("/Layout/") + name + "/" + name + ".arc";
            const std::string out = std::string("wide_") + name + ".arc";
            if (Generate(dvd.c_str(), out.c_str(), &WidenBackgrounds)) ++served;
        }
        std::fprintf(stderr, "[nsmbw][layout] wide backgrounds: %d of %zu layouts widened to %.0f units\n", served,
                     std::size(kWideBackgroundLayouts), g_wideCanvasWidth + kWideMargin);
    }
}

} // namespace

void Register() noexcept { RuntimeDvd::SetGeneratedFilesHook(&GenerateFiles); }

} // namespace nsmbw_layout_patch

extern "C" bool NsmbwPauseMenuHasOptions() { return nsmbw_layout_patch::g_pauseMenuPatched; }
extern "C" bool NsmbwMapMenuHasOptions() { return nsmbw_layout_patch::g_mapMenuPatched; }
// The selector layout has P_mario_00 / _01 (nsmbw_p1_character.cpp offers Mario only then).
extern "C" bool NsmbwSelectorHasMario() { return nsmbw_layout_patch::g_selectorHasMario; }
