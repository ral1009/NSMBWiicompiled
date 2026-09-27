// Controller button glyphs for NSMBW's in-text button icons.
// (Generated from projects/nsmbw/tools/nsmbw_button_glyphs.cpp.in by gen_button_glyphs.py -
// edit the template, not this file.)
//
// NSMBW draws button prompts ("Press (2) to Start", the (1)/(2) in menus and course messages) as
// characters of its picture font, mj2d00_PictureFont_32_RGBA8.brfnt (byte-identical in every EU
// language folder). Parsing its TGLP/CMAP blocks: two 256x256 GX_TF_RGB5A3 sheets, 7x7 cells of
// 32x32 texels at a 33-texel pitch (32 + 1 spacing, measured from the decoded sheet), glyph index
// = character - 0x20. Sheet 0 holds the Wii Remote icons:
//   '!' d-pad   '"' A   '#' B   '$' HOME   '%' +   '&' -   '\'' 1   '(' 2
//   ')' Nunchuk stick   '*' C   '+' Z   ',' '-' Remote (sideways / upright)
// Its Dolphin texture-pack name is tex1_256x256_7b53229a3b86fede_5, so aurora can identify it by
// that hash with or without a pack installed. Two layout textures carry standalone Wii icons too
// (the "2" on the "Hold the Wii Remote sideways" screen, and a d-pad); those are repainted whole.
// Other screens may use more such textures - a Wii icon that stays unchanged under a controller
// set is one of those, found with texture dumps.
//
// Each Wii icon is repainted with the glyph of the physical control that currently produces that
// Wii button, in the current context (nsmbw_controls.cpp: in a course, 2 is "Jump"; in menus it is
// "Confirm", possibly on a different button):
//   Wii button <- action in the current context <- first bound pad control / key
//              -> <set>/<control>.png, embedded below.
// The set follows the last-used device (keyboard, or SDL_GetGamepadType of the pad), unless the
// F10 "Button icons" choice pins one ([controller] button_icons in Config.toml). Cells whose
// control has no glyph keep the game's own Wii icon; HOME, Nunchuk and Remote cells are left alone.
//
// Art: projects/nsmbw/assets/button_glyphs/, built from Kenney's CC0 "Input Prompts" by
// projects/nsmbw/tools/import_kenney_glyphs.py.

#include "nsmbw_button_glyphs.h"
#include "nsmbw_controls.h"

#include "runtime_config.h"

