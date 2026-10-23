"""Build and validate AURA v10.2 evidence from real native LVGL captures."""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont


SCREENS = (
    ("01", "home"), ("02", "listening"), ("03", "thinking"),
    ("04", "speaking"), ("05", "answer"), ("06", "answer-long"),
    ("07", "quick-actions"), ("08", "notifications"), ("09", "settings"),
    ("10", "audio"), ("11", "aoraton"), ("12", "aoraton-chat"),
    ("13", "aoraton-compose"), ("14", "aoraton-confirm"),
    ("15", "offline"), ("16", "error"), ("17", "battery"),
    ("18", "diagnostics"), ("19", "wifi"), ("20", "wifi-password"),
    ("21", "wifi-connecting"), ("22", "wifi-failed"), ("23", "wifi-details"),
    ("24", "wifi-unavailable"),
)
MOTION_STATES = ("home", "listening", "thinking", "speaking")
MERCURY_BOX = (145, 165, 335, 355)
MERCURY_SOURCE_BOX = (152, 172, 328, 348)


def require_480(path: Path) -> Image.Image:
    if not path.is_file():
        raise FileNotFoundError(path)
    with Image.open(path) as loaded:
        if loaded.size != (480, 480):
            raise ValueError(f"{path}: expected 480x480, got {loaded.size}")
        return loaded.convert("RGB")


def changed_pixels(first: Image.Image, second: Image.Image,
                   box: tuple[int, int, int, int]) -> int:
    histogram = ImageChops.difference(first.crop(box), second.crop(box)).convert("L").histogram()
    return sum(histogram[5:])


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def validate_legacy_copy_removed(repo: Path) -> None:
    forbidden = ("Ο προσωπικός σου βοηθός", "Your AI companion", '"BLE"')
    matches = []
    for root_name in ("shared", "simulator", "tests"):
        for path in (repo / root_name).rglob("*"):
            if path.suffix not in {".c", ".cpp", ".h", ".hpp"}:
                continue
            text = path.read_text(encoding="utf-8", errors="ignore")
            for value in forbidden:
                if value in text:
                    matches.append(f"{path.relative_to(repo)}: {value}")
    if matches:
        raise ValueError("forbidden legacy copy remains: " + "; ".join(matches))


def build_screen_sheet(source: Path, output: Path, font: ImageFont.ImageFont,
                       screens: tuple[tuple[str, str], ...] = SCREENS) -> list[dict[str, object]]:
    tile, footer = 240, 28
    rows = (len(screens) + 5) // 6
    sheet = Image.new("RGB", (6 * tile, rows * (tile + footer)), "#000105")
    draw = ImageDraw.Draw(sheet)
    results = []
    for index, (number, name) in enumerate(screens):
        source_path = source / f"aura-{name}.bmp"
        frame = require_480(source_path)
        x, y = (index % 6) * tile, (index // 6) * (tile + footer)
        sheet.paste(frame.resize((tile, tile), Image.Resampling.LANCZOS), (x, y))
        draw.text((x + 8, y + tile + 5), f"{number} {name}", fill="#BACBE3", font=font)
        results.append({"screen": name, "size": [480, 480], "sha256": file_sha256(source_path)})
    output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output, optimize=True)
    return results


def export_screen_pngs(root: Path) -> None:
    output = root / "screenshots/png"
    output.mkdir(parents=True, exist_ok=True)
    for number, name in SCREENS:
        require_480(root / f"screenshots/bmp/aura-{name}.bmp").save(
            output / f"{number}-{name}.png", optimize=True)


def build_typography(root: Path, font: ImageFont.ImageFont) -> list[dict[str, object]]:
    sheet = Image.new("RGB", (3 * 480, 508), "#000105")
    draw = ImageDraw.Draw(sheet)
    results = []
    for index in range(3):
        path = root / f"typography/typography-proof-{index + 1}.bmp"
        frame = require_480(path)
        sheet.paste(frame, (index * 480, 28))
        draw.text((index * 480 + 8, 7), f"Typography proof {index + 1}",
                  fill="#35ECC3", font=font)
        results.append({"page": index + 1, "sha256": file_sha256(path)})
    sheet.save(root / "AURA_V10_2_TYPOGRAPHY_PROOF.png", optimize=True)
    return results


def bright_border_pixels(frame: Image.Image) -> int:
    crop = frame.crop(MERCURY_SOURCE_BOX)
    width, height = crop.size
    border = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(border)
    draw.rectangle((0, 0, width - 1, height - 1), outline=255, width=4)
    bright = crop.convert("L").point(lambda value: 255 if value > 145 else 0)
    masked = ImageChops.multiply(bright, border)
    return sum(masked.histogram()[1:])


def parse_performance(path: Path) -> dict[str, object]:
    text = path.read_text(encoding="utf-16", errors="ignore")
    if "AURA motion evidence:" not in text:
        text = path.read_text(encoding="utf-8", errors="ignore")
    values = {}
    for key in ("mercury_frames", "mercury_estimated_hz", "mercury_buffer_bytes",
                "handler_avg_ms", "handler_max_ms", "lvgl_used_bytes", "lvgl_fragmentation"):
        match = re.search(rf"{key}=([^,\s%]+)", text)
        if not match:
            raise ValueError(f"{path}: missing {key}")
        raw = match.group(1)
        values[key] = float(raw) if "." in raw else int(raw)
    return values


