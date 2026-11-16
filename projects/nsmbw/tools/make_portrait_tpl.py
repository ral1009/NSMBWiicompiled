"""Make the Add/Drop Players selector's Mario portrait from a PNG.

    python -I projects/nsmbw/tools/make_portrait_tpl.py <portrait.png> <nsmbw_data dir> [--hd-pack <texture pack dir>]

Writes <nsmbw_data>/portraits/Im_plofileMario_00.tpl: the picture scaled to 150x150 and stored the
way the game's own portraits are (GX format 5, RGB5A3), which runtime/src/product/nsmbw_layout_patch.cpp
adds to the Add/Drop Players selector as Mario's card (PatchCharaContents). Use a square PNG with a transparent
background, framed like the other portraits (Luigi's head near the top, shoulders at the bottom).

With --hd-pack, the PNG itself is also copied into that HD texture pack folder under the name the
HD-pack loader looks for (tex1_150x150_<XXH64 of the 150x150 pixel bytes>_5.png), so the HD pack
shows it at full resolution. Without it, that name is only printed.

No packages needed: PNG decoding (8-bit RGB / RGBA, not interlaced), the resize and XXH64 are here.
"""
import argparse
import os
import shutil
import struct
import sys
import zlib

SIDE = 150


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit(f"{path}: not a PNG")
    pos, idat, w = 8, b"", None
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or interlace:
                sys.exit(f"{path}: needs an 8-bit RGB or RGBA PNG without interlacing")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    bpp = 4 if ctype == 6 else 3
    raw, stride = zlib.decompress(idat), w * bpp
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        f, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b, c = prev[x], (prev[x - bpp] if x >= bpp else 0)
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    px = []
    for line in rows:
        for x in range(w):
            q = line[x * bpp:(x + 1) * bpp]
            px.append(tuple(q) if bpp == 4 else (q[0], q[1], q[2], 255))
    return w, h, px


def resize(w, h, px, side):
    """Area average (each output pixel averages the source pixels under it), alpha-weighted so
    transparent pixels don't darken the edges."""
    out = []
    for oy in range(side):
        y0, y1 = oy * h / side, (oy + 1) * h / side
        for ox in range(side):
            x0, x1 = ox * w / side, (ox + 1) * w / side
            acc = [0.0] * 4
            wsum = 0.0
            for sy in range(int(y0), min(h, int(y1 + 0.999999))):
                wy = min(y1, sy + 1) - max(y0, sy)
                for sx in range(int(x0), min(w, int(x1 + 0.999999))):
                    wx = min(x1, sx + 1) - max(x0, sx)
                    r, g, b, a = px[sy * w + sx]
                    k = wx * wy
                    acc[0] += r * a * k; acc[1] += g * a * k; acc[2] += b * a * k; acc[3] += a * k
                    wsum += k
            a = acc[3] / wsum if wsum else 0
            if acc[3] > 0:
                out.append((round(acc[0] / acc[3]), round(acc[1] / acc[3]), round(acc[2] / acc[3]), round(a)))
            else:
                out.append((0, 0, 0, 0))
    return out


def rgb5a3(c):
    r, g, b, a = c
    if a >= 0xE0:
        return 0x8000 | (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3)
    return (a >> 5) << 12 | (r >> 4) << 8 | (g >> 4) << 4 | (b >> 4)


def encode(px, side):
    """GX RGB5A3: 4x4-pixel blocks left to right, top to bottom, 16 big-endian u16s per block;
    blocks past the edge are padded with transparent pixels."""
    out = bytearray()
    blocks = (side + 3) // 4
    for by in range(blocks):
        for bx in range(blocks):
            for y in range(by * 4, by * 4 + 4):
                for x in range(bx * 4, bx * 4 + 4):
                    v = rgb5a3(px[y * side + x]) if x < side and y < side else 0
                    out += struct.pack(">H", v)
    return bytes(out)


def tpl(pixels, side):
    header = struct.pack(">III", 0x0020AF30, 1, 0x0C) + struct.pack(">II", 0x14, 0)
    image = struct.pack(">HHII", side, side, 5, 0x40) + struct.pack(">IIIIfBBBB", 0, 0, 1, 1, 0.0, 0, 0, 0, 0)
    head = header + image
    return head + b"\0" * (0x40 - len(head)) + pixels


M = (1 << 64) - 1
P1, P2, P3, P4, P5 = 11400714785074694791, 14029467366897019727, 1609587929392839161, 9650029242287828579, 2870177450012600261


def rotl(x, r):
    return ((x << r) | (x >> (64 - r))) & M


def xxh64(data, seed=0):
    """XXH64, as aurora's HD-pack loader hashes a texture's pixel bytes (texture_replacement.cpp)."""
    n, i = len(data), 0
    def rnd(acc, v):
        return rotl((acc + v * P2) & M, 31) * P1 & M
    if n >= 32:
        v = [(seed + P1 + P2) & M, (seed + P2) & M, seed, (seed - P1) & M]
        while i + 32 <= n:
            for k in range(4):
                v[k] = rnd(v[k], struct.unpack_from("<Q", data, i + 8 * k)[0])
            i += 32
        h = (rotl(v[0], 1) + rotl(v[1], 7) + rotl(v[2], 12) + rotl(v[3], 18)) & M
        for k in range(4):
            h = ((h ^ rnd(0, v[k])) * P1 + P4) & M
    else:
        h = (seed + P5) & M
    h = (h + n) & M
    while i + 8 <= n:
        h = (rotl(h ^ rnd(0, struct.unpack_from("<Q", data, i)[0]), 27) * P1 + P4) & M
        i += 8
    if i + 4 <= n:
        h = (rotl(h ^ (struct.unpack_from("<I", data, i)[0] * P1 & M), 23) * P2 + P3) & M
        i += 4
    while i < n:
        h = (rotl(h ^ (data[i] * P5 & M), 11) * P1) & M
        i += 1
    h ^= h >> 33; h = h * P2 & M
    h ^= h >> 29; h = h * P3 & M
    return h ^ (h >> 32)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("png")
    ap.add_argument("data_dir", help="the nsmbw_data folder next to the exe")
    ap.add_argument("--hd-pack", help="HD texture pack folder to copy the full-size PNG into")
    a = ap.parse_args()
    w, h, px = read_png(a.png)
    pixels = encode(resize(w, h, px, SIDE), SIDE)
    out_dir = os.path.join(a.data_dir, "portraits")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, "Im_plofileMario_00.tpl")
    open(out, "wb").write(tpl(pixels, SIDE))
    hd_name = f"tex1_{SIDE}x{SIDE}_{xxh64(pixels):016x}_5.png"
    print(f"wrote {out} ({w}x{h} -> {SIDE}x{SIDE})")
    if a.hd_pack:
        dest = os.path.join(a.hd_pack, hd_name)
        shutil.copyfile(a.png, dest)
        print(f"copied the full-size PNG to {dest}")
    else:
        print(f"HD pack name for a full-size version: {hd_name}")


if __name__ == "__main__":
    main()
