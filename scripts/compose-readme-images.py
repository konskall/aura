"""Refresh the README's screen pictures from the simulator.

- docs/images/aura-production-screens.png and aura-companion-screens.png are the
  evidence sheets of the last full test run (artifacts/v10_2 and artifacts/v13,
  written by scripts/test.ps1), scaled to 1200 px wide.
- docs/images/aura-newer-screens.png is captured here: photos, file transfer,
  Credits, the appearance setting, the Aoraton account and a conversation with
  a photo, eight screens at
  288 px on a dark page, Greek, in the Halo look.
- docs/images/aura-v1-screens.png is captured here too: what version 1.0.0
  changed (the grouped settings, Οθόνη, Κατάσταση συσκευής, Μάρμαρο's centred
  pearl, an action that asks first, and the version in Σύστημα).

Every capture pins the clock, the date and the labelled DEMO data, so an
unchanged screen gives the same picture. scripts/compose-appearances.py makes
the third picture, of the four looks.

Usage:
  python scripts/compose-readme-images.py [--skip-sheets]
"""

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parent.parent
EXE = REPO / "build" / "native" / "aura_simulator.exe"
IMAGES = REPO / "docs" / "images"
SHEETS = (
    (REPO / "artifacts" / "v10_2" / "AURA_V10_2_PRODUCTION_SCREENS.png", IMAGES / "aura-production-screens.png"),
    (REPO / "artifacts" / "v13" / "AURA_V13_COMPANION_SCREENS.png", IMAGES / "aura-companion-screens.png"),
)
SHEET_WIDTH = 1200
NEWER = IMAGES / "aura-newer-screens.png"
NEWER_SCREENS = (
    ("photos", "photo-ambient", "file-transfer-receiving", "credits"),
    ("appearance", "aoraton-account-signin", "aoraton-account", "aoraton-photo"),
)
V1 = IMAGES / "aura-v1-screens.png"
# (scenario, look, list scroll in px)
V1_SCREENS = (
    (("settings", "halo", 0), ("settings", "halo", 420), ("settings", "halo", 864), ("display-settings", "halo", 288)),
    (("battery", "halo", 620), ("listening", "marble", 0), ("answer-action", "halo", 0), ("system", "halo", 0)),
)
TILE = 288
GAP = 16
BACKGROUND = (18, 18, 20)


def capture(scenario, folder, look="halo", scroll=0):
    path = Path(folder) / f"{scenario}-{look}-{scroll}.bmp"
    command = [str(EXE), "--capture", scenario, "--screenshot", str(path), "--appearance", look]
    if scroll:
        command += ["--capture-scroll", str(scroll)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=180)
    if result.returncode != 0 or not path.exists():
        sys.exit(f"capture {scenario} failed: {result.stderr.strip() or result.stdout.strip()}")
    with Image.open(path) as screen:
        return screen.convert("RGB")


def scale_sheet(source, target):
    if not source.exists():
        sys.exit(f"run scripts/test.ps1 first: {source} is missing")
    with Image.open(source) as sheet:
        height = round(sheet.height * SHEET_WIDTH / sheet.width)
        sheet.convert("RGB").resize((SHEET_WIDTH, height), Image.Resampling.LANCZOS).save(target, optimize=True)
    print(f"wrote {target} ({SHEET_WIDTH} x {height})")


def compose_newer():
    columns = max(len(row) for row in NEWER_SCREENS)
    width = columns * TILE + (columns + 1) * GAP
    height = len(NEWER_SCREENS) * TILE + (len(NEWER_SCREENS) + 1) * GAP
    page = Image.new("RGB", (width, height), BACKGROUND)
    with tempfile.TemporaryDirectory() as folder:
        for row, scenarios in enumerate(NEWER_SCREENS):
            for column, scenario in enumerate(scenarios):
                screen = capture(scenario, folder).resize((TILE, TILE), Image.Resampling.LANCZOS)
                page.paste(screen, (GAP + column * (TILE + GAP), GAP + row * (TILE + GAP)))
    page.save(NEWER, optimize=True)
    print(f"wrote {NEWER} ({width} x {height})")


def compose_v1():
    columns = max(len(row) for row in V1_SCREENS)
    width = columns * TILE + (columns + 1) * GAP
    height = len(V1_SCREENS) * TILE + (len(V1_SCREENS) + 1) * GAP
    page = Image.new("RGB", (width, height), BACKGROUND)
    with tempfile.TemporaryDirectory() as folder:
        for row, screens in enumerate(V1_SCREENS):
            for column, (scenario, look, scroll) in enumerate(screens):
                screen = capture(scenario, folder, look, scroll).resize((TILE, TILE), Image.Resampling.LANCZOS)
                page.paste(screen, (GAP + column * (TILE + GAP), GAP + row * (TILE + GAP)))
    page.save(V1, optimize=True)
    print(f"wrote {V1} ({width} x {height})")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--skip-sheets", action="store_true", help="only capture the newer screens")
    args = parser.parse_args()
    if not EXE.exists():
        sys.exit(f"build the simulator first: {EXE} is missing")
    IMAGES.mkdir(parents=True, exist_ok=True)
    if not args.skip_sheets:
        for source, target in SHEETS:
            scale_sheet(source, target)
    compose_newer()
    compose_v1()


if __name__ == "__main__":
    main()
