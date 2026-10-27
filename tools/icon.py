# SPDX-License-Identifier: MIT
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow"]
# ///
"""Makes the Homebrew Channel icon from a PostHog illustration.

The Homebrew Channel shows a 128x48 PNG next to each app. The illustration is
vector art published as a 1000-pixel PNG. This tool scales it down to fit,
places it on the game's page color, and changes nothing else.

Usage: icon.py <illustration png> <output png>
"""

import sys
from pathlib import Path

from PIL import Image

SIZE = (128, 48)
MARGIN = 2
PAGE = (0xF3, 0xF4, 0xEF, 0xFF)


def main(source: Path, output: Path) -> None:
    """Write the icon for the illustration at source to output."""
    art = Image.open(source).convert("RGBA")
    art = art.crop(art.getbbox())
    art.thumbnail((SIZE[0] - 2 * MARGIN, SIZE[1] - 2 * MARGIN), Image.Resampling.LANCZOS)

    icon = Image.new("RGBA", SIZE, PAGE)
    icon.alpha_composite(art, ((SIZE[0] - art.width) // 2, (SIZE[1] - art.height) // 2))
    output.parent.mkdir(parents=True, exist_ok=True)
    icon.convert("RGB").save(output)


if __name__ == "__main__":
    main(Path(sys.argv[1]), Path(sys.argv[2]))
