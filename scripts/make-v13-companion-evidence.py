"""Build and validate AURA v13 "Companion" evidence from real native LVGL captures."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont

SCREENS = (
    ("01", "quick-actions"), ("02", "home-timer"), ("03", "transcribing"),
    ("04", "answer-action"), ("05", "voice-error"), ("06", "clock"),
    ("07", "clock-new"), ("08", "timer-new"), ("09", "alarm-new"),
    ("10", "reminder-new"), ("11", "clock-alert"), ("12", "notes"),
    ("13", "note-detail"), ("14", "note-record"), ("15", "briefing"),
    ("16", "translate"), ("17", "always-on"), ("18", "phone-call"),
    ("19", "notifications-phone"), ("20", "phone-notification"), ("21", "phone-link"),
    ("22", "display-settings"), ("23", "update"),
)


def load(path: Path) -> Image.Image:
    if not path.is_file():
        raise FileNotFoundError(path)
    with Image.open(path) as loaded:
        if loaded.size != (480, 480):
            raise ValueError(f"{path}: expected 480x480, got {loaded.size}")
        return loaded.convert("RGB")


def lit_pixels(frame: Image.Image) -> int:
    """Pixels brighter than the AMOLED black canvas."""
    return sum(frame.convert("L").histogram()[24:])


def difference(first: Image.Image, second: Image.Image) -> int:
    return sum(ImageChops.difference(first, second).convert("L").histogram()[8:])


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    root = repo / "artifacts/v13"
    source = root / "screenshots/bmp"
    font = ImageFont.load_default()
    frames = {name: load(source / f"aura-{name}.bmp") for _, name in SCREENS}

    problems = []
    for name, frame in frames.items():
        lit = lit_pixels(frame)
        # The always-on clock is deliberately sparse; everything else is a full screen.
        minimum = 2500 if name == "always-on" else 9000
        if lit < minimum:
            problems.append(f"{name}: only {lit} lit pixels")
    names = [name for _, name in SCREENS]
    for index, first in enumerate(names):
        for second in names[index + 1:]:
            if difference(frames[first], frames[second]) < 1500:
                problems.append(f"{first} and {second} look identical")
    if lit_pixels(frames["always-on"]) > 60000:
        problems.append("always-on: too bright for an always-on clock")
    if problems:
        raise SystemExit("v13 evidence failed: " + "; ".join(problems))

    tile, footer = 240, 28
    rows = (len(SCREENS) + 5) // 6
    sheet = Image.new("RGB", (6 * tile, rows * (tile + footer)), "#000105")
    draw = ImageDraw.Draw(sheet)
    pngs = root / "screenshots/png"
    pngs.mkdir(parents=True, exist_ok=True)
    results = []
    for index, (number, name) in enumerate(SCREENS):
        frame = frames[name]
        x, y = (index % 6) * tile, (index // 6) * (tile + footer)
        sheet.paste(frame.resize((tile, tile), Image.Resampling.LANCZOS), (x, y))
        draw.text((x + 8, y + tile + 5), f"{number} {name}", fill="#BACBE3", font=font)
        frame.save(pngs / f"{number}-{name}.png", optimize=True)
        digest = hashlib.sha256((source / f"aura-{name}.bmp").read_bytes()).hexdigest().upper()
        results.append({"screen": name, "lit_pixels": lit_pixels(frame), "sha256": digest})
    sheet.save(root / "AURA_V13_COMPANION_SCREENS.png", optimize=True)
    (root / "evidence.json").write_text(json.dumps({
        "schema": "aura.v13-companion-evidence.v1",
        "screens": results,
    }, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"v13 companion evidence ok: {len(SCREENS)} screens, sheet={root / 'AURA_V13_COMPANION_SCREENS.png'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
