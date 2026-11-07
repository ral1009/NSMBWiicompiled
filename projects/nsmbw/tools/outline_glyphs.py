# Gives every PNG in projects/nsmbw/assets/button_glyphs/ a black outline, so a white icon stays
# readable on bright backgrounds (the sky, the light menu panels). import_kenney_glyphs.py calls
# outline_all() as its last step. Not idempotent: run it once on freshly imported art, because a
# second run would outline the outline. Then python gen_button_glyphs.py.
#
# The art fills its 128 px canvas almost to the edge, so each icon is scaled down by the outline's
# width on every side first; every icon gets the same factor, so a set keeps its relative sizes.
import math, os
from PIL import Image, ImageChops

OUTLINE = 8        # width at 128 px; a prompt draws that canvas into a 32 px font cell
SUPERSAMPLE = 4
DIRECTIONS = 48


def outline(im):
    size = im.width
    big = size * SUPERSAMPLE
    margin = OUTLINE * SUPERSAMPLE
    inner = big - 2 * margin
    glyph = Image.new('RGBA', (big, big), (0, 0, 0, 0))
    glyph.paste(im.convert('RGBA').resize((inner, inner), Image.LANCZOS), (margin, margin))
    alpha = glyph.getchannel('A')
    grown = alpha
    # A disc of offsets gives round corners, unlike a square dilation.
    for ring in (margin, margin // 2):
        for i in range(DIRECTIONS):
            angle = 2 * math.pi * i / DIRECTIONS
            grown = ImageChops.lighter(grown, ImageChops.offset(alpha, round(ring * math.cos(angle)), round(ring * math.sin(angle))))
    black = Image.new('L', (big, big), 0)
    out = Image.merge('RGBA', (black, black, black, grown))
    out.alpha_composite(glyph)
    return out.resize((size, size), Image.LANCZOS)


def outline_all(root):
    count = 0
    for folder, _, files in os.walk(root):
        for name in files:
            if name.endswith('.png'):
                path = os.path.join(folder, name)
                outline(Image.open(path)).save(path)
                count += 1
    print('outlined', count)


if __name__ == '__main__':
    outline_all(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'button_glyphs'))
