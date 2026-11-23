import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class EmbedTests(unittest.TestCase):
    def test_binary_bytes_and_relative_paths_survive_expansion(self):
        with tempfile.TemporaryDirectory(prefix="nsmbw embed ") as directory:
            root = Path(directory)
            (root / "assets").mkdir()
            (root / "assets/glyph.bin").write_bytes(bytes((0, 127, 255)))
            source = root / "glyph.cpp"
            source.write_text('constexpr unsigned char data[] = {\n#embed "assets/glyph.bin"\n};\n'
                              'static_assert(sizeof(data) == 3);\n'
                              'static_assert(data[0] == 0 && data[1] == 127 && data[2] == 255);\n'
                              'int main() {}\n')
            output = root / "generated/glyph.cpp"
            subprocess.run([sys.executable, Path(__file__).with_name("expand_embeds.py"), source, output],
                           cwd="/tmp", check=True)
            subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++17", output, "-o", root / "glyph"],
                           check=True)
            subprocess.run([root / "glyph"], check=True)


if __name__ == "__main__":
    unittest.main()
