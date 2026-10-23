"""Validate native 480x480 AURA weather captures and assemble v10.3 evidence."""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont


SCREENS = (
    "weather-success", "weather-loading", "weather-no-city", "weather-offline",
    "weather-error", "weather-location", "weather-results", "quick-actions",
    "weather-from-quick-actions",
)
MOTION_STATES = ("weather-clear", "weather-rain", "weather-snow", "weather-wind")
WEATHER_MOTION_BOX = (290, 135, 452, 255)


def require_480(path: Path) -> Image.Image:
    if not path.is_file():
        raise FileNotFoundError(path)
    with Image.open(path) as loaded:
        if loaded.size != (480, 480):
            raise ValueError(f"{path}: expected 480x480, got {loaded.size}")
        return loaded.convert("RGB")


def changed_pixels(first: Image.Image, second: Image.Image) -> int:
    histogram = ImageChops.difference(
        first.crop(WEATHER_MOTION_BOX), second.crop(WEATHER_MOTION_BOX)
    ).convert("L").histogram()
    return sum(histogram[4:])


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def performance(path: Path) -> dict[str, float | int]:
    raw = path.read_bytes()
    text = raw.decode("utf-16", errors="ignore")
    if "AURA motion evidence:" not in text:
        text = raw.decode("utf-8", errors="ignore")
    result: dict[str, float | int] = {}
    for key in ("weather_updates", "weather_update_hz", "handler_avg_ms", "handler_max_ms",
                "lvgl_used_bytes", "lvgl_fragmentation"):
        match = re.search(rf"{key}=([^,\s%]+)", text)
        if not match:
            raise ValueError(f"{path}: missing {key}")
        value = match.group(1)
        result[key] = float(value) if "." in value else int(value)
    return result


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    root = repo / "artifacts/v10_3"
    font = ImageFont.load_default(size=16)
    screen_sheet = Image.new("RGB", (3 * 240, 3 * 268), "#000105")
    screen_draw = ImageDraw.Draw(screen_sheet)
    screen_results = []
    screen_hashes = set()
    for index, scenario in enumerate(SCREENS):
        path = root / f"screenshots/bmp/aura-{scenario}.bmp"
        frame = require_480(path)
        digest = sha256(path)
        screen_hashes.add(digest)
        x, y = (index % 3) * 240, (index // 3) * 268
        screen_sheet.paste(frame.resize((240, 240), Image.Resampling.LANCZOS), (x, y))
        screen_draw.text((x + 7, y + 245), scenario, fill="#BACBE3", font=font)
        screen_results.append({"screen": scenario, "size": [480, 480], "sha256": digest})
        output = root / f"screenshots/png/{index + 1:02d}-{scenario}.png"
        output.parent.mkdir(parents=True, exist_ok=True)
        frame.save(output, optimize=True)
    # The success screen reached directly and through Quick Actions is expected to
    # have identical pixels; routing is independently proven by the pointer report.
    if len(screen_hashes) != len(SCREENS) - 1:
        raise ValueError("weather state gallery contains an unexpected duplicate rendered screen")
    screen_sheet.save(root / "AURA_V10_3_WEATHER_STATES.png", optimize=True)

    motion_sheet = Image.new("RGB", (8 * 200, 4 * 224), "#000105")
    motion_draw = ImageDraw.Draw(motion_sheet)
    motion_results = {}
    for row, state in enumerate(MOTION_STATES):
        frames = [require_480(root / f"motion/{state}/{state}-{index}.bmp") for index in range(8)]
        hashes = {hashlib.sha256(frame.tobytes()).hexdigest() for frame in frames}
        deltas = [changed_pixels(frames[index], frames[index + 1]) for index in range(7)]
        if len(hashes) != 8 or min(deltas) < 20:
            raise ValueError(f"{state}: expected eight materially distinct motion frames; deltas={deltas}")
        y = row * 224
        motion_draw.text((7, y + 5), f"{state} · 8 frames · min Δ {min(deltas)}",
                         fill="#25D6FF", font=font)
        for index, frame in enumerate(frames):
            frame.save(root / f"motion/{state}/{state}-{index}.png", optimize=True)
            motion_sheet.paste(frame.resize((200, 200), Image.Resampling.LANCZOS),
                               (index * 200, y + 24))
        frames[0].save(root / f"motion/{state}.gif", save_all=True,
                       append_images=frames[1:], duration=350, loop=0, optimize=False)
        motion_results[state] = {
            "frames": 8,
            "span_ms": 2450,
            "changed_pixels_adjacent": deltas,
            "all_frames_distinct": True,
            "performance": performance(root / f"performance/{state}.log"),
        }
    motion_sheet.save(root / "AURA_V10_3_WEATHER_MOTION.png", optimize=True)

    pointer = {}
    for scale in ("1x", "2x"):
        path = root / f"interaction/pointer-{scale}.json"
        report = json.loads(path.read_text(encoding="utf-8"))
        if report["passed"] != report["total"]:
            raise ValueError(f"{path}: {report['passed']}/{report['total']} checks passed")
        pointer[scale] = {"passed": report["passed"], "total": report["total"]}

    summary = {
        "schema": "aura.production-evidence.v10.3.weather",
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
