"""Write another game version's function_map.txt from ours (PAL rev 1, "P1"), and check it.

Usage:
  python projects/nsmbw/tools/map_function_map.py E1 NSMBW-Versions/E1 NSMBW-Versions/E1/function_map.txt

Each "ADDR [name]" line of projects/nsmbw/function_map.txt is converted with the community address
map (nsmbw_address_map.py). The map is checked, not trusted: for every function, the instruction word
at its start in our code (NSMBW-Decomp/original) is compared with the word at the converted address
in the other version's code (<code dir>, from unpack_game_code.py). Unrelocated REL words are
compared as stored in the file (OSLink fills their relocations in later, in both versions alike).

Reported, and left out of the output:
  unmapped   - the address map has no counterpart (the function differs or does not exist there)
  mismatch   - the first instruction differs (the map points somewhere else, or the code changed)

--prove compares every word of every function (up to the next function start) and classifies it:
  identical  - every word equal
  equivalent - the differing words are all explained by the address map: a relative branch whose
               targets map to each other, an r13/r2-relative access whose addresses map (r13 moved
               0x300 but the small-data variables 0x2E0/0x2F8, so these offsets differ), or a
               lis + low-half pair building an address that maps
  different  - some differing word is not explained; listed (still written to the output, since
               the function exists in both; its translated body is simply the other version's)
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from nsmbw_address_map import AddressMap  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]
# PAL load addresses (projects/nsmbw/nsmbw-<rel>.yml); the other version's are these, mapped.
REL_LOAD_P1 = {"d_profileNP": 0x807684C0, "d_basesNP": 0x8076D680,
               "d_enemiesNP": 0x809A2CA0, "d_en_bossNP": 0x80B1C920}


class Image:
    """The guest memory a version's code occupies: DOL sections plus RELs at their load addresses."""

    def __init__(self):
        self.spans = []  # (start, bytes)

    def add_dol(self, data):
        offs = struct.unpack(">18I", data[0:0x48])
        addrs = struct.unpack(">18I", data[0x48:0x90])
        sizes = struct.unpack(">18I", data[0x90:0xD8])
        for o, a, s in zip(offs, addrs, sizes):
            if s:
                self.spans.append((a, data[o:o + s]))

    def add_rel(self, data, load):
        # Sections sit at load + their file offset. Only up to the end of the last section: the
        # relocation tables after it are discarded once OSLink has applied them, and the next REL
        # is loaded into that memory (d_enemiesNP starts inside d_basesNP's file length).
        count, info = struct.unpack(">II", data[0x0C:0x14])
        end = 0
        for i in range(count):
            off, size = struct.unpack(">II", data[info + 8 * i:info + 8 * i + 8])
            if off & ~1:
                end = max(end, (off & ~1) + size)
        self.spans.append((load, data[:end]))

    def word(self, addr):
        for start, data in self.spans:
            if start <= addr and addr + 4 <= start + len(data):
                return struct.unpack(">I", data[addr - start:addr - start + 4])[0]
        return None


def load_image(code_dir, dol_name, rel_loads):
    img = Image()
    img.add_dol((code_dir / dol_name).read_bytes())
    for name, load in rel_loads.items():
        img.add_rel((code_dir / (name + ".rel")).read_bytes(), load)
    return img


# Small-data base registers per version (r13 = _SDA_BASE_, r2 = _SDA2_BASE_), from each main.dol's
# __init_registers. P1 matches projects/nsmbw/nsmbw.yml; checked against the DOL in main().
SDA = {"P1": (0x8042F980, 0x80433360), "E1": (0x8042F680, 0x80433080)}


def read_init_registers(img, entry=0x80004050):
    """Find `lis r13,hi; ori/addi r13,..` and the same for r2 near the entry point (__start calls
    __init_registers first). Returns (r13, r2) as the DOL sets them."""
    vals = {}
    pending = {}
    for addr in range(entry, entry + 0x400, 4):
        w = img.word(addr)
        if w is None:
            break
        op, rd, ra, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
        if op == 15 and ra == 0 and rd in (2, 13):  # lis
            pending[rd] = imm << 16
        elif op in (24, 14) and rd in (2, 13) and ra == rd and rd in pending:  # ori / addi
            simm = imm - 0x10000 if (op == 14 and imm & 0x8000) else imm
            vals[rd] = (pending[rd] + simm) & 0xFFFFFFFF if op == 14 else pending[rd] | imm
        if 2 in vals and 13 in vals:
            break
    return vals.get(13), vals.get(2)


