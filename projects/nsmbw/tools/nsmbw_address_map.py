"""Read NSMBW-Maps/address-map.txt (NSMBW-Updated, by RoadrunnerWMC; originally Ninji and CLF78) and
convert addresses between game versions. Used by the multi-version tools (map_function_map.py, ...).

The file has one [VERSION] block per game version. Each block may "extend" another and then lists
inclusive address ranges of the parent version with a constant offset:
    [E1]
    extend P1
    80000000-800b4600: +0x0
    800b4630-800b463f: -0x20
An address of the parent version inside a range maps to address + offset in this version; an address
in no range has no counterpart (the map is strict on purpose: unmapped means "not portable").
P1 (PAL rev 1, the version this repo is written against) is the root: "80000000-*: +0x0".

    m = AddressMap.load()
    m.convert(0x801D2D60, "P1", "E1")   # -> 0x801D2C20, or None when unmapped
"""
import re
from pathlib import Path

DEFAULT_PATH = Path(__file__).resolve().parents[3] / "NSMBW-Maps" / "address-map.txt"
_RANGE = re.compile(r"^([0-9A-Fa-f]{8})-([0-9A-Fa-f]{8}|\*):\s*([+-])0x([0-9A-Fa-f]+)")


class AddressMap:
    def __init__(self, parents, ranges):
        self.parents = parents  # version -> parent version (None for the root)
        self.ranges = ranges    # version -> [(start, end, offset)] in parent-version addresses

    @classmethod
    def load(cls, path=DEFAULT_PATH):
        parents, ranges, current = {}, {}, None
        for raw in Path(path).read_text(encoding="utf-8").splitlines():
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            m = re.match(r"^\[(\w+)\]$", line)
            if m:
                current = m.group(1)
                parents[current] = None
                ranges[current] = []
                continue
            if line.startswith("extend "):
                parents[current] = line.split()[1]
                continue
            m = _RANGE.match(line)
            if m:
                start = int(m.group(1), 16)
                end = 0xFFFFFFFF if m.group(2) == "*" else int(m.group(2), 16)
                offset = int(m.group(4), 16) * (1 if m.group(3) == "+" else -1)
                ranges[current].append((start, end, offset))
        return cls(parents, ranges)

    def _down(self, addr, version):
        """Parent-version address -> `version` address, or None."""
        for start, end, offset in self.ranges[version]:
            if start <= addr <= end:
                return addr + offset
        return None

    def _up(self, addr, version):
        """`version` address -> parent-version address, or None (inverse of _down)."""
        hits = [a - o for s, e, o in self.ranges[version] for a in [addr] if s <= a - o <= e]
        return hits[0] if len(hits) == 1 else None

    def _chain(self, version):
        chain = [version]
        while self.parents[chain[-1]] is not None:
            chain.append(self.parents[chain[-1]])
        return chain  # version, parent, ..., root

    def convert(self, addr, src, dst):
        if src == dst:
            return addr
        a = addr
        for v in self._chain(src)[:-1]:  # up to the root
            a = self._up(a, v)
            if a is None:
                return None
        for v in reversed(self._chain(dst)[:-1]):  # down from the root
            a = self._down(a, v)
            if a is None:
                return None
        return a
