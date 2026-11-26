"""Run against the launcher built by runtime/tests/linux/CMakeLists.txt."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

BUILD = Path(sys.argv.pop(1)).resolve()


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="nsmbw launcher ")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.launcher = self.bin / "NSMBWCompiled"
        shutil.copy2(BUILD / "NSMBWCompiled", self.launcher)
        self.env = {k: v for k, v in os.environ.items()
                    if not k.startswith(("NSMBW_", "MKW_"))}
        self.env.update(XDG_DATA_HOME=str(self.root / "data"), SDL_VIDEODRIVER="dummy")

    def disc(self, game_id=b"SMNP01", revision=1):
        root = self.root / "disc with spaces" / "DATA"
        (root / "sys").mkdir(parents=True)
        (root / "files").mkdir()
        (root / "sys" / "boot.bin").write_bytes(game_id + bytes((0, revision)))
        (root / "sys" / "fst.bin").write_bytes(b"fixture")
        return root

    def library(self, name):
        shutil.copy2(BUILD / "test_game.so", self.bin / name)

    def run_game(self, *args):
        return subprocess.run([self.launcher, *map(str, args)], cwd=self.root,
                              env=self.env, capture_output=True, text=True, timeout=15)

    def test_pal_folder_is_saved_and_reused_from_another_working_directory(self):
        disc = self.disc()
        self.library("NSMBWCompiled-P1-compat.so")
        result = self.run_game("--disc", disc.parent)
        self.assertEqual(result.returncode, 37, result.stderr)
        config = (self.root / "data/WiiCompiled/Config.toml").read_text()
        self.assertIn(str(disc), config)
        self.assertEqual(self.run_game().returncode, 37)

    def test_usa_disc_selects_the_usa_library(self):
        self.disc(b"SMNE01")
        self.library("NSMBWCompiled-E1-compat.so")
        result = self.run_game("--disc", self.root / "disc with spaces")
        self.assertEqual(result.returncode, 37, result.stderr)

    def test_wrong_revision_does_not_replace_a_saved_disc(self):
        disc = self.disc(revision=2)
        result = self.run_game("--disc", disc)
        self.assertEqual(result.returncode, 1)
        self.assertIn("revision", result.stderr)
        config = self.root / "data/WiiCompiled/Config.toml"
        self.assertTrue(not config.exists() or str(disc) not in config.read_text())

    def test_truncated_disc_header_is_rejected(self):
        disc = self.disc()
        (disc / "sys/boot.bin").write_bytes(b"SMN")
        result = self.run_game("--disc", disc)
        self.assertEqual(result.returncode, 1)
        self.assertIn("header", result.stderr)

    def test_missing_game_library_is_an_error(self):
        result = self.run_game("--disc", self.disc())
        self.assertEqual(result.returncode, 1)
        self.assertIn("missing", result.stderr.lower())

    def test_forced_compat_does_not_load_the_fast_library(self):
        self.env["MKW_FORCE_UNSUPPORTED_CPU"] = "1"
        self.library("NSMBWCompiled-P1.so")
        result = self.run_game("--disc", self.disc())
        self.assertEqual(result.returncode, 1)
        self.assertIn("compat", result.stderr)

    def test_forced_compat_loads_compat_and_clears_the_override(self):
        self.env["MKW_FORCE_UNSUPPORTED_CPU"] = "1"
        self.library("NSMBWCompiled-P1-compat.so")
        result = self.run_game("--disc", self.disc())
        self.assertEqual(result.returncode, 37, result.stderr)

    def test_unknown_option_is_rejected(self):
        self.assertEqual(self.run_game("--unknown").returncode, 1)

    def test_environment_folder_hook_selects_a_disc(self):
        self.env["NSMBW_SETUP_FOLDER"] = str(self.disc())
        self.library("NSMBWCompiled-P1-compat.so")
        result = self.run_game()
        self.assertEqual(result.returncode, 37, result.stderr)

    def test_image_import_is_permanent_and_a_failed_replacement_is_discarded(self):
        import shlex
        disc = self.disc(b"SMNE01")
        self.library("NSMBWCompiled-E1-compat.so")
        image = self.root / "Mario's game ($1).wbfs"
        image.touch()
        tool = self.bin / "nodtool"
        tool.write_text("#!/bin/sh\nset -eu\n"
                        'test "$1" = extract && test "$2" = -q\n'
                        f'test "$3" = {shlex.quote(str(image))}\n'
                        f'cp -R {shlex.quote(str(disc))} "$4"\n')
        tool.chmod(0o755)
        self.env["NSMBW_SETUP_IMAGE"] = str(image)
        result = self.run_game()
        self.assertEqual(result.returncode, 37, result.stderr)
        destination = self.root / "data/WiiCompiled/discs/E1"
        self.assertEqual((destination / "sys/boot.bin").read_bytes(), b"SMNE01\x00\x01")
        saved = (self.root / "data/WiiCompiled/Config.toml").read_bytes()
        self.assertIn(os.fsencode(destination), saved)

        (disc / "sys/boot.bin").write_bytes(b"SMNE01\x00\x02")
        result = self.run_game("--disc", image)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("revision 2", result.stderr)
        self.assertEqual((destination / "sys/boot.bin").read_bytes(), b"SMNE01\x00\x01")
        self.assertEqual((self.root / "data/WiiCompiled/Config.toml").read_bytes(), saved)
        self.assertEqual(list(destination.parent.glob(".import-*")), [])
        tool.write_text("#!/bin/sh\nexit 1\n")
        self.assertIn("Could not extract", self.run_game("--disc", image).stderr)
        del self.env["NSMBW_SETUP_IMAGE"]
        image.unlink()
        self.assertEqual(self.run_game().returncode, 37)

    def test_texture_pack_is_copied_and_enabled(self):
        disc = self.disc()
        self.library("NSMBWCompiled-P1-compat.so")
        pack = self.root / "HD pack"
        smn = pack / "Load/Textures/SMN"
        smn.mkdir(parents=True)
        (smn / "grass.png").write_bytes(b"texture")
        self.env["NSMBW_SETUP_TEXTURES"] = str(pack)
        result = self.run_game("--disc", disc)
        self.assertEqual(result.returncode, 37, result.stderr)
        data = self.root / "data/WiiCompiled"
        installed = data / "nsmbw_data/texture_replacements/SMN"
        self.assertEqual((installed / "grass.png").read_bytes(), b"texture")
        self.assertIn("texture_replacements = true", (data / "Config.toml").read_text())
        (smn / "grass.png").unlink()
        (smn / "sky.png").write_bytes(b"sky")
        result = self.run_game("--disc", disc)
        self.assertEqual(result.returncode, 37, result.stderr)
        self.assertTrue((installed / "grass.png").is_file())
        self.assertEqual((installed / "sky.png").read_bytes(), b"sky")

    def test_invalid_shared_library_reports_a_load_error(self):
        (self.bin / "NSMBWCompiled-P1-compat.so").write_text("not an ELF library")
        result = self.run_game("--disc", self.disc())
        self.assertEqual(result.returncode, 1)
        self.assertIn("Could not load", result.stderr)

    def test_packaged_launcher_runs_without_copying_disc_or_user_data(self):
        self.library("NSMBWCompiled-P1-compat.so")
        root = Path(__file__).resolve().parents[3]
        for name in ("dsp_coef.bin", "initial_pipeline_cache.db", "initial_texture_preload.txt",
                     "nodtool", "nodtool-LICENSE"):
            (self.bin / name).write_bytes(b"seed")
        bootstrap = self.bin / "wii_bootstrap/shared2/wc24"
        bootstrap.mkdir(parents=True)
        (bootstrap / "seed").write_bytes(b"bootstrap")
        (self.bin / "disc").mkdir()
        (self.bin / "disc/private-file").write_text("disc contents")
        (self.bin / "portable.txt").touch()
        output = self.root / "package"
        package = subprocess.run([
            sys.executable, root / "projects/nsmbw/tools/package_nsmbw_linux.py", self.bin,
            "--output-dir", output, "--appdir-only"], capture_output=True, text=True)
        self.assertEqual(package.returncode, 0, package.stderr)
        appdir = output / "NSMBWCompiled.AppDir"
        self.assertTrue((appdir / "usr/bin/nodtool").is_file())
        self.assertTrue((appdir / "usr/share/licenses/nsmbwcompiled/nodtool-LICENSE").is_file())
        self.assertFalse((appdir / "usr/bin/disc").exists())
        self.assertFalse((appdir / "usr/bin/portable.txt").exists())
        self.assertTrue((appdir / "usr/share/licenses/nsmbwcompiled/LICENSE").is_file())
        self.assertTrue((appdir / "usr/bin/wii_bootstrap/shared2/wc24/seed").is_file())
        result = subprocess.run([appdir / "AppRun", "--disc", self.disc()],
                                cwd=self.root, env=self.env, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 37, result.stderr)

    def test_package_rejects_missing_runtime_files(self):
        root = Path(__file__).resolve().parents[3]
        result = subprocess.run([
            sys.executable, root / "projects/nsmbw/tools/package_nsmbw_linux.py", self.bin,
            "--output-dir", self.root / "package", "--appdir-only"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("Missing runtime file", result.stderr)
        self.assertFalse((self.root / "package/NSMBWCompiled.AppDir").exists())


if __name__ == "__main__":
    unittest.main()
