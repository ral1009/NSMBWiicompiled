# Builds projects/nsmbw/assets/button_glyphs/ from Kenney's "Input Prompts" pack (CC0,
# https://kenney.nl/assets/input-prompts, tested with 1.5A). Usage:
#   python import_kenney_glyphs.py <unzipped kenney_input-prompts folder>
# then python gen_button_glyphs.py to regenerate the embedded table.
#
# File names are our control names: SDL gamepad positions (south/east/west/north, ...), which is
# what the runtime reads from the PAD button mapping, and keyboard key names from
# nsmbw_button_glyphs.cpp.in's scancode table.
#
# Kenney's filled icons are white with the symbol cut out as transparency. On the light panels
# NSMBW draws prompts over (the "Hold the Wii Remote sideways" card is near-white) the symbol
# would vanish, so transparent areas enclosed by the icon are filled black: white icon,
# black symbol, readable on any background.
import os, shutil, sys
from collections import deque
import numpy as np
from PIL import Image

SRC = sys.argv[1]
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'button_glyphs')
FILL = np.array([0, 0, 0], float)  # symbols black (developer request)

GAMEPADS = {
    'xbox': ('Xbox Series', {
        'south': 'xbox_button_a', 'east': 'xbox_button_b', 'west': 'xbox_button_x', 'north': 'xbox_button_y',
        'back': 'xbox_button_view', 'start': 'xbox_button_menu', 'guide': 'xbox_guide',
        'left_stick': 'xbox_ls', 'right_stick': 'xbox_rs', 'left_shoulder': 'xbox_lb', 'right_shoulder': 'xbox_rb',
        'left_trigger': 'xbox_lt', 'right_trigger': 'xbox_rt', 'dpad': 'xbox_dpad',
        'lstick': 'xbox_stick_l', 'rstick': 'xbox_stick_r'}),
    'playstation': ('PlayStation Series', {
        'south': 'playstation_button_cross', 'east': 'playstation_button_circle',
        'west': 'playstation_button_square', 'north': 'playstation_button_triangle',
        # back/start (Create/Options) are drawn below: Kenney's versions are bare pills.
        'left_stick': 'playstation_button_l3', 'right_stick': 'playstation_button_r3',
        'left_shoulder': 'playstation_trigger_l1', 'right_shoulder': 'playstation_trigger_r1',
        'left_trigger': 'playstation_trigger_l2', 'right_trigger': 'playstation_trigger_r2',
        'dpad': 'playstation_dpad', 'lstick': 'playstation_stick_l', 'rstick': 'playstation_stick_r'}),
    # SDL3 reports Switch buttons by position: south is the B button, east is A.
    'switch': ('Nintendo Switch', {
        'south': 'switch_button_b', 'east': 'switch_button_a', 'west': 'switch_button_y', 'north': 'switch_button_x',
        'back': 'switch_button_minus', 'start': 'switch_button_plus', 'guide': 'switch_button_home',
        'left_stick': 'switch_stick_l_press', 'right_stick': 'switch_stick_r_press',
        'left_shoulder': 'switch_button_l', 'right_shoulder': 'switch_button_r',
        'left_trigger': 'switch_button_zl', 'right_trigger': 'switch_button_zr', 'dpad': 'switch_dpad',
        'lstick': 'switch_stick_l', 'rstick': 'switch_stick_r'}),
}

KEYS = {c: c for c in 'abcdefghijklmnopqrstuvwxyz0123456789'}
KEYS.update({
    'enter': 'enter', 'space': 'space', 'tab': 'tab', 'escape': 'escape', 'backspace': 'backspace',
    'shift': 'shift', 'ctrl': 'ctrl', 'alt': 'alt', 'up': 'arrow_up', 'down': 'arrow_down',
    'left': 'arrow_left', 'right': 'arrow_right', 'arrows': 'arrows', 'equals': 'equals', 'minus': 'minus',  # not arrows_all: red highlight
    'comma': 'comma', 'period': 'period', 'semicolon': 'semicolon', 'apostrophe': 'apostrophe',
    'slash': 'slash_forward', 'backslash': 'slash_back', 'grave': 'tilde', 'left_bracket': 'bracket_open',
    'right_bracket': 'bracket_close', 'delete': 'delete', 'insert': 'insert', 'home': 'home', 'end': 'end',
    'page_up': 'page_up', 'page_down': 'page_down',
})


