"""Package a native Linux build without disc contents or user data."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
GAME_LIBRARIES = (
    "NSMBWCompiled-P1.so", "NSMBWCompiled-P1-compat.so",
    "NSMBWCompiled-E1.so", "NSMBWCompiled-E1-compat.so",
)
RUNTIME_FILES = ("dsp_coef.bin", "initial_pipeline_cache.db", "initial_texture_preload.txt")


def stage(build, appdir):
    required = [build / "NSMBWCompiled", build / "nodtool", build / "nodtool-LICENSE", *(build / name for name in RUNTIME_FILES),
                build / "wii_bootstrap/shared2/wc24"]
    for path in required:
        if not path.exists():
            raise ValueError(f"Missing runtime file: {path}")
    games = [build / name for name in GAME_LIBRARIES if (build / name).is_file()]
    if not games:
        raise ValueError("No NSMBW game libraries found in the build directory")
    binary_dir = appdir / "usr/bin"
    binary_dir.mkdir(parents=True)
    for path in [build / "NSMBWCompiled", build / "nodtool", *games, *(build / name for name in RUNTIME_FILES)]:
        shutil.copy2(path, binary_dir / path.name)
    shutil.copytree(build / "wii_bootstrap", binary_dir / "wii_bootstrap")
    resources = ROOT / "projects/nsmbw/linux"
    shutil.copy2(resources / "AppRun", appdir / "AppRun")
    (appdir / "AppRun").chmod(0o755)
    shutil.copy2(resources / "nsmbwcompiled.desktop", appdir)
    shutil.copy2(resources / "nsmbwcompiled.svg", appdir)
    licenses = appdir / "usr/share/licenses/nsmbwcompiled"
    licenses.mkdir(parents=True)
    shutil.copy2(build / "nodtool-LICENSE", licenses)
    for name in ("LICENSE", "THIRD-PARTY-NOTICES.md"):
        shutil.copy2(ROOT / name, licenses)
    return [binary_dir / path.name for path in games]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist")
    parser.add_argument("--linuxdeploy", default="linuxdeploy")
    parser.add_argument("--appdir-only", action="store_true", help="Stage files without bundling dependencies")
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if args.appdir_only and (output / "NSMBWCompiled.AppDir").exists():
        parser.error("NSMBWCompiled.AppDir already exists; choose a different output directory")
    try:
        with tempfile.TemporaryDirectory(prefix="nsmbw-package-", dir=output) as temporary:
            appdir = Path(temporary) / "NSMBWCompiled.AppDir"
            games = stage(args.build.resolve(), appdir)
            if args.appdir_only:
                shutil.move(appdir, output / appdir.name)
                print(output / appdir.name)
                return
            command = [args.linuxdeploy, "--appdir", str(appdir),
                       "--executable", str(appdir / "usr/bin/NSMBWCompiled"),
                       "--desktop-file", str(appdir / "nsmbwcompiled.desktop"),
                       "--icon-file", str(appdir / "nsmbwcompiled.svg")]
            for game in games:
                command.extend(("--deploy-deps-only", str(game)))
            command.extend(("--output", "appimage"))
            environment = dict(os.environ, APPIMAGE_EXTRACT_AND_RUN="1", ARCH="x86_64",
                               LDAI_OUTPUT=str(output / "NSMBWCompiled-x86_64.AppImage"))
            subprocess.run(command, check=True, cwd=output, env=environment)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