#include <aurora/gfx.h>
#include <dolphin/pad.h>
#include <imgui.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace nsmbw_button_glyphs {
namespace {

enum class GlyphSet { Wii, Xbox, PlayStation, Switch, Keyboard };

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
constexpr unsigned char kXbox_back[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/xbox/back.png"
};
constexpr unsigned char kXbox_dpad[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/xbox/dpad.png"
};
constexpr unsigned char kXbox_east[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/xbox/east.png"
};
constexpr unsigned char kXbox_guide[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/xbox/guide.png"
};
constexpr unsigned char kXbox_left_shoulder[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/xbox/left_shoulder.png"
};
constexpr unsigned char kXbox_left_stick[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/xbox/left_stick.png"
};
constexpr unsigned char kXbox_left_trigger[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/xbox/left_trigger.png"
};
constexpr unsigned char kXbox_lstick[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/xbox/lstick.png"
};
constexpr unsigned char kXbox_north[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/xbox/north.png"
};
constexpr unsigned char kXbox_right_shoulder[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/xbox/right_shoulder.png"
};
constexpr unsigned char kXbox_right_stick[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/xbox/right_stick.png"
};
constexpr unsigned char kXbox_right_trigger[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/xbox/right_trigger.png"
};
constexpr unsigned char kXbox_rstick[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/xbox/rstick.png"
};
constexpr unsigned char kXbox_south[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/xbox/south.png"
};
constexpr unsigned char kXbox_start[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/xbox/start.png"
};
constexpr unsigned char kXbox_west[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/xbox/west.png"
};
constexpr unsigned char kPlayStation_back[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/playstation/back.png"
};
constexpr unsigned char kPlayStation_dpad[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/playstation/dpad.png"
};
constexpr unsigned char kPlayStation_east[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/playstation/east.png"
};
constexpr unsigned char kPlayStation_left_shoulder[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/playstation/left_shoulder.png"
};
constexpr unsigned char kPlayStation_left_stick[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/playstation/left_stick.png"
};
constexpr unsigned char kPlayStation_left_trigger[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/playstation/left_trigger.png"
};
constexpr unsigned char kPlayStation_lstick[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/playstation/lstick.png"
};
constexpr unsigned char kPlayStation_north[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/playstation/north.png"
};
constexpr unsigned char kPlayStation_right_shoulder[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/playstation/right_shoulder.png"
};
constexpr unsigned char kPlayStation_right_stick[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/playstation/right_stick.png"
};
constexpr unsigned char kPlayStation_right_trigger[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/playstation/right_trigger.png"
};
constexpr unsigned char kPlayStation_rstick[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/playstation/rstick.png"
};
constexpr unsigned char kPlayStation_south[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/playstation/south.png"
};
constexpr unsigned char kPlayStation_start[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/playstation/start.png"
};
constexpr unsigned char kPlayStation_west[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/playstation/west.png"
};
constexpr unsigned char kSwitch_back[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/switch/back.png"
};
constexpr unsigned char kSwitch_dpad[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/switch/dpad.png"
};
constexpr unsigned char kSwitch_east[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/switch/east.png"
};
constexpr unsigned char kSwitch_guide[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/switch/guide.png"
};
constexpr unsigned char kSwitch_left_shoulder[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/switch/left_shoulder.png"
};
constexpr unsigned char kSwitch_left_stick[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/switch/left_stick.png"
};
constexpr unsigned char kSwitch_left_trigger[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/switch/left_trigger.png"
};
constexpr unsigned char kSwitch_lstick[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/switch/lstick.png"
};
constexpr unsigned char kSwitch_north[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/switch/north.png"
};
constexpr unsigned char kSwitch_right_shoulder[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/switch/right_shoulder.png"
};
constexpr unsigned char kSwitch_right_stick[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/switch/right_stick.png"
};
constexpr unsigned char kSwitch_right_trigger[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/switch/right_trigger.png"
};
constexpr unsigned char kSwitch_rstick[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/switch/rstick.png"
};
constexpr unsigned char kSwitch_south[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/switch/south.png"
};
constexpr unsigned char kSwitch_start[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/switch/start.png"
};
constexpr unsigned char kSwitch_west[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/switch/west.png"
};
constexpr unsigned char kKeyboard_0[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/0.png"
};
constexpr unsigned char kKeyboard_1[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/1.png"
};
constexpr unsigned char kKeyboard_2[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/2.png"
};
constexpr unsigned char kKeyboard_3[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/3.png"
};
constexpr unsigned char kKeyboard_4[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/4.png"
};
constexpr unsigned char kKeyboard_5[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/5.png"
};
constexpr unsigned char kKeyboard_6[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/6.png"
};
constexpr unsigned char kKeyboard_7[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/7.png"
};
constexpr unsigned char kKeyboard_8[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/8.png"
};
constexpr unsigned char kKeyboard_9[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/9.png"
};
constexpr unsigned char kKeyboard_a[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/a.png"
};
constexpr unsigned char kKeyboard_alt[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/alt.png"
};
constexpr unsigned char kKeyboard_apostrophe[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/apostrophe.png"
};
constexpr unsigned char kKeyboard_arrows[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/arrows.png"
};
constexpr unsigned char kKeyboard_b[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/b.png"
};
constexpr unsigned char kKeyboard_backslash[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/backslash.png"
};
constexpr unsigned char kKeyboard_backspace[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/backspace.png"
};
constexpr unsigned char kKeyboard_c[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/c.png"
};
constexpr unsigned char kKeyboard_comma[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/comma.png"
};
constexpr unsigned char kKeyboard_ctrl[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/ctrl.png"
};
constexpr unsigned char kKeyboard_d[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/d.png"
};
constexpr unsigned char kKeyboard_delete[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/delete.png"
};
constexpr unsigned char kKeyboard_down[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/down.png"
};
constexpr unsigned char kKeyboard_e[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/e.png"
};
constexpr unsigned char kKeyboard_end[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/end.png"
};
constexpr unsigned char kKeyboard_enter[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/enter.png"
};
constexpr unsigned char kKeyboard_equals[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/equals.png"
};
constexpr unsigned char kKeyboard_escape[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/escape.png"
};
constexpr unsigned char kKeyboard_f[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/f.png"
};
constexpr unsigned char kKeyboard_g[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/g.png"
};
constexpr unsigned char kKeyboard_grave[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/grave.png"
};
constexpr unsigned char kKeyboard_h[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/h.png"
};
constexpr unsigned char kKeyboard_home[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/home.png"
};
constexpr unsigned char kKeyboard_i[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/i.png"
};
constexpr unsigned char kKeyboard_insert[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/insert.png"
};
constexpr unsigned char kKeyboard_j[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/j.png"
};
constexpr unsigned char kKeyboard_k[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/k.png"
};
constexpr unsigned char kKeyboard_l[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/l.png"
};
constexpr unsigned char kKeyboard_left[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/left.png"
};
constexpr unsigned char kKeyboard_left_bracket[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/left_bracket.png"
};
constexpr unsigned char kKeyboard_m[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/m.png"
};
constexpr unsigned char kKeyboard_minus[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/minus.png"
};
constexpr unsigned char kKeyboard_n[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/n.png"
};
constexpr unsigned char kKeyboard_o[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/o.png"
};
constexpr unsigned char kKeyboard_p[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/p.png"
};
constexpr unsigned char kKeyboard_page_down[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/page_down.png"
};
constexpr unsigned char kKeyboard_page_up[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/page_up.png"
};
constexpr unsigned char kKeyboard_period[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/period.png"
};
constexpr unsigned char kKeyboard_q[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/q.png"
};
constexpr unsigned char kKeyboard_r[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/r.png"
};
constexpr unsigned char kKeyboard_right[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/right.png"
};
constexpr unsigned char kKeyboard_right_bracket[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/right_bracket.png"
};
constexpr unsigned char kKeyboard_s[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/s.png"
};
constexpr unsigned char kKeyboard_semicolon[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/semicolon.png"
};
constexpr unsigned char kKeyboard_shift[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/shift.png"
};
constexpr unsigned char kKeyboard_slash[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/slash.png"
};
constexpr unsigned char kKeyboard_space[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/space.png"
};
constexpr unsigned char kKeyboard_t[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/t.png"
};
constexpr unsigned char kKeyboard_tab[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/tab.png"
};
constexpr unsigned char kKeyboard_u[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/u.png"
};
constexpr unsigned char kKeyboard_up[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/up.png"
};
constexpr unsigned char kKeyboard_v[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/v.png"
};
constexpr unsigned char kKeyboard_w[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/w.png"
};
constexpr unsigned char kKeyboard_x[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/x.png"
};
constexpr unsigned char kKeyboard_y[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/y.png"
};
constexpr unsigned char kKeyboard_z[] = {
#embed "../../../projects/nsmbw/assets/button_glyphs/keyboard/z.png"
};
#pragma clang diagnostic pop

struct Glyph {
    GlyphSet set;
    const char* control;
    const unsigned char* png;
    size_t size;
};

constexpr Glyph kGlyphs[] = {
    {GlyphSet::Xbox, "back", kXbox_back, sizeof(kXbox_back)},
    {GlyphSet::Xbox, "dpad", kXbox_dpad, sizeof(kXbox_dpad)},
    {GlyphSet::Xbox, "east", kXbox_east, sizeof(kXbox_east)},
    {GlyphSet::Xbox, "guide", kXbox_guide, sizeof(kXbox_guide)},
    {GlyphSet::Xbox, "left_shoulder", kXbox_left_shoulder, sizeof(kXbox_left_shoulder)},
    {GlyphSet::Xbox, "left_stick", kXbox_left_stick, sizeof(kXbox_left_stick)},
    {GlyphSet::Xbox, "left_trigger", kXbox_left_trigger, sizeof(kXbox_left_trigger)},
    {GlyphSet::Xbox, "lstick", kXbox_lstick, sizeof(kXbox_lstick)},
    {GlyphSet::Xbox, "north", kXbox_north, sizeof(kXbox_north)},
    {GlyphSet::Xbox, "right_shoulder", kXbox_right_shoulder, sizeof(kXbox_right_shoulder)},
    {GlyphSet::Xbox, "right_stick", kXbox_right_stick, sizeof(kXbox_right_stick)},
    {GlyphSet::Xbox, "right_trigger", kXbox_right_trigger, sizeof(kXbox_right_trigger)},
    {GlyphSet::Xbox, "rstick", kXbox_rstick, sizeof(kXbox_rstick)},
    {GlyphSet::Xbox, "south", kXbox_south, sizeof(kXbox_south)},
    {GlyphSet::Xbox, "start", kXbox_start, sizeof(kXbox_start)},
    {GlyphSet::Xbox, "west", kXbox_west, sizeof(kXbox_west)},
    {GlyphSet::PlayStation, "back", kPlayStation_back, sizeof(kPlayStation_back)},
    {GlyphSet::PlayStation, "dpad", kPlayStation_dpad, sizeof(kPlayStation_dpad)},
    {GlyphSet::PlayStation, "east", kPlayStation_east, sizeof(kPlayStation_east)},
    {GlyphSet::PlayStation, "left_shoulder", kPlayStation_left_shoulder, sizeof(kPlayStation_left_shoulder)},
    {GlyphSet::PlayStation, "left_stick", kPlayStation_left_stick, sizeof(kPlayStation_left_stick)},
    {GlyphSet::PlayStation, "left_trigger", kPlayStation_left_trigger, sizeof(kPlayStation_left_trigger)},
    {GlyphSet::PlayStation, "lstick", kPlayStation_lstick, sizeof(kPlayStation_lstick)},
    {GlyphSet::PlayStation, "north", kPlayStation_north, sizeof(kPlayStation_north)},
    {GlyphSet::PlayStation, "right_shoulder", kPlayStation_right_shoulder, sizeof(kPlayStation_right_shoulder)},
    {GlyphSet::PlayStation, "right_stick", kPlayStation_right_stick, sizeof(kPlayStation_right_stick)},
    {GlyphSet::PlayStation, "right_trigger", kPlayStation_right_trigger, sizeof(kPlayStation_right_trigger)},
    {GlyphSet::PlayStation, "rstick", kPlayStation_rstick, sizeof(kPlayStation_rstick)},
    {GlyphSet::PlayStation, "south", kPlayStation_south, sizeof(kPlayStation_south)},
    {GlyphSet::PlayStation, "start", kPlayStation_start, sizeof(kPlayStation_start)},
    {GlyphSet::PlayStation, "west", kPlayStation_west, sizeof(kPlayStation_west)},
    {GlyphSet::Switch, "back", kSwitch_back, sizeof(kSwitch_back)},
    {GlyphSet::Switch, "dpad", kSwitch_dpad, sizeof(kSwitch_dpad)},
    {GlyphSet::Switch, "east", kSwitch_east, sizeof(kSwitch_east)},
    {GlyphSet::Switch, "guide", kSwitch_guide, sizeof(kSwitch_guide)},
    {GlyphSet::Switch, "left_shoulder", kSwitch_left_shoulder, sizeof(kSwitch_left_shoulder)},
    {GlyphSet::Switch, "left_stick", kSwitch_left_stick, sizeof(kSwitch_left_stick)},
    {GlyphSet::Switch, "left_trigger", kSwitch_left_trigger, sizeof(kSwitch_left_trigger)},
    {GlyphSet::Switch, "lstick", kSwitch_lstick, sizeof(kSwitch_lstick)},
    {GlyphSet::Switch, "north", kSwitch_north, sizeof(kSwitch_north)},
    {GlyphSet::Switch, "right_shoulder", kSwitch_right_shoulder, sizeof(kSwitch_right_shoulder)},
    {GlyphSet::Switch, "right_stick", kSwitch_right_stick, sizeof(kSwitch_right_stick)},
    {GlyphSet::Switch, "right_trigger", kSwitch_right_trigger, sizeof(kSwitch_right_trigger)},
    {GlyphSet::Switch, "rstick", kSwitch_rstick, sizeof(kSwitch_rstick)},
    {GlyphSet::Switch, "south", kSwitch_south, sizeof(kSwitch_south)},
    {GlyphSet::Switch, "start", kSwitch_start, sizeof(kSwitch_start)},
    {GlyphSet::Switch, "west", kSwitch_west, sizeof(kSwitch_west)},
    {GlyphSet::Keyboard, "0", kKeyboard_0, sizeof(kKeyboard_0)},
    {GlyphSet::Keyboard, "1", kKeyboard_1, sizeof(kKeyboard_1)},
    {GlyphSet::Keyboard, "2", kKeyboard_2, sizeof(kKeyboard_2)},
    {GlyphSet::Keyboard, "3", kKeyboard_3, sizeof(kKeyboard_3)},
    {GlyphSet::Keyboard, "4", kKeyboard_4, sizeof(kKeyboard_4)},
    {GlyphSet::Keyboard, "5", kKeyboard_5, sizeof(kKeyboard_5)},
    {GlyphSet::Keyboard, "6", kKeyboard_6, sizeof(kKeyboard_6)},
    {GlyphSet::Keyboard, "7", kKeyboard_7, sizeof(kKeyboard_7)},
    {GlyphSet::Keyboard, "8", kKeyboard_8, sizeof(kKeyboard_8)},
    {GlyphSet::Keyboard, "9", kKeyboard_9, sizeof(kKeyboard_9)},
    {GlyphSet::Keyboard, "a", kKeyboard_a, sizeof(kKeyboard_a)},
    {GlyphSet::Keyboard, "alt", kKeyboard_alt, sizeof(kKeyboard_alt)},
    {GlyphSet::Keyboard, "apostrophe", kKeyboard_apostrophe, sizeof(kKeyboard_apostrophe)},
    {GlyphSet::Keyboard, "arrows", kKeyboard_arrows, sizeof(kKeyboard_arrows)},
    {GlyphSet::Keyboard, "b", kKeyboard_b, sizeof(kKeyboard_b)},
    {GlyphSet::Keyboard, "backslash", kKeyboard_backslash, sizeof(kKeyboard_backslash)},
    {GlyphSet::Keyboard, "backspace", kKeyboard_backspace, sizeof(kKeyboard_backspace)},
    {GlyphSet::Keyboard, "c", kKeyboard_c, sizeof(kKeyboard_c)},
    {GlyphSet::Keyboard, "comma", kKeyboard_comma, sizeof(kKeyboard_comma)},
    {GlyphSet::Keyboard, "ctrl", kKeyboard_ctrl, sizeof(kKeyboard_ctrl)},
    {GlyphSet::Keyboard, "d", kKeyboard_d, sizeof(kKeyboard_d)},
    {GlyphSet::Keyboard, "delete", kKeyboard_delete, sizeof(kKeyboard_delete)},
    {GlyphSet::Keyboard, "down", kKeyboard_down, sizeof(kKeyboard_down)},
    {GlyphSet::Keyboard, "e", kKeyboard_e, sizeof(kKeyboard_e)},
    {GlyphSet::Keyboard, "end", kKeyboard_end, sizeof(kKeyboard_end)},
    {GlyphSet::Keyboard, "enter", kKeyboard_enter, sizeof(kKeyboard_enter)},
    {GlyphSet::Keyboard, "equals", kKeyboard_equals, sizeof(kKeyboard_equals)},
    {GlyphSet::Keyboard, "escape", kKeyboard_escape, sizeof(kKeyboard_escape)},
    {GlyphSet::Keyboard, "f", kKeyboard_f, sizeof(kKeyboard_f)},
    {GlyphSet::Keyboard, "g", kKeyboard_g, sizeof(kKeyboard_g)},
    {GlyphSet::Keyboard, "grave", kKeyboard_grave, sizeof(kKeyboard_grave)},
    {GlyphSet::Keyboard, "h", kKeyboard_h, sizeof(kKeyboard_h)},
    {GlyphSet::Keyboard, "home", kKeyboard_home, sizeof(kKeyboard_home)},
    {GlyphSet::Keyboard, "i", kKeyboard_i, sizeof(kKeyboard_i)},
    {GlyphSet::Keyboard, "insert", kKeyboard_insert, sizeof(kKeyboard_insert)},
    {GlyphSet::Keyboard, "j", kKeyboard_j, sizeof(kKeyboard_j)},
    {GlyphSet::Keyboard, "k", kKeyboard_k, sizeof(kKeyboard_k)},
    {GlyphSet::Keyboard, "l", kKeyboard_l, sizeof(kKeyboard_l)},
    {GlyphSet::Keyboard, "left", kKeyboard_left, sizeof(kKeyboard_left)},
    {GlyphSet::Keyboard, "left_bracket", kKeyboard_left_bracket, sizeof(kKeyboard_left_bracket)},
    {GlyphSet::Keyboard, "m", kKeyboard_m, sizeof(kKeyboard_m)},
    {GlyphSet::Keyboard, "minus", kKeyboard_minus, sizeof(kKeyboard_minus)},
    {GlyphSet::Keyboard, "n", kKeyboard_n, sizeof(kKeyboard_n)},
    {GlyphSet::Keyboard, "o", kKeyboard_o, sizeof(kKeyboard_o)},
    {GlyphSet::Keyboard, "p", kKeyboard_p, sizeof(kKeyboard_p)},
    {GlyphSet::Keyboard, "page_down", kKeyboard_page_down, sizeof(kKeyboard_page_down)},
    {GlyphSet::Keyboard, "page_up", kKeyboard_page_up, sizeof(kKeyboard_page_up)},
    {GlyphSet::Keyboard, "period", kKeyboard_period, sizeof(kKeyboard_period)},
    {GlyphSet::Keyboard, "q", kKeyboard_q, sizeof(kKeyboard_q)},
    {GlyphSet::Keyboard, "r", kKeyboard_r, sizeof(kKeyboard_r)},
    {GlyphSet::Keyboard, "right", kKeyboard_right, sizeof(kKeyboard_right)},
    {GlyphSet::Keyboard, "right_bracket", kKeyboard_right_bracket, sizeof(kKeyboard_right_bracket)},
    {GlyphSet::Keyboard, "s", kKeyboard_s, sizeof(kKeyboard_s)},
    {GlyphSet::Keyboard, "semicolon", kKeyboard_semicolon, sizeof(kKeyboard_semicolon)},
    {GlyphSet::Keyboard, "shift", kKeyboard_shift, sizeof(kKeyboard_shift)},
    {GlyphSet::Keyboard, "slash", kKeyboard_slash, sizeof(kKeyboard_slash)},
    {GlyphSet::Keyboard, "space", kKeyboard_space, sizeof(kKeyboard_space)},
    {GlyphSet::Keyboard, "t", kKeyboard_t, sizeof(kKeyboard_t)},
    {GlyphSet::Keyboard, "tab", kKeyboard_tab, sizeof(kKeyboard_tab)},
    {GlyphSet::Keyboard, "u", kKeyboard_u, sizeof(kKeyboard_u)},
    {GlyphSet::Keyboard, "up", kKeyboard_up, sizeof(kKeyboard_up)},
    {GlyphSet::Keyboard, "v", kKeyboard_v, sizeof(kKeyboard_v)},
    {GlyphSet::Keyboard, "w", kKeyboard_w, sizeof(kKeyboard_w)},
    {GlyphSet::Keyboard, "x", kKeyboard_x, sizeof(kKeyboard_x)},
    {GlyphSet::Keyboard, "y", kKeyboard_y, sizeof(kKeyboard_y)},
    {GlyphSet::Keyboard, "z", kKeyboard_z, sizeof(kKeyboard_z)},
};

// Textures carrying Wii button art, by their Dolphin texture-pack identity. All GX_TF_RGB5A3 (5).
struct IconTexture {
    uint64_t hash;
    uint16_t width, height;
};
constexpr IconTexture kPictureFont{0x7b53229a3b86fedeULL, 256, 256};
// Identified from the HD pack's SMN/Buttons (Wii art) and its ~PSX Buttons variant, which replaces
// exactly these two with a cross and a d-pad.
constexpr IconTexture kLayoutTwo{0x332ba0ec293b70b9ULL, 40, 40};
constexpr IconTexture kLayoutDpad{0x6de12cfa944308b1ULL, 48, 48};
constexpr const IconTexture* kIconTextures[] = {&kPictureFont, &kLayoutTwo, &kLayoutDpad};
constexpr uint32_t kIconFormat = 5;

// WPAD bits (NSMBW-Decomp WPAD.h). The d-pad icon follows screen-up, which is WPAD RIGHT on the
// sideways remote.
constexpr uint32_t kWpadRight = 1u << 1, kWpadPlus = 1u << 4, kWpad2 = 1u << 8, kWpad1 = 1u << 9;
constexpr uint32_t kWpadB = 1u << 10, kWpadA = 1u << 11, kWpadMinus = 1u << 12;

struct IconCell {
    const IconTexture* texture;
    uint16_t x, y, width, height;
    uint32_t wpad; // the Wii button this icon shows
};

// A picture-font character's cell: 32x32 at a 33-texel pitch, 7 per row, index = ch - 0x20.
constexpr IconCell FontCell(char ch, uint32_t wpad) {
    const int index = ch - 0x20;
    return {&kPictureFont, static_cast<uint16_t>(index % 7 * 33), static_cast<uint16_t>(index / 7 * 33), 32, 32, wpad};
}

constexpr IconCell kCells[] = {
    FontCell('!', kWpadRight),
    FontCell('"', kWpadA),
    FontCell('#', kWpadB),
    FontCell('%', kWpadPlus),
    FontCell('&', kWpadMinus),
    FontCell('\'', kWpad1),
    FontCell('(', kWpad2),
    {&kLayoutTwo, 0, 0, 40, 40, kWpad2},
    {&kLayoutDpad, 0, 0, 48, 48, kWpadRight},
};

GlyphSet g_lastDevice = GlyphSet::Wii; // what "Auto" shows
GlyphSet g_pinned = GlyphSet::Wii;     // NSMBW_GLYPH_TEST
bool g_initialized = false;
std::string g_appliedSignature;
bool g_log = false;

struct Choice {
    const char* config;
    const char* label;
};
constexpr Choice kChoices[] = {
    {"auto", "Auto (last used device)"}, {"wii", "Wii Remote (original)"}, {"xbox", "Xbox"},
    {"playstation", "PlayStation"},      {"switch", "Nintendo Switch"},    {"keyboard", "Keyboard"},
};
int g_choice = 0;

const char* SetName(GlyphSet set) {
    switch (set) {
    case GlyphSet::Xbox: return "xbox";
    case GlyphSet::PlayStation: return "playstation";
    case GlyphSet::Switch: return "switch";
    case GlyphSet::Keyboard: return "keyboard";
    default: return "wii";
    }
}

GlyphSet SetFromName(const char* name) {
    for (GlyphSet set : {GlyphSet::Xbox, GlyphSet::PlayStation, GlyphSet::Switch, GlyphSet::Keyboard}) {
        if (std::strcmp(name, SetName(set)) == 0) return set;
    }
    return GlyphSet::Wii;
}

GlyphSet SetForGamepadType(SDL_GamepadType type) {
    switch (type) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
        return GlyphSet::PlayStation;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return GlyphSet::Switch;
    case SDL_GAMEPAD_TYPE_GAMECUBE:
        return GlyphSet::Wii; // no GameCube set yet
    default:
        return GlyphSet::Xbox; // Xbox, and generic "standard" pads, which use Xbox labels
    }
}