def build_motion(root: Path, font: ImageFont.ImageFont) -> dict[str, object]:
    motion_root = root / "motion"
    thumb, header = 240, 24
    sheet = Image.new("RGB", (8 * thumb, 4 * (thumb + header)), "#000105")
    draw = ImageDraw.Draw(sheet)
    summary: dict[str, object] = {}
    first_frames = {}
    for row, state in enumerate(MOTION_STATES):
        frames = [require_480(motion_root / state / f"{state}-{index}.bmp") for index in range(8)]
        hashes = {hashlib.sha256(frame.tobytes()).hexdigest() for frame in frames}
        adjacent = [changed_pixels(frames[index], frames[index + 1], MERCURY_BOX) for index in range(7)]
        first_last = changed_pixels(frames[0], frames[-1], MERCURY_BOX)
        borders = [bright_border_pixels(frame) for frame in frames]
        if len(hashes) != 8:
            raise ValueError(f"{state}: expected eight distinct rendered frames")
        if min(adjacent) < 1000 or first_last < 1000:
            raise ValueError(f"{state}: Mercury material is too static: {adjacent}, {first_last}")
        if max(borders) != 0:
            raise ValueError(f"{state}: bright Mercury pixels touch source bounds: {borders}")
        first_frames[state] = frames[0]
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
            "bright_pixels_on_source_border": borders,
            "all_frames_distinct": True,
            "performance": parse_performance(root / f"performance/{state}.log"),
        }
    cross_state = {}
    for left_index, left in enumerate(MOTION_STATES):
        for right in MOTION_STATES[left_index + 1:]:
            delta = changed_pixels(first_frames[left], first_frames[right], MERCURY_BOX)
            if delta < 5000:
                raise ValueError(f"Mercury states {left}/{right} are not visually distinct: {delta}")
            cross_state[f"{left}_vs_{right}"] = delta
    summary["cross_state_changed_pixels"] = cross_state
    sheet.save(root / "AURA_V10_2_MERCURY_MOTION.png", optimize=True)
    return summary


def build_status_matrix(root: Path, font: ImageFont.ImageFont) -> list[dict[str, object]]:
    tile, header = 240, 28
    sheet = Image.new("RGB", (4 * tile, 4 * (tile + header)), "#000105")
    draw = ImageDraw.Draw(sheet)
    results = []
    for index in range(16):
        bits = format(index, "04b")
        path = root / f"status-matrix/status-{bits}.bmp"
        frame = require_480(path)
        x, y = (index % 4) * tile, (index // 4) * (tile + header)
        draw.text((x + 6, y + 6),
                  f"Wi-Fi {bits[0]} · BLE {bits[1]} · charge {bits[2]} · DEV {bits[3]}",
                  fill="#25D6FF", font=font)
        sheet.paste(frame.resize((tile, tile), Image.Resampling.LANCZOS), (x, y + header))
        results.append({"bits": bits, "sha256": file_sha256(path), "size": [480, 480]})
    if len({entry["sha256"] for entry in results}) != 16:
        raise ValueError("status matrix must produce 16 distinct header states")
    sheet.save(root / "AURA_V10_2_STATUS_MATRIX.png", optimize=True)
    return results


def build_before_after(root: Path, font: ImageFont.ImageFont) -> None:
    before_path = root / "before/AURA_V10_2_BEFORE_SCREENS.png"
    after_path = root / "AURA_V10_2_PRODUCTION_SCREENS.png"
    with Image.open(before_path) as loaded:
        before = loaded.convert("RGB")
    with Image.open(after_path) as loaded:
        after = loaded.convert("RGB")
    column_width = 720
    before = before.resize((column_width, round(before.height * column_width / before.width)), Image.Resampling.LANCZOS)
    after = after.resize((column_width, round(after.height * column_width / after.width)), Image.Resampling.LANCZOS)
    header = 30
    sheet = Image.new("RGB", (column_width * 2, header + max(before.height, after.height)), "#000105")
    draw = ImageDraw.Draw(sheet)
    draw.text((8, 7), "BEFORE · v10 baseline", fill="#8295B0", font=font)
    draw.text((column_width + 8, 7), "AFTER · v12 Halo redesign", fill="#35ECC3", font=font)
    sheet.paste(before, (0, header))
    sheet.paste(after, (column_width, header))
    sheet.save(root / "AURA_V10_2_BEFORE_AFTER.png", optimize=True)


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    root = repo / "artifacts/v10_2"
    font = ImageFont.load_default(size=16)
    validate_legacy_copy_removed(repo)
    # The v10 "before" baseline is local history (artifacts/ is not versioned);
    # a fresh checkout or CI validates the current captures without it.
    before_source = root / "before/screenshots/bmp"
    # The v10 baseline predates the Wi-Fi screens (19-24).
    before = (build_screen_sheet(before_source, root / "before/AURA_V10_2_BEFORE_SCREENS.png", font,
                                 SCREENS[:18])
              if before_source.is_dir() else "not available in this checkout")
    screens = build_screen_sheet(root / "screenshots/bmp",
                                 root / "AURA_V10_2_PRODUCTION_SCREENS.png", font)
    export_screen_pngs(root)
    summary: dict[str, object] = {
        "schema": "aura.production-evidence.v10.2",
        "before_screens": before,
        "screens": screens,
        "typography": build_typography(root, font),
        "motion": build_motion(root, font),
        "status_matrix": build_status_matrix(root, font),
    }
    if before_source.is_dir():
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
