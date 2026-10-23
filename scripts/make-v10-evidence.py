"""Build and validate AURA v10 production evidence from real LVGL captures."""

from __future__ import annotations

import hashlib
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
MERCURY_BOX = (145, 55, 335, 250)


def require_480(path: Path) -> Image.Image:
    if not path.is_file():
        raise FileNotFoundError(path)
    with Image.open(path) as loaded:
        if loaded.size != (480, 480):
            raise ValueError(f"{path}: expected 480x480, got {loaded.size}")
        return loaded.convert("RGB")


def changed_pixels(first: Image.Image, second: Image.Image, box: tuple[int, int, int, int]) -> int:
    histogram = ImageChops.difference(first.crop(box), second.crop(box)).convert("L").histogram()
    return sum(histogram[5:])


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def build_screens(root: Path, font: ImageFont.ImageFont) -> list[dict[str, object]]:
    source = root / "screenshots/bmp"
    png = root / "screenshots/png"
    png.mkdir(parents=True, exist_ok=True)
    tile, footer = 240, 28
    contact = Image.new("RGB", (6 * tile, 3 * (tile + footer)), "#000105")
    draw = ImageDraw.Draw(contact)
    results = []
    for index, (number, name) in enumerate(SCREENS):
        source_path = source / f"aura-{name}.bmp"
        frame = require_480(source_path)
        png_path = png / f"{number}-{name}.png"
        frame.save(png_path, optimize=True)
        x, y = (index % 6) * tile, (index // 6) * (tile + footer)
        contact.paste(frame.resize((tile, tile), Image.Resampling.LANCZOS), (x, y))
        draw.text((x + 8, y + tile + 5), f"{number} {name}", fill="#BACBE3", font=font)
        results.append({"screen": name, "size": [480, 480], "sha256": file_sha256(source_path)})
    contact.save(root / "AURA_V10_PRODUCTION_SCREENS.png", optimize=True)
    return results


def build_typography(root: Path, font: ImageFont.ImageFont) -> list[dict[str, object]]:
    source = root / "typography"
    sheet = Image.new("RGB", (3 * 480, 508), "#000105")
    draw = ImageDraw.Draw(sheet)
    results = []
    for index in range(3):
        path = source / f"typography-proof-{index + 1}.bmp"
        frame = require_480(path)
        sheet.paste(frame, (index * 480, 28))
        draw.text((index * 480 + 8, 7), f"Typography proof {index + 1}", fill="#35ECC3", font=font)
        results.append({"page": index + 1, "sha256": file_sha256(path)})
    sheet.save(root / "AURA_V10_TYPOGRAPHY_PROOF.png", optimize=True)
    return results


def build_before_after(root: Path, font: ImageFont.ImageFont) -> None:
    before_path = root / "before/AURA_V10_BEFORE_SCREENS.png"
    after_path = root / "AURA_V10_PRODUCTION_SCREENS.png"
    if not before_path.is_file():
        raise FileNotFoundError(before_path)
    with Image.open(before_path) as loaded:
        before = loaded.convert("RGB")
    with Image.open(after_path) as loaded:
        after = loaded.convert("RGB")
    column_width = 720
    before = before.resize(
        (column_width, round(before.height * column_width / before.width)),
        Image.Resampling.LANCZOS,
    )
    after = after.resize(
        (column_width, round(after.height * column_width / after.width)),
        Image.Resampling.LANCZOS,
    )
    header = 30
    sheet = Image.new("RGB", (column_width * 2, header + max(before.height, after.height)), "#000105")
    draw = ImageDraw.Draw(sheet)
    draw.text((8, 7), "BEFORE · inherited v8 UI", fill="#8295B0", font=font)
    draw.text((column_width + 8, 7), "AFTER · v10 production UI", fill="#35ECC3", font=font)
    sheet.paste(before, (0, header))
    sheet.paste(after, (column_width, header))
    sheet.save(root / "AURA_V10_BEFORE_AFTER.png", optimize=True)


def build_motion(root: Path, font: ImageFont.ImageFont) -> dict[str, object]:
    motion_root = root / "motion"
    thumb, header = 240, 24
    sheet = Image.new("RGB", (8 * thumb, 4 * (thumb + header)), "#000105")
    draw = ImageDraw.Draw(sheet)
    summary: dict[str, object] = {}
    for row, state in enumerate(MOTION_STATES):
        frames = [require_480(motion_root / state / f"{state}-{index}.bmp") for index in range(8)]
        hashes = {hashlib.sha256(frame.tobytes()).hexdigest() for frame in frames}
        adjacent = [changed_pixels(frames[index], frames[index + 1], MERCURY_BOX) for index in range(7)]
        first_last = changed_pixels(frames[0], frames[-1], MERCURY_BOX)
        if len(hashes) != 8:
            raise ValueError(f"{state}: expected eight distinct rendered frames")
        if min(adjacent) < 1000 or first_last < 1000:
            raise ValueError(f"{state}: Mercury material is too static: {adjacent}, {first_last}")
        y = row * (thumb + header)
        draw.text((8, y + 5), f"{state} · 8 frames / 2450 ms · Mercury Δ {first_last}",
                  fill="#25D6FF", font=font)
        for index, frame in enumerate(frames):
            frame.save(motion_root / state / f"{state}-{index}.png", optimize=True)
            sheet.paste(frame.resize((thumb, thumb), Image.Resampling.LANCZOS),
                        (index * thumb, y + header))
        frames[0].save(motion_root / f"{state}.gif", save_all=True,
                       append_images=frames[1:], duration=350, loop=0, optimize=False)
        summary[state] = {
            "frames": 8,
            "span_ms": 2450,
            "mercury_box": MERCURY_BOX,
            "changed_pixels_first_to_last": first_last,
            "changed_pixels_adjacent": adjacent,
            "all_frames_distinct": True,
            "performance_log": f"performance/{state}.log",
        }
    sheet.save(root / "AURA_V10_MERCURY_MOTION.png", optimize=True)
    return summary


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    root = repo / "artifacts/v10"
    font = ImageFont.load_default(size=16)
    summary: dict[str, object] = {
        "schema": "aura.production-evidence.v10",
        "screens": build_screens(root, font),
        "typography": build_typography(root, font),
        "motion": build_motion(root, font),
    }
    build_before_after(root, font)
    pointer = {}
    for scale in ("1x", "2x"):
        path = root / f"interaction/pointer-{scale}.json"
        report = json.loads(path.read_text(encoding="utf-8"))
        if report["passed"] != report["total"]:
            raise ValueError(f"{path}: {report['passed']}/{report['total']} checks passed")
        pointer[scale] = {"passed": report["passed"], "total": report["total"]}
    summary["pointer_coverage"] = pointer
    (root / "evidence-summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