const Glyph* FindGlyph(GlyphSet set, const std::string& control) {
    if (control.empty()) return nullptr;
    for (const auto& glyph : kGlyphs) {
        if (glyph.set == set && control == glyph.control) return &glyph;
    }
    return nullptr;
}

// Before any input: the pad on port 0 if there is one, else the keyboard.
GlyphSet DefaultDevice() {
    const s32 index = PADGetIndexForPort(0);
    if (index < 0) return GlyphSet::Keyboard;
    SDL_Gamepad* gamepad = PADGetSDLGamepadForIndex(static_cast<u32>(index));
    return gamepad != nullptr ? SetForGamepadType(SDL_GetGamepadType(gamepad)) : GlyphSet::Keyboard;
}

void NoteSdlEvent(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
        // F-keys drive the overlay (F10), not the game; don't let opening it flip the icons.
        if (!event.key.repeat && !(event.key.scancode >= SDL_SCANCODE_F1 && event.key.scancode <= SDL_SCANCODE_F12)) {
            g_lastDevice = GlyphSet::Keyboard;
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        g_lastDevice = SetForGamepadType(SDL_GetGamepadTypeForID(event.gbutton.which));
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        // Past resting drift only: about half tilt / half pull.
        if (event.gaxis.value > 16000 || event.gaxis.value < -16000) {
            g_lastDevice = SetForGamepadType(SDL_GetGamepadTypeForID(event.gaxis.which));
        }
        break;
    default:
        break;
    }
}

