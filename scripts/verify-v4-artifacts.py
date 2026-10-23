"""Fail fast when the deterministic v4 visual-regression artifacts are incomplete."""

from __future__ import annotations

from pathlib import Path

from PIL import Image


SCREENS = (
    "01-home", "02-listening", "03-thinking", "04-speaking", "05-quick-actions",
    "06-notifications", "07-aoraton", "08-navigation", "09-search", "10-weather",
    "11-settings", "12-audio", "13-tools", "14-calendar", "15-camera",
    "16-offline", "17-battery", "18-voice",
)


def require_image(path: Path, expected: tuple[int, int]) -> None:
    if not path.is_file():
        raise FileNotFoundError(path)
    with Image.open(path) as image:
        if image.size != expected:
            raise ValueError(f"{path}: expected {expected}, got {image.size}")


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    require_image(root / "design/reference/aura_ui_target.png", (1536, 1024))
    for screen in SCREENS:
        require_image(root / f"artifacts/screenshots/after/{screen}.png", (480, 480))
        require_image(root / f"artifacts/screenshots/comparisons/{screen}.png", (920, 530))
        if not (root / f"design/reference/panels/{screen}.png").is_file():
            raise FileNotFoundError(root / f"design/reference/panels/{screen}.png")
    if not (root / "artifacts/screenshots/AURA_V4_ALL_SCREENS.png").is_file():
        raise FileNotFoundError("AURA_V4_ALL_SCREENS.png")
    if not (root / "artifacts/screenshots/reference_vs_actual.png").is_file():
        raise FileNotFoundError("reference_vs_actual.png")
    localization = (root / "shared/localization/src/visual_strings.cpp").read_text(encoding="utf-8")
    if "\ufffd" in localization:
        raise ValueError("replacement glyph found in visual localization")
    for phrase in ("Σκέφτομαι…", "Πλοήγηση", "Ειδοποιήσεις", "Επεξεργασία"):
        if phrase not in localization:
            raise ValueError(f"missing Greek glyph fixture: {phrase}")
    print("AURA v4 artifacts: 18/18 captures and comparisons verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