def simm16(w):
    v = w & 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def prove(src, dst, addr, new, length, amap, version):
    """Returns (identical, unexplained word offsets)."""
    p_r13, p_r2 = SDA["P1"]
    d_r13, d_r2 = SDA[version]
    lis_p, lis_d = {}, {}  # register -> value loaded by the last lis, per version
    identical, bad = True, []
    for off in range(0, length, 4):
        a, b = src.word(addr + off), dst.word(new + off)
        if a is None or b is None:
            break
        op, rd, ra = a >> 26, (a >> 21) & 31, (a >> 16) & 31
        if op == 15 and ra == 0:  # lis rd, hi
            lis_p[rd] = (a & 0xFFFF) << 16
            if b >> 16 == a >> 16:
                lis_d[rd] = (b & 0xFFFF) << 16
        if a == b:
            continue
        identical = False
        # A whole-word pointer (vtables, profile tables and other data the map lists as symbols).
        if 0x80000000 <= a < 0x81800000 and amap.convert(a, "P1", version) == b:
            continue
        if (a >> 16) != (b >> 16) and op != 15 and op != 18:
            bad.append(off)  # opcode/registers differ: real code change
            continue
        explained = False
        if op == 18:  # b/bl: 24-bit displacement
            da = (a & 0x03FFFFFC) - (0x04000000 if a & 0x02000000 else 0)
            db = (b & 0x03FFFFFC) - (0x04000000 if b & 0x02000000 else 0)
            if (a & 3) == (b & 3) and (a & 2) == 0:
                explained = amap.convert((addr + off + da) & 0xFFFFFFFF, "P1", version) == (new + off + db) & 0xFFFFFFFF
        elif op == 15:  # lis whose half differs: explained when its pair (later) maps
            explained = True
            lis_d[rd] = (b & 0xFFFF) << 16
        elif ra in (13, 2) and op in (14, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55):
            base_p, base_d = (p_r13, d_r13) if ra == 13 else (p_r2, d_r2)
            explained = amap.convert((base_p + simm16(a)) & 0xFFFFFFFF, "P1", version) == (base_d + simm16(b)) & 0xFFFFFFFF
        elif ra in lis_p and ra in lis_d and op in (14, 24, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55):
            lo_a = (a & 0xFFFF) if op == 24 else simm16(a)
            lo_b = (b & 0xFFFF) if op == 24 else simm16(b)
            explained = amap.convert((lis_p[ra] + lo_a) & 0xFFFFFFFF, "P1", version) == (lis_d[ra] + lo_b) & 0xFFFFFFFF
        if not explained:
            bad.append(off)
    return identical, bad


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    do_prove = "--prove" in sys.argv
    if len(args) != 3:
        sys.exit(__doc__)
    version, code_dir, out_path = args[0], Path(args[1]), Path(args[2])
    amap = AddressMap.load()
    rel_loads = {n: amap.convert(a, "P1", version) for n, a in REL_LOAD_P1.items()}
    src = load_image(ROOT / "NSMBW-Decomp" / "original", "wiimj2d.dol", REL_LOAD_P1)
    dst = load_image(code_dir, "main.dol", rel_loads)
    for v, img in (("P1", src), (version, dst)):
        got = read_init_registers(img)
        if got != SDA[v]:
            sys.exit("SDA table wrong for %s: DOL sets r13/r2 = %s" % (v, ["%08X" % x if x else None for x in got]))
    print("r13/r2 from the DOLs: P1 %08X/%08X, %s %08X/%08X" % (SDA["P1"] + (version,) + SDA[version]))

    entries = []
    for raw in (ROOT / "projects" / "nsmbw" / "function_map.txt").read_text().splitlines():
        parts = raw.split()
        if parts:
            entries.append((int(parts[0], 16), parts))
    starts = sorted({a for a, _ in entries})
    next_start = {a: b for a, b in zip(starts, starts[1:])}

    lines, unmapped, different = [], [], []
    counts = {"identical": 0, "equivalent": 0, "different": 0}
    for addr, parts in entries:
        raw = " ".join(parts)
        new = amap.convert(addr, "P1", version)
        if new is None:
            unmapped.append(raw)
            continue
        length = min(next_start.get(addr, addr + 4) - addr, 0x10000) if do_prove else 4
        identical, bad = prove(src, dst, addr, new, length, amap, version)
        if bad:
            counts["different"] += 1
            different.append("%s -> %08X: %d unexplained word(s), first at +0x%X" % (raw, new, len(bad), bad[0]))
        else:
            counts["identical" if identical else "equivalent"] += 1
        lines.append(" ".join(["%08X" % new] + parts[1:]))
    out_path.write_text("\n".join(lines) + "\n")
    print("REL load addresses for %s: %s" % (version, ", ".join("%s %08X" % kv for kv in rel_loads.items())))
    print("%s: %d identical, %d equivalent, %d different, %d unmapped (of %d)" % (
        "whole functions" if do_prove else "first word only", counts["identical"], counts["equivalent"],
        counts["different"], len(unmapped), len(entries)))
    for title, rows in (("unmapped (left out)", unmapped), ("different (kept; see above)", different)):
        if rows:
            print("--", title)
            for r in rows:
                print("  ", r)


if __name__ == "__main__":
    main()
