"""Print the part of an lv_font_conv range that a font can actually draw.

lv_font_conv stops on a range with no glyph at all, and the appearance fonts
lack a few UI characters (arrows, some dashes and combining marks). Those are
drawn by each font's Plex fallback instead.

Usage: python tools/fonts/font-range.py <font.ttf> <range>   e.g. 0x20-0xFF,0x2212
"""

from __future__ import annotations

import sys

from fontTools.ttLib import TTFont


def parse(text: str) -> list[int]:
    codepoints: list[int] = []
    for part in text.split(","):
        first, _, last = part.partition("-")
        start = int(first, 16)
        codepoints.extend(range(start, (int(last, 16) if last else start) + 1))
    return codepoints


def compress(codepoints: list[int]) -> str:
    runs: list[str] = []
    start = previous = None
    for codepoint in codepoints:
        if start is None:
            start = previous = codepoint
        elif codepoint == previous + 1:
            previous = codepoint
        else:
            runs.append(f"0x{start:X}" if start == previous else f"0x{start:X}-0x{previous:X}")
            start = previous = codepoint
    if start is not None:
        runs.append(f"0x{start:X}" if start == previous else f"0x{start:X}-0x{previous:X}")
    return ",".join(runs)


def main() -> int:
    cmap = TTFont(sys.argv[1]).getBestCmap()
    print(compress([codepoint for codepoint in parse(sys.argv[2]) if codepoint in cmap]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