GlyphSet ActiveSet() {
    if (g_pinned != GlyphSet::Wii) return g_pinned;
    if (g_choice == 0) return g_lastDevice;
    return SetFromName(kChoices[g_choice].config);
}

// Rebuilds the patch list and hands it to aurora only when the resulting icons would change;
// each hand-off flushes every resolved texture, so it must not happen per frame.
void ApplyIfChanged() {
    const GlyphSet set = ActiveSet();
    std::vector<AuroraTexturePatch> patches[std::size(kIconTextures)];
    std::string signature = SetName(set);
    size_t total = 0;
    if (set != GlyphSet::Wii) {
        signature += '@';
        signature += nsmbw_controls::ContextName(nsmbw_controls::CurrentContext());
        for (const auto& cell : kCells) {
            const std::string control = set == GlyphSet::Keyboard ? nsmbw_controls::KeyGlyphFor(cell.wpad)
                                                                  : nsmbw_controls::GamepadGlyphFor(cell.wpad);
            const Glyph* glyph = FindGlyph(set, control);
            signature += ':';
            signature += glyph != nullptr ? control : "-";
            if (glyph == nullptr) continue; // keep the game's Wii icon
            for (size_t t = 0; t < std::size(kIconTextures); ++t) {
                if (kIconTextures[t] != cell.texture) continue;
                patches[t].push_back(AuroraTexturePatch{
                    .x = cell.x,
                    .y = cell.y,
                    .width = cell.width,
                    .height = cell.height,
                    .png = glyph->png,
                    .pngSize = glyph->size,
                });
                ++total;
            }
        }
    }
    if (signature == g_appliedSignature) return;
    g_appliedSignature = signature;
    for (size_t t = 0; t < std::size(kIconTextures); ++t) {
        const IconTexture& tex = *kIconTextures[t];
        aurora_set_texture_patches(tex.hash, tex.width, tex.height, kIconFormat, patches[t].data(), patches[t].size());
    }
    if (g_log) std::fprintf(stderr, "[nsmbw] button glyphs -> %s (%zu cells)\n", signature.c_str(), total);
}

} // namespace

