"""Copy one game version's code out of an extracted disc, for the translator.

Usage:
  python projects/nsmbw/tools/unpack_game_code.py <extracted disc> <out dir>
  e.g.  ... C:\\Emulation\\roms\\wii\\DATA NSMBW-Versions/E1

<extracted disc> is a folder with sys\\ and files\\ (Dolphin: Extract Entire Disc, data partition).
Writes <out>/main.dol (sys/main.dol) and <out>/<name>.rel for the four boot RELs, which the disc
stores LZ77-compressed (files/rels/<name>.rel.LZ, Nintendo's type 0x11 format). The output is game
code: keep it in a gitignored folder.

Check: run it on the PAL disc and compare with NSMBW-Decomp/original/*.rel (byte-identical).

Type 0x11 format (the same one GBATEK documents as "LZ77 11h"): a 4-byte header (0x11 then the
24-bit decompressed size), then groups of one flag byte + 8 items, highest bit first. Flag 0 = one
literal byte. Flag 1 = a back-reference whose first nibble picks its length encoding:
  0     -> 3 bytes: length = ((b0 & 0xF) << 4 | b1 >> 4) + 0x11,   distance from the low 12 bits
  1     -> 4 bytes: length = ((b0 & 0xF) << 12 | b1 << 4 | b2 >> 4) + 0x111
  2..15 -> 2 bytes: length = (b0 >> 4) + 1
and the distance (minus 1) is always the last 12 bits.
"""
import hashlib
import sys
from pathlib import Path

RELS = ("d_profileNP", "d_basesNP", "d_enemiesNP", "d_en_bossNP")


def lz11_decompress(data: bytes) -> bytes:
    if data[0] != 0x11:
        raise ValueError("not LZ77 type 0x11 data (first byte 0x%02x)" % data[0])
    size = data[1] | data[2] << 8 | data[3] << 16
    out = bytearray()
    pos = 4
    while len(out) < size:
        flags = data[pos]
        pos += 1
        for bit in range(7, -1, -1):
            if len(out) >= size:
                break
            if not (flags >> bit) & 1:
                out.append(data[pos])
                pos += 1
                continue
            b0 = data[pos]
            kind = b0 >> 4
            if kind == 0:
                b1, b2 = data[pos + 1], data[pos + 2]
                length = ((b0 & 0xF) << 4 | b1 >> 4) + 0x11
                disp = ((b1 & 0xF) << 8 | b2) + 1
                pos += 3
            elif kind == 1:
                b1, b2, b3 = data[pos + 1], data[pos + 2], data[pos + 3]
                length = ((b0 & 0xF) << 12 | b1 << 4 | b2 >> 4) + 0x111
                disp = ((b2 & 0xF) << 8 | b3) + 1
                pos += 4
            else:
                b1 = data[pos + 1]
                length = kind + 1
                disp = ((b0 & 0xF) << 8 | b1) + 1
                pos += 2
            start = len(out) - disp
            if start < 0:
                raise ValueError("back-reference before the start of the output")
            for i in range(length):  # byte by byte: a copy may overlap its own output
                out.append(out[start + i])
    return bytes(out[:size])


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    disc, out = Path(sys.argv[1]), Path(sys.argv[2])
    boot = (disc / "sys" / "boot.bin").read_bytes()
    print("disc: %s revision %d" % (boot[:6].decode("ascii"), boot[7]))
    out.mkdir(parents=True, exist_ok=True)
    dol = (disc / "sys" / "main.dol").read_bytes()
    (out / "main.dol").write_bytes(dol)
    print("main.dol  %8d bytes  sha256 %s" % (len(dol), hashlib.sha256(dol).hexdigest()))
    for name in RELS:
        rel = lz11_decompress((disc / "files" / "rels" / (name + ".rel.LZ")).read_bytes())
        (out / (name + ".rel")).write_bytes(rel)
        print("%-12s %8d bytes  sha256 %s" % (name, len(rel), hashlib.sha256(rel).hexdigest()))


if __name__ == "__main__":
    main()
