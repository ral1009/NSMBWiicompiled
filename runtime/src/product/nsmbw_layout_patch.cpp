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

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

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

std::optional<Bytes> ReadFile(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    return Bytes(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

bool Generate(const char* dvdPath, const char* outName, Bytes (*patch)(const Bytes&)) {
    const fs::path source = RuntimeDvd::HostPathFor(dvdPath);
    const auto original = source.empty() ? std::nullopt : ReadFile(source);
    if (!original) {
        std::fprintf(stderr, "[nsmbw][layout] %s not found; the Options button will be missing\n", dvdPath);
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

void GenerateFiles() {
    g_pauseMenuPatched = Generate("/Layout/pauseMenu/pauseMenu.arc", "pauseMenu.arc", &PatchPauseMenu);
    g_mapMenuPatched =
        Generate("/Layout/corseSelectMenu/corseSelectMenu.arc", "corseSelectMenu.arc", &PatchCourseSelectMenu);
}

} // namespace

void Register() noexcept { RuntimeDvd::SetGeneratedFilesHook(&GenerateFiles); }

} // namespace nsmbw_layout_patch

extern "C" bool NsmbwPauseMenuHasOptions() { return nsmbw_layout_patch::g_pauseMenuPatched; }
extern "C" bool NsmbwMapMenuHasOptions() { return nsmbw_layout_patch::g_mapMenuPatched; }