void HandleEvents(const AuroraEvent* events) noexcept {
    if (!g_initialized) {
        g_initialized = true;
        g_log = std::getenv("NSMBW_LOG_GLYPHS") != nullptr;
        g_lastDevice = DefaultDevice();
        const std::string configured = RuntimeConfigFile::ButtonIcons("auto");
        for (int i = 0; i < static_cast<int>(std::size(kChoices)); ++i) {
            if (configured == kChoices[i].config) g_choice = i;
        }
        // NSMBW_GLYPH_TEST=xbox|playstation|switch|keyboard pins a set, to check the art without
        // that device.
        if (const char* test = std::getenv("NSMBW_GLYPH_TEST")) g_pinned = SetFromName(test);
    }
    for (const AuroraEvent* ev = events; ev != nullptr && ev->type != AURORA_NONE; ++ev) {
        if (ev->type == AURORA_SDL_EVENT) {
            NoteSdlEvent(ev->sdl);
        } else if (ev->type == AURORA_CONTROLLER_REMOVED) {
            g_lastDevice = DefaultDevice();
        }
    }
    // Every batch, not only on events: the context (course / map / menu) and rebinds change
    // without an input event, and the check is a few table lookups.
    ApplyIfChanged();
}

void DrawSettingsMenu() noexcept {
    const char* current = kChoices[g_choice].label;
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("Button icons", current)) {
        for (int i = 0; i < static_cast<int>(std::size(kChoices)); ++i) {
            if (ImGui::Selectable(kChoices[i].label, i == g_choice)) {
                g_choice = i;
                RuntimeConfigFile::SetButtonIcons(kChoices[i].config);
            }
        }
        ImGui::EndCombo();
    }
}

} // namespace nsmbw_button_glyphs
