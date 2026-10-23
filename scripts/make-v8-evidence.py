"""Build and validate AURA v8 screenshots, hero motion and pointer evidence."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont


SCREENS = (
    ("01", "home"), ("02", "listening"), ("03", "thinking"),
    ("04", "speaking"), ("05", "answer"), ("06", "answer-long"),
    ("07", "quick-actions"), ("08", "notifications"), ("09", "settings"),
    ("10", "audio"), ("11", "aoraton"), ("12", "aoraton-chat"),
    ("13", "aoraton-compose"), ("14", "aoraton-confirm"),
    ("15", "offline"), ("16", "error"), ("17", "battery"),
    ("18", "diagnostics"),
)
MOTION_STATES = ("home", "listening", "thinking", "speaking")
HERO_BOXES = {
    "home": (72, 52, 408, 286),
    "listening": (34, 52, 446, 286),
    "thinking": (92, 52, 388, 286),
    "speaking": (92, 52, 388, 286),
}


def require_480(path: Path) -> Image.Image:
    if not path.is_file():
        raise FileNotFoundError(path)
    with Image.open(path) as loaded:
        if loaded.size != (480, 480):
            raise ValueError(f"{path}: expected 480x480, got {loaded.size}")
        return loaded.convert("RGB")


def screenshots(root: Path) -> None:
    source = root / "artifacts/v8/screenshots/bmp"
    output = root / "artifacts/v8/screenshots/png"
    output.mkdir(parents=True, exist_ok=True)
    font = ImageFont.load_default(size=16)
    tile, label_height = 240, 28
    contact = Image.new("RGB", (6 * tile, 3 * (tile + label_height)), "#000105")
    draw = ImageDraw.Draw(contact)
    for index, (number, name) in enumerate(SCREENS):
        frame = require_480(source / f"aura-{name}.bmp")
        frame.save(output / f"{number}-{name}.png", optimize=True)
        x, y = (index % 6) * tile, (index // 6) * (tile + label_height)
        contact.paste(frame.resize((tile, tile), Image.Resampling.LANCZOS), (x, y))
        draw.text((x + 8, y + tile + 5), f"{number} {name}", fill="#BACBE3", font=font)
    contact.save(root / "artifacts/v8/AURA_V8_PRODUCTION_SCREENS.png", optimize=True)


def changed_pixels(first: Image.Image, last: Image.Image, box: tuple[int, int, int, int] | None = None) -> int:
    if box:
        first, last = first.crop(box), last.crop(box)
    histogram = ImageChops.difference(first, last).convert("L").histogram()
    return sum(histogram[5:])


def luminous_bounds(frame: Image.Image, box: tuple[int, int, int, int]) -> tuple[int, int, int, int] | None:
    crop = frame.crop(box)
    mask = Image.new("1", crop.size)
    pixels = mask.load()
    for y in range(crop.height):
        for x in range(crop.width):
            red, green, blue = crop.getpixel((x, y))
            if max(red, green, blue) >= 46 and max(red, green, blue) - min(red, green, blue) >= 14:
                pixels[x, y] = 1
    bounds = mask.getbbox()
    if not bounds:
        return None
    return bounds[0] + box[0], bounds[1] + box[1], bounds[2] + box[0], bounds[3] + box[1]


def motion(root: Path) -> None:
    motion_root = root / "artifacts/v8/motion"
    font = ImageFont.load_default(size=16)
    sheet = Image.new("RGB", (8 * 160, 4 * 190), "#000105")
    draw = ImageDraw.Draw(sheet)
    summary: dict[str, object] = {"schema": "aura.production-evidence.v8", "states": {}}
    for row, state in enumerate(MOTION_STATES):
        frames = [require_480(motion_root / state / f"{state}-{index}.bmp") for index in range(8)]
        hero_box = HERO_BOXES[state]
        pair_differences = [changed_pixels(frames[index], frames[index + 1], hero_box) for index in range(7)]
        first_last = changed_pixels(frames[0], frames[-1], hero_box)
        outside = changed_pixels(frames[0], frames[-1]) - first_last
        bounds = [luminous_bounds(frame, hero_box) for frame in frames]
        if min(pair_differences) < 300:
            raise ValueError(f"{state}: a hero frame pair is too static ({pair_differences})")
        if first_last < 1000:
            raise ValueError(f"{state}: hero is too static ({first_last} changed pixels)")
        if any(value is None for value in bounds):
            raise ValueError(f"{state}: no luminous hero geometry found")
        for value in bounds:
            assert value is not None
            if value[0] <= 4 or value[2] >= 476 or value[1] <= 48 or value[3] >= 292:
                raise ValueError(f"{state}: luminous hero approaches a display edge: {value}")
        for index, frame in enumerate(frames):
            frame.save(motion_root / state / f"{state}-{index}.png", optimize=True)
            sheet.paste(frame.resize((160, 160), Image.Resampling.LANCZOS), (index * 160, row * 190))
        draw.text((8, row * 190 + 165),
                  f"{state} · 8 frames / 2450 ms · hero Δ {first_last}",
                  fill="#25D6FF", font=font)
        frames[0].save(motion_root / f"{state}.gif", save_all=True,
                       append_images=frames[1:], duration=350, loop=0, optimize=False)
        summary["states"][state] = {
            "frames": 8,
            "span_ms": 2450,
            "hero_box": hero_box,
            "hero_changed_pixels_first_to_last": first_last,
            "hero_changed_pixels_adjacent": pair_differences,
            "outside_hero_changed_pixels": outside,
            "luminous_bounds": bounds,
        }
    sheet.save(root / "artifacts/v8/AURA_V8_MOTION_FRAMES.png", optimize=True)
    interaction: dict[str, object] = {}
    for scale in ("1x", "2x"):
        path = root / f"artifacts/v8/interaction/pointer-{scale}.json"
        data = json.loads(path.read_text(encoding="utf-8"))
        if data["passed"] != data["total"]:
            raise ValueError(f"{path}: {data['passed']}/{data['total']} pointer checks passed")
        interaction[scale] = {"passed": data["passed"], "total": data["total"]}
    summary["pointer_coverage"] = interaction
    (root / "artifacts/v8/evidence-summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--screenshots-only", action="store_true")
    parser.add_argument("--motion-only", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    if not args.motion_only:
        screenshots(root)
    if not args.screenshots_only:
        motion(root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
