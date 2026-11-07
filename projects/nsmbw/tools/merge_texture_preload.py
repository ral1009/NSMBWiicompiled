"""Merge learned HD-pack preload lists into the shipped seed.

    python projects/nsmbw/tools/merge_texture_preload.py \
        projects/nsmbw/assets/textures/initial_texture_preload.txt \
        build_nsmbw/nsmbw_data/Cache/texture_preload.txt [more lists...]

Each line is "<scene>\t<pack file name>" (aurora_texture_preload_scene, texture_replacement.cpp).
The game appends to <cache>/texture_preload.txt every pack file a scene loads on demand; merging
those into the seed lets a first launch load them while the screen is black instead of mid-scene.
Lines are kept in first-seen order (the order the game first needed them) and deduplicated.
"""
import sys


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    seed, sources = sys.argv[1], sys.argv[1:]
    lines, seen = [], set()
    for path in sources:
        try:
            with open(path, encoding="utf-8") as f:
                for line in f:
                    line = line.rstrip("\r\n")
                    if "\t" in line and line not in seen:
                        seen.add(line)
                        lines.append(line)
        except FileNotFoundError:
            print(f"skipping missing {path}")
    with open(seed, "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(l + "\n" for l in lines))
    scenes = {l.split("\t", 1)[0] for l in lines}
    print(f"{seed}: {len(lines)} files across {len(scenes)} scenes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
