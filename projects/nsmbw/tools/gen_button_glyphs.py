# Regenerates runtime/src/product/nsmbw_button_glyphs.cpp from nsmbw_button_glyphs.cpp.in and
# the PNGs in projects/nsmbw/assets/button_glyphs/. Run after adding or removing a glyph.
import os
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
SETS = [('xbox', 'Xbox'), ('playstation', 'PlayStation'), ('switch', 'Switch'), ('keyboard', 'Keyboard')]

arrays, table = [], []
for folder, enum in SETS:
    base = os.path.join(ROOT, 'projects', 'nsmbw', 'assets', 'button_glyphs', folder)
    for f in sorted(os.listdir(base)):
        if not f.endswith('.png'):
            continue
        c = f[:-4]
        rel = f'projects/nsmbw/assets/button_glyphs/{folder}/{f}'
        name = f'k{enum}_{c}'
        arrays.append(f'constexpr unsigned char {name}[] = {{\n#embed "../../../{rel}"\n}};')
        table.append(f'    {{GlyphSet::{enum}, "{c}", {name}, sizeof({name})}},')

src = open(os.path.join(os.path.dirname(__file__), 'nsmbw_button_glyphs.cpp.in')).read()
src = src.replace('@ARRAYS@', '\n'.join(arrays)).replace('@TABLE@', '\n'.join(table))
open(os.path.join(ROOT, 'runtime', 'src', 'product', 'nsmbw_button_glyphs.cpp'), 'w', newline='').write(src)
print(len(table), 'glyphs')
