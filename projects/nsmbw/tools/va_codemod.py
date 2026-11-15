"""Route the guest addresses in our NSMBW-specific C++ through the address layer (nsmbw_va.h).

Usage:
  python projects/nsmbw/tools/va_codemod.py [--check] [files...]
  python projects/nsmbw/tools/va_codemod.py --shared [--check]
With no files: projects/nsmbw/native/*.cpp|*.h and runtime/src/product/nsmbw_*.cpp|*.h.
--shared: the shared runtime (runtime/src and runtime/include, minus product/ and files NSMBW does not
compile). Every game-memory literal there is mapped too, even Mario Kart Wii leftovers: for PAL the
macro is the identity, and for another version the address moves to the matching location, so that
version behaves exactly as PAL does. func_ names are NOT rewritten there: the shared runtime calls
Mario Kart stubs by their raw names (nsmbw_runtime_shims.cpp). Low memory below 0x80004000 (OS
globals, the same in every game) is left alone.

Rewrites, outside comments, strings and character literals:
  0x8042A228u  ->  NSMBW_VA(0x8042A228u)      any 8-digit literal in 0x80000000-0x817FFFFF
  func_8077AB70 ->  NSMBW_FUNC(8077AB70)       a translated function, by name
  r13 + -20424  ->  r13 + -NSMBW_SDA13(20424) a small global below r13 (same for r2: NSMBW_SDA2)
  r13 - 20352u  ->  r13 - NSMBW_SDA13(20352u)  the same, as hand-written code spells it
Leaves alone: literals already inside NSMBW_VA(...), and the first argument of the hook macros
(PPC_NATIVE_OVERRIDE[_VOID], REGISTER_NATIVE_FUNCTION[_AS], REGISTER_TRANSLATED_FUNCTION,
MKW_KNOWN_TYPED_NATIVE, MKW_KNOWN_NATIVE_CPU_CALL, GX_FATAL_STUB), which map their own argument.
A literal or func_ name followed by the comment `// nsmbw-va: not an address` on the same line
is left alone.

It is idempotent. --check changes nothing and exits 1 if anything would change (for a test).

Top halves of two-step addresses (copied translated code: `r3 = 0x80430000u; ... r3 + -23776`):
a statement `rN = 0xXXXX0000u;` is followed to the low halves added to rN before rN is assigned
again. One low half (or several that all need the same top half) -> NSMBW_VA_PAIR(0xXXXX0000u, lo),
which is exact (nsmbw_va.h). Otherwise, or with no low half found, it is wrapped as a plain
NSMBW_VA and reported when the +-32 KB window around it crosses a range of the address map
("check by hand"): only inside one range does the top half alone map correctly for any low half.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from nsmbw_address_map import AddressMap  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]
HOOK_MACROS = ("PPC_NATIVE_OVERRIDE_VOID", "PPC_NATIVE_OVERRIDE", "REGISTER_NATIVE_FUNCTION_AS",
               "REGISTER_NATIVE_FUNCTION", "REGISTER_TRANSLATED_FUNCTION", "MKW_KNOWN_TYPED_NATIVE",
               "MKW_KNOWN_NATIVE_CPU_CALL", "GX_FATAL_STUB")
LITERAL = re.compile(r"\b0[xX](8[01][0-9A-Fa-f]{6})([uU]?)\b")
FUNC = re.compile(r"\bfunc_([0-9A-Fa-f]{8})\b")
NOT_ADDRESS = "nsmbw-va: not an address"
SDA_ACCESS = re.compile(r"\b(r13|r2|ctx->gpr\[13\]|ctx->gpr\[2\])\s*\+\s*-(\d+)\b")
SDA_MINUS = re.compile(r"\b(r13|r2|ctx->gpr\[13\]|ctx->gpr\[2\]|cpu->gpr\[13\]|cpu->gpr\[2\])\s*-\s*((?:0[xX][0-9A-Fa-f]+|\d+)[uU]?)(?![\w(])")
USES_MACROS = re.compile(r"\bNSMBW_(?:VA|VA_PAIR|VA_HOOK|FUNC|SDA13|SDA2|TOKEN|BY_VERSION)\(")


def code_spans(text):
    """Yield (start, end) of text that is code: not a comment, string or char literal."""
    i, n, start = 0, len(text), 0
    while i < n:
        c = text[i]
        two = text[i:i + 2]
        if two == "//" or two == "/*" or c in "\"'":
            if start < i:
                yield start, i
            if two == "//":
                j = text.find("\n", i)
                i = n if j < 0 else j
            elif two == "/*":
                j = text.find("*/", i + 2)
                i = n if j < 0 else j + 2
            else:
                if c == '"' and i > 0 and text[i - 1] == "R":  # raw string R"(...)"
                    j = text.find(')"', i)
                    i = n if j < 0 else j + 2
                else:
                    j = i + 1
                    while j < n and text[j] != c:
                        j += 2 if text[j] == "\\" else 1
                    i = j + 1
            start = i
        else:
            i += 1
    if start < n:
        yield start, n


def first_macro_arg_ranges(text):
    """(start, end) of the first argument of each hook macro use."""
    out = []
    for m in re.finditer(r"\b(" + "|".join(HOOK_MACROS) + r")\s*\(", text):
        j = m.end()
        k = j
        while k < len(text) and text[k] not in ",)":
            k += 1
        out.append((j, k))
    return out


PAIR_STMT = re.compile(r"^(\s*)(r\d+|ctx->gpr\[\d+\])\s*=\s*0[xX](8[01][0-9A-Fa-f]{2}0000)([uU]?);")


def window_in_one_range(value, amap):
    lo, hi = max(value - 0x8000, 0x80000000), min(value + 0x7FFF, 0x817FFFFF)
    offsets = {None if amap.convert(a, "P1", "E1") is None else amap.convert(a, "P1", "E1") - a
               for a in list(range(lo, hi + 1, 0x10)) + [hi]}
    return len(offsets) == 1 and None not in offsets


def pair_pass(text, amap, report, path):
    lines = text.split("\n")
    for i, line in enumerate(lines):
        m = PAIR_STMT.match(line)
        if not m:
            continue
        reg, hi = m.group(2), int(m.group(3), 16)
        use = re.compile(re.escape(reg) + r"\s*\+\s*(-?(?:0[xX][0-9A-Fa-f]+|\d+))\b")
        kill = re.compile(r"^\s*" + re.escape(reg) + r"\s*=[^=]")
        los = []
        for j in range(i + 1, min(i + 40, len(lines))):
            los += [int(x, 0) for x in use.findall(lines[j])]
            if kill.match(lines[j]):
                break
        literal = "0x%s%s" % (m.group(3), m.group(4))
        # A pair whose full value is outside game memory builds a constant, not an address
        # (`0x80000000u` + -2 = 0x7FFFFFFE in the EXI code): no pair, leave it to the plain pass.
        if any(not 0x80000000 <= ((hi + lo) & 0xFFFFFFFF) < 0x81800000 for lo in los):
            continue
        tops = {(amap.convert((hi + lo) & 0xFFFFFFFF, "P1", "E1") or 0) - lo for lo in los}
        if los and len(tops) == 1 and 0 not in [amap.convert((hi + lo) & 0xFFFFFFFF, "P1", "E1") for lo in los]:
            lines[i] = line.replace(literal, "NSMBW_VA_PAIR(%s, %d)" % (literal, los[0]), 1)
        elif los:
            report.append("%s:%d  %s: low halves %s need different top halves: check by hand" % (path, i + 1, literal, sorted(set(los))))
        elif not window_in_one_range(hi, amap):
            report.append("%s:%d  %s: no low half found nearby and the window crosses a range: check by hand" % (path, i + 1, literal))
    return "\n".join(lines)


def rewrite(text, amap, report, path, shared=False):
    text = pair_pass(text, amap, report, path)
    skip = first_macro_arg_ranges(text)
    pieces, last = [], 0
    for s, e in code_spans(text):
        pieces.append(text[last:s])
        chunk = text[s:e]

        def lit(m, base=s):
            pos = base + m.start()
            if any(a <= pos < b for a, b in skip):
                return m.group(0)
            before = text[max(0, pos - 9):pos]
            if before.endswith("NSMBW_VA(") or before.endswith("_VA_HOOK(") or before.endswith("_VA_PAIR("):
                return m.group(0)
            line_end = text.find("\n", pos)
            if NOT_ADDRESS in text[pos:line_end if line_end >= 0 else len(text)]:
                return m.group(0)
            value = int(m.group(1), 16)
            if value >= 0x81800000 or (shared and value < 0x80004000):
                return m.group(0)
            if value & 0xFFFF == 0 and not window_in_one_range(value, amap):
                line = text.count("\n", 0, pos) + 1
                report.append("%s:%d  0x%08X: top half outside a pair statement, window crosses a range: check by hand"
                              % (path, line, value))
            return "NSMBW_VA(%s)" % m.group(0)

        chunk = LITERAL.sub(lit, chunk)
        def fn(m, base=s):
            pos = base + m.start()
            line_end = text.find("\n", pos)
            if NOT_ADDRESS in text[pos:line_end if line_end >= 0 else len(text)]:
                return m.group(0)  # e.g. Mario Kart Wii stub names the shared runtime calls
            return "NSMBW_FUNC(%s)" % m.group(1)

        if not shared:
            chunk = FUNC.sub(fn, chunk)
        # Copied code reaches small globals relative to r13 / r2 (`r13 + -20424`). Those registers
        # moved by a different amount than the globals between versions, so the offset is mapped.
        chunk = SDA_ACCESS.sub(lambda m: "%s + -NSMBW_SDA%s(%s)" % (m.group(1), "13" if "13" in m.group(1) else "2", m.group(2)), chunk)
        # The hand-written form, `r13 - 20352u` / `ctx->gpr[13] - 0x4F80u` (nsmbw_os_init_stm.cpp: its
        # PAL offset landed on USA's scheduler-disable count and the game exited at boot).
        chunk = SDA_MINUS.sub(lambda m: "%s - NSMBW_SDA%s(%s)" % (m.group(1), "13" if "13" in m.group(1) else "2", m.group(2)), chunk)
        pieces.append(chunk)
        last = e
    pieces.append(text[last:])
    out = "".join(pieces)
    # Every file that uses the macros includes their header itself (some files reach the hook
    # macros another way, or not at all).
    if USES_MACROS.search(out) and '#include "nsmbw_va.h"' not in out:
        i = out.find("#include")
        nl = "\r\n" if "\r\n" in out else "\n"
        line = '#include "nsmbw_va.h"  // NSMBW_VA / NSMBW_FUNC: guest addresses are PAL rev 1, mapped per version' + nl
        out = out[:i] + line + out[i:] if i >= 0 else line + out
    return out


def default_files():
    files = sorted((ROOT / "projects" / "nsmbw" / "native").glob("*.cpp"))
    files += sorted((ROOT / "projects" / "nsmbw" / "native").glob("*.h"))
    files += sorted((ROOT / "runtime" / "src" / "product").glob("nsmbw_*.cpp"))
    files += sorted((ROOT / "runtime" / "src" / "product").glob("nsmbw_*.h"))
    return files


SHARED_SKIP = {"main.cpp", "base_product.cpp", "retro_rewind_product.cpp", "music_attenuation.cpp",
               "ax_effects.cpp", "nsmbw_va.h", "host_cpu_baseline.cpp",
               # their macros map their own address arguments; the .inc must not include anything
               "native_cpu_calls.inc", "hle_stubs.h"}


def shared_files():
    out = []
    for base, exts in ((ROOT / "runtime" / "src", (".cpp", ".h", ".inc")), (ROOT / "runtime" / "include", (".h", ".inc", ".hpp"))):
        for f in sorted(base.rglob("*")):
            if f.suffix in exts and f.name not in SHARED_SKIP and "product" not in f.relative_to(ROOT).parts:
                out.append(f)
    return out


def main():
    check = "--check" in sys.argv
    shared = "--shared" in sys.argv
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    files = [Path(a) for a in args] or (shared_files() if shared else default_files())
    amap = AddressMap.load()
    report, changed = [], []
    for f in files:
        raw = f.read_bytes().decode("utf-8")
        new = rewrite(raw, amap, report, f.relative_to(ROOT) if f.is_absolute() else f, shared)
        if new != raw:
            changed.append((f, raw.count("NSMBW_VA(") , new.count("NSMBW_VA("), raw.count("NSMBW_FUNC("), new.count("NSMBW_FUNC(")))
            if not check:
                f.write_bytes(new.encode("utf-8"))
    for f, va0, va1, fn0, fn1 in changed:
        print("%-60s NSMBW_VA %4d -> %4d   NSMBW_FUNC %3d -> %3d" % (f.name, va0, va1, fn0, fn1))
    print("%d file(s) %s" % (len(changed), "would change" if check else "changed"))
    for r in report:
        print(r)
    if check and changed:
        sys.exit(1)


if __name__ == "__main__":
    main()
