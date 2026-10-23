"""Create the deterministic AURA v4 screenshot and reference-comparison galleries."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


SCREENS = (
    ("01", "home"), ("02", "listening"), ("03", "thinking"),
    ("04", "speaking"), ("05", "quick-actions"), ("06", "notifications"),
    ("07", "aoraton"), ("08", "navigation"), ("09", "search"),
    ("10", "weather"), ("11", "settings"), ("12", "audio"),
    ("13", "tools"), ("14", "calendar"), ("15", "camera"),
    ("16", "offline"), ("17", "battery"), ("18", "voice"),
)


def fit(image: Image.Image, size: tuple[int, int], background: str = "#000105") -> Image.Image:
    canvas = Image.new("RGB", size, background)
    copy = image.convert("RGB")
    scale = min(size[0] / copy.width, size[1] / copy.height)
    copy = copy.resize((round(copy.width * scale), round(copy.height * scale)), Image.Resampling.LANCZOS)
    canvas.paste(copy, ((size[0] - copy.width) // 2, (size[1] - copy.height) // 2))
    return canvas


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--references", type=Path, required=True)
    parser.add_argument("--contact-sheet", type=Path, required=True)
    parser.add_argument("--comparison-sheet", type=Path, required=True)
    parser.add_argument("--comparisons", type=Path, required=True)
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    args.comparisons.mkdir(parents=True, exist_ok=True)
    font = ImageFont.load_default(size=15)
    captures: list[tuple[str, str, Image.Image]] = []
    pairs: list[tuple[str, str, Image.Image]] = []

    for number, name in SCREENS:
        source = args.source / f"aura-{name}.bmp"
        reference = args.references / f"{number}-{name}.png"
        if not source.is_file():
            raise FileNotFoundError(source)
        if not reference.is_file():
            raise FileNotFoundError(reference)
        with Image.open(source) as loaded:
            if loaded.size != (480, 480):
                raise ValueError(f"{source.name}: expected 480x480, got {loaded.size}")
            capture = loaded.convert("RGB")
        output_name = f"{number}-{name}.png"
        capture.save(args.output / output_name, optimize=True)
        captures.append((number, name, capture.copy()))

        with Image.open(reference) as loaded_reference:
            concept = loaded_reference.convert("RGB").crop((0, 0, loaded_reference.width, min(286, loaded_reference.height)))
            target = fit(concept, (420, 480))
        comparison = Image.new("RGB", (920, 530), "#030712")
        comparison.paste(target, (10, 40))
        comparison.paste(capture, (440, 40))
        draw = ImageDraw.Draw(comparison)
        draw.text((20, 12), f"{number} {name} · TARGET", fill="#8295B0", font=font)
        draw.text((450, 12), "ACTUAL LVGL 480x480", fill="#25D6FF", font=font)
        comparison.save(args.comparisons / output_name, optimize=True)
        pairs.append((number, name, comparison))

    thumb = 240
    label_height = 28
    contact = Image.new("RGB", (6 * thumb, 3 * (thumb + label_height)), "#000105")
    draw = ImageDraw.Draw(contact)
    for index, (number, name, capture) in enumerate(captures):
        x = (index % 6) * thumb
        y = (index // 6) * (thumb + label_height)
        contact.paste(capture.resize((thumb, thumb), Image.Resampling.LANCZOS), (x, y))
        draw.text((x + 8, y + thumb + 6), f"{number} {name}", fill="#BACBE3", font=font)
    args.contact_sheet.parent.mkdir(parents=True, exist_ok=True)
    contact.save(args.contact_sheet, optimize=True)

    pair_width, pair_height = 460, 286
    comparison_sheet = Image.new("RGB", (pair_width * 2, pair_height * 9), "#000105")
    draw = ImageDraw.Draw(comparison_sheet)
    for index, (number, name, pair) in enumerate(pairs):
        x = (index % 2) * pair_width
        y = (index // 2) * pair_height
        thumbnail = pair.resize((460, 265), Image.Resampling.LANCZOS)
        comparison_sheet.paste(thumbnail, (x, y))
        draw.text((x + 8, y + 266), f"{number} {name}", fill="#BACBE3", font=font)
    args.comparison_sheet.parent.mkdir(parents=True, exist_ok=True)
    comparison_sheet.save(args.comparison_sheet, optimize=True)
    print(f"Wrote {len(captures)} deterministic 480x480 captures and comparisons")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
