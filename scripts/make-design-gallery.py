"""Convert genuine SDL/LVGL BMP captures to PNG and build a review contact sheet."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--contact-sheet", type=Path, required=True)
    parser.add_argument("--scenarios", nargs="+", required=True)
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    converted: list[tuple[str, Image.Image]] = []
    for scenario in args.scenarios:
        source = args.source / f"aura-{scenario}.bmp"
        if not source.is_file():
            raise FileNotFoundError(source)
        with Image.open(source) as capture:
            if capture.size != (480, 480):
                raise ValueError(f"{source.name}: expected 480x480, got {capture.size}")
            rgb = capture.convert("RGB")
            rgb.save(args.output / f"aura-{scenario}.png", optimize=True)
            converted.append((scenario, rgb.copy()))

    columns = 4
    thumb_size = 240
    label_height = 30
    rows = (len(converted) + columns - 1) // columns
    sheet = Image.new("RGB", (columns * thumb_size, rows * (thumb_size + label_height)), "#05070B")
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default(size=14)
    for index, (scenario, capture) in enumerate(converted):
        x = (index % columns) * thumb_size
        y = (index // columns) * (thumb_size + label_height)
        sheet.paste(capture.resize((thumb_size, thumb_size), Image.Resampling.LANCZOS), (x, y))
        draw.text((x + 10, y + thumb_size + 7), scenario, fill="#A8B5C7", font=font)

    args.contact_sheet.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(args.contact_sheet, optimize=True)
    print(f"Wrote {len(converted)} 480x480 PNG captures and {args.contact_sheet}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
