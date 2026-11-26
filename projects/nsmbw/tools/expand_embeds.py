"""Expand #embed directives for Clang versions that do not support them."""
from pathlib import Path
import re
import sys


def expand(source, destination):
    def embed(match):
        data = (source.parent / match.group(1)).read_bytes()
        return "\n".join(
            ",".join(str(byte) for byte in data[offset:offset + 24]) + ","
            for offset in range(0, len(data), 24))

    text = re.sub(r'^\s*#embed\s+"([^"]+)"\s*$', embed, source.read_text(), flags=re.MULTILINE)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(text)


if __name__ == "__main__":
    expand(Path(sys.argv[1]).resolve(), Path(sys.argv[2]))
