"""Validate native AURA v10.3.1 weather captures and assemble evidence sheets."""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont


SCREENS = (
    "weather-sunny", "weather-cloudy", "weather-rainy", "weather-stormy",
    "weather-snowy", "weather-night", "weather-forecast", "weather-loading",
    "weather-no-city", "weather-offline", "weather-error", "weather-location",
    "weather-results", "weather-settings",
)
MOTION_STATES = (
    "weather-clear", "weather-cloudy", "weather-rain",
    "weather-storm", "weather-snow", "weather-night",
)
WEATHER_SCENE_BOX = (264, 134, 458, 328)


def require_480(path: Path) -> Image.Image:
    if not path.is_file():
        raise FileNotFoundError(path)
    with Image.open(path) as loaded:
        if loaded.size != (480, 480):
            raise ValueError(f"{path}: expected 480x480, got {loaded.size}")
        return loaded.convert("RGB")


def changed_pixels(first: Image.Image, second: Image.Image) -> int:
    histogram = ImageChops.difference(
        first.crop(WEATHER_SCENE_BOX), second.crop(WEATHER_SCENE_BOX)
    ).convert("L").histogram()
    return sum(histogram[4:])


def performance(path: Path) -> dict[str, float | int]:
    raw = path.read_bytes()
    text = raw.decode("utf-16", errors="ignore")
    if "AURA motion evidence:" not in text:
        text = raw.decode("utf-8", errors="ignore")
    result: dict[str, float | int] = {}
    for key in (
        "weather_updates", "weather_update_hz", "handler_avg_ms", "handler_max_ms",
        "lvgl_used_bytes", "lvgl_fragmentation",
    ):
        match = re.search(rf"{key}=([^,\s%]+)", text)
        if not match:
            raise ValueError(f"{path}: missing {key}")
        value = match.group(1)
        result[key] = float(value) if "." in value else int(value)
    return result


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    root = repo / "artifacts/v10_3_1"
    font = ImageFont.load_default(size=15)

    screen_sheet = Image.new("RGB", (4 * 240, 4 * 265), "#000105")
    screen_draw = ImageDraw.Draw(screen_sheet)
    screen_results = []
    hashes = set()
    for index, scenario in enumerate(SCREENS):
        path = root / f"screenshots/bmp/aura-{scenario}.bmp"
        frame = require_480(path)
        digest = hashlib.sha256(path.read_bytes()).hexdigest().upper()
        hashes.add(digest)
        x, y = (index % 4) * 240, (index // 4) * 265
        screen_sheet.paste(frame.resize((240, 240), Image.Resampling.LANCZOS), (x, y))
        screen_draw.text((x + 6, y + 244), scenario, fill="#BACBE3", font=font)
        png = root / f"screenshots/png/{index + 1:02d}-{scenario}.png"
        png.parent.mkdir(parents=True, exist_ok=True)
        frame.save(png, optimize=True)
        screen_results.append({"screen": scenario, "size": [480, 480], "sha256": digest})
    if len(hashes) != len(SCREENS):
        raise ValueError("v10.3.1 weather gallery contains duplicate rendered states")
    screen_sheet.save(root / "AURA_V10_3_1_WEATHER_STATES.png", optimize=True)

    motion_sheet = Image.new("RGB", (8 * 160, 6 * 184), "#000105")
    motion_draw = ImageDraw.Draw(motion_sheet)
    motion_results = {}
    for row, state in enumerate(MOTION_STATES):
        frames = [require_480(root / f"motion/{state}/{state}-{index}.bmp") for index in range(8)]
        frame_hashes = {hashlib.sha256(frame.tobytes()).hexdigest() for frame in frames}
        deltas = [changed_pixels(frames[index], frames[index + 1]) for index in range(7)]
        if len(frame_hashes) != 8 or min(deltas) < 20:
            raise ValueError(f"{state}: expected eight materially distinct frames; deltas={deltas}")
        y = row * 184
        motion_draw.text((6, y + 3), f"{state} · 8 frames · min Δ {min(deltas)}",
                         fill="#25D6FF", font=font)
        for index, frame in enumerate(frames):
            frame.save(root / f"motion/{state}/{state}-{index}.png", optimize=True)
            motion_sheet.paste(frame.resize((160, 160), Image.Resampling.LANCZOS),
                               (index * 160, y + 24))
        frames[0].save(root / f"motion/{state}.gif", save_all=True,
                       append_images=frames[1:], duration=350, loop=0, optimize=False)
        motion_results[state] = {
            "frames": 8,
            "span_ms": 2450,
            "changed_pixels_adjacent": deltas,
            "all_frames_distinct": True,
            "performance": performance(root / f"performance/{state}.log"),
        }
    motion_sheet.save(root / "AURA_V10_3_1_WEATHER_MOTION.png", optimize=True)

    pointer = {}
    for scale in ("1x", "2x"):
        path = root / f"interaction/pointer-{scale}.json"
        report = json.loads(path.read_text(encoding="utf-8"))
        if report["passed"] != report["total"]:
            raise ValueError(f"{path}: {report['passed']}/{report['total']} checks passed")
        pointer[scale] = {"passed": report["passed"], "total": report["total"]}

    summary = {
        "schema": "aura.production-evidence.v10.3.1.weather",
        "screens": screen_results,
        "motion": motion_results,
        "pointer_coverage": pointer,
    }
    (root / "weather-evidence-summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