def fill_enclosed(im):
    rgba = np.asarray(im.convert('RGBA')).astype(float)
    a = rgba[..., 3] / 255.0
    H, W = a.shape
    ext = np.zeros((H, W), bool)
    q = deque([(y, x) for y in range(H) for x in (0, W - 1)] + [(y, x) for x in range(W) for y in (0, H - 1)])
    while q:
        y, x = q.popleft()
        if ext[y, x] or a[y, x] >= 0.5:
            continue
        ext[y, x] = True
        for ny, nx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
            if 0 <= ny < H and 0 <= nx < W and not ext[ny, nx]:
                q.append((ny, nx))
    out = rgba.copy()
    inner = ~ext
    out[inner, :3] = rgba[inner, :3] * a[inner, None] + FILL * (1 - a[inner, None])
    out[inner, 3] = 255
    return Image.fromarray(out.clip(0, 255).astype(np.uint8), 'RGBA')


def convert(src, dst):
    fill_enclosed(Image.open(src)).save(dst)


if os.path.isdir(OUT):
    shutil.rmtree(OUT)
for set_name, (folder, items) in GAMEPADS.items():
    os.makedirs(os.path.join(OUT, set_name))
    for control, name in items.items():
        convert(os.path.join(SRC, folder, 'Double', name + '.png'), os.path.join(OUT, set_name, control + '.png'))
    print(set_name, len(items))
os.makedirs(os.path.join(OUT, 'keyboard'))
for key, name in KEYS.items():
    convert(os.path.join(SRC, 'Keyboard & Mouse', 'Double', 'keyboard_' + name + '.png'),
            os.path.join(OUT, 'keyboard', key + '.png'))
print('keyboard', len(KEYS))

# PlayStation Create/Options as white discs with a black symbol, matching the face buttons.
from PIL import ImageDraw
def draw_disc_icon(path, symbol, size=128):
    S = size * 4  # supersample, then downscale for smooth edges
    im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    fill = tuple(int(v) for v in FILL) + (255,)
    m = int(S * 0.06)
    d.ellipse((m, m, S - m, S - m), fill=(255, 255, 255, 255))
    c = S // 2
    if symbol == 'menu':      # Options: three bars
        w, h, gap = int(S * 0.44), int(S * 0.075), int(S * 0.15)
        for k in (-1, 0, 1):
            y = c + k * gap
            d.rounded_rectangle((c - w // 2, y - h // 2, c + w // 2, y + h // 2), radius=h // 2, fill=fill)
    else:                     # Create/Share: arrow up out of a tray
        t = int(S * 0.07)
        d.polygon([(c, int(S * 0.24)), (c - int(S * 0.15), int(S * 0.42)), (c + int(S * 0.15), int(S * 0.42))], fill=fill)
        d.rectangle((c - t // 2, int(S * 0.38), c + t // 2, int(S * 0.62)), fill=fill)
        x0, x1, y0, y1 = int(S * 0.28), int(S * 0.72), int(S * 0.52), int(S * 0.74)
        d.rectangle((x0, y0, x0 + t, y1), fill=fill)
        d.rectangle((x1 - t, y0, x1, y1), fill=fill)
        d.rectangle((x0, y1 - t, x1, y1), fill=fill)
    im.resize((size, size), Image.LANCZOS).save(path)
draw_disc_icon(os.path.join(OUT, 'playstation', 'start.png'), 'menu')
draw_disc_icon(os.path.join(OUT, 'playstation', 'back.png'), 'share')
print('playstation start/back drawn')
