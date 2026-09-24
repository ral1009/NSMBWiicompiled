"""Resolve NSMBW_PROFILE_SAMPLER output (build_nsmbw/nsmbw_data/../nsmbw_profile.txt) to function names.

Usage:
  python projects/nsmbw/tools/resolve_profile.py [profile.txt] [--exe build_nsmbw/NSMBWCompiled.exe]
         [--window N | --windows A-B] [--top 40] [--thread TID]

The sampler (runtime/src/product/nsmbw_product.cpp, StartProfileSampler) suspends every thread
~500 times a second, unwinds its stack and writes one 10 s histogram per window:
  <count> tid=<tid> <module>+0x<rva><<module>+0x<rva><...     (leaf frame first)

Symbols: frames in NSMBWCompiled.exe are named with llvm-nm (translated guest functions are
func_<guest address>); frames in Windows DLLs are named by the nearest export (llvm-readobj), which
is exact for exported functions and "export+large offset" for internal ones.

Reports, for the busiest thread (or --thread):
  self       - samples whose leaf frame is that function (where the CPU actually was)
  inclusive  - samples with that function anywhere on the stack (it or its callees)
  callers    - for leaves outside the exe, the first exe frame above them (who called into the DLL)
A thread's "wait" samples are those whose leaf is a kernel wait/sleep/delay entry point.
"""
import argparse, bisect, collections, os, re, subprocess, sys

SYS32 = os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32")
WAIT_RE = re.compile(r"Wait|Delay|Sleep|RemoveIoCompletion|WorkerFactory|GetMessage", re.I)


class Symbols:
    def __init__(self, path, kind):
        rows = []
        if kind == "nm":
            out = subprocess.run(["llvm-nm", "--defined-only", "--no-demangle", path],
                                 capture_output=True, text=True).stdout
            for line in out.splitlines():
                p = line.split()
                if len(p) == 3 and p[1] in ("T", "t"):
                    rows.append((int(p[0], 16) - 0x140000000, p[2]))
        else:
            out = subprocess.run(["llvm-readobj", "--coff-exports", path], capture_output=True, text=True).stdout
            name = None
            for line in out.splitlines():
                line = line.strip()
                if line.startswith("Name:"):
                    name = line.split(None, 1)[1] if len(line.split()) > 1 else None
                elif line.startswith("RVA:") and name:
                    rows.append((int(line.split()[1], 16), name))
        rows.sort()
        self.addrs = [a for a, _ in rows]
        self.names = [n for _, n in rows]

    def lookup(self, rva, with_offset):
        i = bisect.bisect_right(self.addrs, rva) - 1
        if i < 0:
            return "?"
        if with_offset and rva - self.addrs[i] > 0x200:
            return "%s+0x%x" % (self.names[i], rva - self.addrs[i])
        return self.names[i]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("profile", nargs="?", default="build_nsmbw/nsmbw_profile.txt")
    ap.add_argument("--exe", default="build_nsmbw/NSMBWCompiled.exe")
    ap.add_argument("--window", type=int, default=None)
    ap.add_argument("--windows", default=None, help="inclusive range A-B, summed")
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--thread", default=None)
    args = ap.parse_args()

    text = open(args.profile, encoding="utf-8", errors="replace").read()
    windows = re.split(r"^== window ", text, flags=re.M)[1:]
    if not windows:
        sys.exit("no windows in " + args.profile)
    if args.windows:
        a, b = (int(x) for x in args.windows.split("-"))
        chosen = windows[a:b + 1]
    else:
        chosen = [windows[args.window if args.window is not None else -1]]
    for w in chosen:
        print("window", w.splitlines()[0])

    exe_name = os.path.basename(args.exe).lower()
    tables = {}

    def sym(module, rva):
        key = module.lower()
        if key not in tables:
            if key == exe_name:
                tables[key] = Symbols(args.exe, "nm")
            elif os.path.exists(os.path.join(SYS32, module)):
                tables[key] = Symbols(os.path.join(SYS32, module), "exports")
            else:
                tables[key] = None
        t = tables[key]
        if t is None:
            return "%s!0x%x" % (module, rva)
        name = t.lookup(rva, key != exe_name)
        return name if key == exe_name else "%s!%s" % (module.split(".")[0], name)

    names = {}
    busy = collections.Counter(); wait = collections.Counter()
    self_ = collections.defaultdict(collections.Counter)
    incl = collections.defaultdict(collections.Counter)
    callers = collections.defaultdict(collections.Counter)
    for w in chosen:
        for line in w.splitlines()[1:]:
            m = re.match(r'thread (\d+) "(.*)" samples=(\d+)', line)
            if m:
                names[m.group(1)] = m.group(2)
                continue
            m = re.match(r"(\d+) tid=(\d+) (.+)$", line)
            if not m:
                continue
            n, tid = int(m.group(1)), m.group(2)
            frames = []
            for f in m.group(3).split("<"):
                mod, _, rva = f.rpartition("+0x")
                frames.append((mod, int(rva, 16)))
            leaf = sym(*frames[0])
            if WAIT_RE.search(leaf) and frames[0][0].lower() != exe_name:
                wait[tid] += n
                continue
            busy[tid] += n
            self_[tid][leaf] += n
            seen = set()
            for f in frames:
                s = sym(*f)
                if s not in seen:
                    seen.add(s)
                    incl[tid][s] += n
            if frames[0][0].lower() != exe_name:
                caller = next((sym(*f) for f in frames[1:] if f[0].lower() == exe_name), "(no exe frame)")
                callers[tid]["%s  <-  %s" % (leaf, caller)] += n

    print("\nper thread (samples; divide by the window's passes/s for seconds):")
    for tid in sorted(set(busy) | set(wait), key=lambda t: -busy[t])[:12]:
        print("  tid %-6s busy=%-6d wait=%-6d %s" % (tid, busy[tid], wait[tid], names.get(tid, "")))
    tid = args.thread or max(busy, key=busy.get)
    total = busy[tid] or 1
    for title, table in (("self", self_), ("inclusive", incl), ("callers of non-exe leaves", callers)):
        print("\n%s, tid %s (%s busy samples):" % (title, tid, busy[tid]))
        for s, n in table[tid].most_common(args.top):
            print("  %6d %5.1f%%  %s" % (n, 100.0 * n / total, s))


if __name__ == "__main__":
    main()
