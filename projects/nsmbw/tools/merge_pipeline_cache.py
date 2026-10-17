"""Merge aurora pipeline caches into the seed NSMBW ships as initial_pipeline_cache.db.

A pipeline_cache.db holds pipeline *recipes* (the GX state each shader was built from), not
compiled GPU code, so it is GPU- and driver-independent. aurora merges a bundled seed next to the
exe into the player's cache at boot and compiles every recipe on background threads, so a first
launch does not stall mid-game compiling a shader the first time it is drawn (3.1 s freeze on the
first world-map visit on an Iris Xe, 2026-10-16).

  python projects/nsmbw/tools/merge_pipeline_cache.py projects/nsmbw/assets/initial_pipeline_cache.db \
      <path/to/nsmbw_data/Cache/pipeline_cache.db> [...]

The output is merged into, not replaced, so contributing a new play session's cache only adds
recipes. Rows whose config_version differs from --version (aurora's GXPipelineConfigVersion, in
aurora-main/lib/gx/pipeline.hpp) are dropped: aurora prunes them anyway, and a version bump means
the seed has to be re-collected.
"""
import argparse
import os
import sqlite3

SCHEMA = 1  # aurora PipelineCacheSchema (aurora-main/lib/gfx/pipeline_cache.cpp)
GX_TYPE = 1  # ShaderType::GX; type 0 is the clear pipelines


def open_output(path: str) -> sqlite3.Connection:
    db = sqlite3.connect(path)
    db.executescript(
        f"""
        CREATE TABLE IF NOT EXISTS aurora_schema(value INTEGER);
        CREATE TABLE IF NOT EXISTS pipeline_cache (
          type INTEGER NOT NULL,
          hash INTEGER NOT NULL,
          config_version INTEGER NOT NULL,
          config_size INTEGER NOT NULL,
          config BLOB NOT NULL,
          first_frame_used INTEGER NOT NULL,
          PRIMARY KEY (type, hash)
        );
        CREATE INDEX IF NOT EXISTS pipeline_cache_load_order_idx
          ON pipeline_cache(type, config_version, first_frame_used);
        DELETE FROM aurora_schema;
        INSERT INTO aurora_schema VALUES ({SCHEMA});
        """
    )
    return db


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output")
    parser.add_argument("inputs", nargs="+")
    parser.add_argument("--version", type=int, default=19, help="GXPipelineConfigVersion to keep (default 19)")
    args = parser.parse_args()

    out = open_output(args.output)
    before = out.execute("SELECT COUNT(*) FROM pipeline_cache").fetchone()[0]
    for path in args.inputs:
        # Read-only URI: a running game's cache (and its -wal) must not be touched.
        src = sqlite3.connect(f"file:{os.path.abspath(path)}?mode=ro", uri=True)
        rows = src.execute(
            "SELECT type, hash, config_version, config_size, config, first_frame_used FROM pipeline_cache"
        ).fetchall()
        src.close()
        kept = [r for r in rows if r[0] != GX_TYPE or r[2] == args.version]
        out.executemany(
            "INSERT INTO pipeline_cache VALUES (?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(type, hash) DO UPDATE SET "
            "first_frame_used = MIN(first_frame_used, excluded.first_frame_used)",
            kept,
        )
        print(f"{path}: {len(kept)} of {len(rows)} rows")
    out.commit()
    after = out.execute("SELECT type, COUNT(*) FROM pipeline_cache GROUP BY type").fetchall()
    total = sum(n for _, n in after)
    print(f"{args.output}: {before} -> {total} rows {dict(after)}")
    out.execute("VACUUM")
    out.close()


if __name__ == "__main__":
    main()
