"""Compose the README's picture of the four appearances from simulator captures.

Captures Home and listening in each look with the native simulator and lays them
out as docs/images/aura-appearances.png: Άλως, Μάρμαρο, Σέλας and Γραφίτης from
left to right, Home above and listening below, each screen at 288 px on a dark
page. Run it after a change to any look's Home or presence; the captures pin the
clock, the date and the DEMO data, so an unchanged look gives the same picture.

Usage:
  python scripts/compose-appearances.py [--out PATH]
"""

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parent.parent
EXE = REPO / "build" / "native" / "aura_simulator.exe"
OUT = REPO / "docs" / "images" / "aura-appearances.png"
LOOKS = ("halo", "marble", "aurora", "graphite")
SCENARIOS = ("home", "listening")
TILE = 288
GAP = 16
BACKGROUND = (18, 18, 20)


def capture(look, scenario, folder):
    path = Path(folder) / f"{look}-{scenario}.bmp"
    command = [str(EXE), "--capture", scenario, "--screenshot", str(path), "--appearance", look]
    result = subprocess.run(command, capture_output=True, text=True, timeout=180)
    if result.returncode != 0 or not path.exists():
        sys.exit(f"capture {look}/{scenario} failed: {result.stderr.strip() or result.stdout.strip()}")
    with Image.open(path) as screen:
        return screen.convert("RGB")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, default=OUT)
    args = parser.parse_args()
    if not EXE.exists():
        sys.exit(f"build the simulator first: {EXE} is missing")
    width = len(LOOKS) * TILE + (len(LOOKS) + 1) * GAP
    height = len(SCENARIOS) * TILE + (len(SCENARIOS) + 1) * GAP
    page = Image.new("RGB", (width, height), BACKGROUND)
    with tempfile.TemporaryDirectory() as folder:
        for row, scenario in enumerate(SCENARIOS):
            for column, look in enumerate(LOOKS):
                screen = capture(look, scenario, folder).resize((TILE, TILE), Image.Resampling.LANCZOS)
                page.paste(screen, (GAP + column * (TILE + GAP), GAP + row * (TILE + GAP)))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    page.save(args.out, optimize=True)
    print(f"wrote {args.out} ({width} x {height})")


if __name__ == "__main__":
    main()
