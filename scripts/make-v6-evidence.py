"""Build and validate AURA v6 square-screen and motion evidence."""

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


def require_480(path: Path) -> Image.Image:
    if not path.is_file():
        raise FileNotFoundError(path)
    with Image.open(path) as loaded:
        if loaded.size != (480, 480):
            raise ValueError(f"{path}: expected 480x480, got {loaded.size}")
        return loaded.convert("RGB")


def screenshots(root: Path) -> None:
    source = root / "artifacts/v6/screenshots/bmp"
    output = root / "artifacts/v6/screenshots/png"
    output.mkdir(parents=True, exist_ok=True)
    font = ImageFont.load_default(size=16)
    tile = 240
    label_height = 28
    contact = Image.new("RGB", (6 * tile, 3 * (tile + label_height)), "#000105")
    draw = ImageDraw.Draw(contact)
    for index, (number, name) in enumerate(SCREENS):
        frame = require_480(source / f"aura-{name}.bmp")
        frame.save(output / f"{number}-{name}.png", optimize=True)
        x = (index % 6) * tile
        y = (index // 6) * (tile + label_height)
        contact.paste(frame.resize((tile, tile), Image.Resampling.LANCZOS), (x, y))
        draw.text((x + 8, y + tile + 5), f"{number} {name}", fill="#BACBE3", font=font)
    contact.save(root / "artifacts/v6/AURA_V6_RECOVERY_SCREENS.png", optimize=True)


def changed_pixels(first: Image.Image, last: Image.Image) -> int:
    difference = ImageChops.difference(first, last).convert("L")
    histogram = difference.histogram()
    return sum(histogram[5:])


def motion(root: Path) -> None:
    motion_root = root / "artifacts/v6/motion"
    font = ImageFont.load_default(size=16)
    sheet = Image.new("RGB", (8 * 160, 4 * 190), "#000105")
    draw = ImageDraw.Draw(sheet)
    summary: dict[str, object] = {"schema": "aura.motion-evidence.v1", "states": {}}
    for row, state in enumerate(MOTION_STATES):
        frames = [require_480(motion_root / state / f"{state}-{index}.bmp") for index in range(8)]
        difference = changed_pixels(frames[0], frames[-1])
        if difference < 1000:
            raise ValueError(f"{state}: animation evidence is too static ({difference} changed pixels)")
        for index, frame in enumerate(frames):
            frame.save(motion_root / state / f"{state}-{index}.png", optimize=True)
            sheet.paste(frame.resize((160, 160), Image.Resampling.LANCZOS), (index * 160, row * 190))
        draw.text((8, row * 190 + 165), f"{state} · 8 frames · changed pixels {difference}",
                  fill="#25D6FF", font=font)
        frames[0].save(
            motion_root / f"{state}.gif",
            save_all=True,
            append_images=frames[1:],
            duration=160,
            loop=0,
            optimize=False,
        )
        summary["states"][state] = {"frames": 8, "changed_pixels_first_to_last": difference}

    sheet.save(root / "artifacts/v6/AURA_V6_MOTION_FRAMES.png", optimize=True)
    interaction: dict[str, object] = {}
    for scale in ("1x", "2x"):
        path = root / f"artifacts/v6/interaction/pointer-{scale}.json"
        data = json.loads(path.read_text(encoding="utf-8"))
        if data["passed"] != data["total"]:
            raise ValueError(f"{path}: {data['passed']}/{data['total']} pointer checks passed")
        interaction[scale] = {"passed": data["passed"], "total": data["total"]}
    summary["pointer_coverage"] = interaction
    (root / "artifacts/v6/evidence-summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
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
