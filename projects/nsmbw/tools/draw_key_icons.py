# Draws the keyboard glyphs whose Kenney versions spell the key's name (ENTER, SHIFT, TAB, ...)
# as symbol keycaps instead: same 128 px canvas and 96 px white rounded square as Kenney's letter
# keys, black symbol. import_kenney_glyphs.py calls this after copying the pack; it can also be
# run on its own to redraw just these:  python draw_key_icons.py
import os
from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'button_glyphs', 'keyboard')
SIZE, SS = 128, 4          # final size, supersample factor
S = SIZE * SS
K = 255, 255, 255, 255     # keycap
F = 0, 0, 0, 255           # symbol


def px(v):                 # 0..128 design units -> supersampled pixels
    return int(round(v * SS))


def keycap():
    im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((px(16), px(16), px(112), px(112)), radius=px(14), fill=K)
    return im, d


def line(d, pts, w=9):
    pts = [(px(x), px(y)) for x, y in pts]
    d.line(pts, fill=F, width=px(w), joint='curve')
    r = px(w) // 2
    for x, y in pts:       # round caps
        d.ellipse((x - r, y - r, x + r, y + r), fill=F)


def tri(d, pts):
    d.polygon([(px(x), px(y)) for x, y in pts], fill=F)


def enter(d):              # return arrow: down from top right, then left
    line(d, [(86, 38), (86, 74), (52, 74)])
    tri(d, [(36, 74), (56, 58), (56, 90)])


def shift(d):              # hollow up arrow
    pts = [(64, 34), (94, 66), (76, 66), (76, 92), (52, 92), (52, 66), (34, 66)]
    d.polygon([(px(x), px(y)) for x, y in pts], outline=F, width=px(7))


def tab(d):                # arrow into a bar
    line(d, [(36, 64), (74, 64)])
    tri(d, [(88, 64), (70, 48), (70, 80)])
    line(d, [(92, 42), (92, 86)])


def backspace(d):          # left-pointing key outline with an x
    pts = [(34, 64), (52, 42), (94, 42), (94, 86), (52, 86)]
    d.polygon([(px(x), px(y)) for x, y in pts], outline=F, width=px(7))
    line(d, [(60, 54), (80, 74)], 7)
    line(d, [(80, 54), (60, 74)], 7)


def space(d):              # open-box space symbol
    line(d, [(36, 60), (36, 80), (92, 80), (92, 60)])


ICONS = {'enter': enter, 'shift': shift, 'tab': tab, 'backspace': backspace, 'space': space}


def draw_all(out=OUT):
    for name, fn in ICONS.items():
        im, d = keycap()
        fn(d)
        im.resize((SIZE, SIZE), Image.LANCZOS).save(os.path.join(out, name + '.png'))
    print('keyboard symbol keys drawn:', ', '.join(ICONS))


if __name__ == '__main__':
    draw_all()
