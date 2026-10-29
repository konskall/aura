"""Capture every simulator scenario, or compare two capture folders pixel by pixel.

The appearances work must leave Άλως (the default look) pixel-identical, so a
baseline is captured before a change and compared after it.

Usage:
  python scripts/compare-captures.py capture <out_dir> [--appearance ID]
  python scripts/compare-captures.py compare <baseline_dir> <candidate_dir> [--allow NAME ...]
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageChops

REPO = Path(__file__).resolve().parent.parent
EXE = REPO / "build" / "native" / "aura_simulator.exe"

# Regions (x1, y1, x2, y2, exclusive) that differ between two identical runs,
# with the reason. Only these boxes are ignored; the rest of the screen is compared.
NON_DETERMINISTIC = {
    "aoraton-account-unlock": ((41, 113, 52, 159), "blinking text cursor"),
    "aoraton-account-waiting": ((259, 182, 284, 206), "waiting spinner"),
    "aoraton-compose": ((42, 110, 53, 152), "blinking text cursor"),
    "boot": ((210, 410, 225, 424), "boot progress bar end"),
    "diagnostics": ((150, 190, 310, 252), "live host loop rate and handler times"),
    "wifi-password": ((41, 113, 52, 159), "blinking text cursor"),
}


def scenarios():
    text = (REPO / "simulator" / "app" / "src" / "main.cpp").read_text(encoding="utf-8")
    return sorted(set(re.findall(r'capture_scenario == "([a-z0-9_-]+)"', text)))


def capture(out_dir, appearance):
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    names = scenarios()
    for name in names:
        command = [str(EXE), "--capture", name, "--screenshot", str(out / f"{name}.bmp")]
        if appearance:
            command += ["--appearance", appearance]
        result = subprocess.run(command, capture_output=True, text=True, timeout=180)
        if result.returncode != 0:
            print(f"FAILED {name}: exit {result.returncode}")
            return 1
    print(f"captured {len(names)} scenarios into {out}")
    return 0


def changed_pixels(first, second):
    difference = ImageChops.difference(first, second)
    if difference.getbbox() is None:
        return 0
    mask = difference.convert("L").point(lambda value: 255 if value else 0)
    return mask.histogram()[255]


def compare(baseline, candidate, allowed):
    failures = 0
    for name in scenarios():
        first_path = Path(baseline) / f"{name}.bmp"
        second_path = Path(candidate) / f"{name}.bmp"
        if not first_path.exists() and second_path.exists():
            print(f"new {name}")
            continue
        if not first_path.exists() or not second_path.exists():
            print(f"MISSING {name}")
            failures += 1
            continue
        first = Image.open(first_path).convert("RGB")
        second = Image.open(second_path).convert("RGB")
        if first.size != second.size:
            print(f"DIFFERS {name}: size {first.size} != {second.size}")
            failures += 1
            continue
        if name in NON_DETERMINISTIC:
            box = NON_DETERMINISTIC[name][0]
            first.paste((0, 0, 0), box)
            second.paste((0, 0, 0), box)
        changed = changed_pixels(first, second)
        if changed:
            expected = name in allowed
            print(f"{'allowed' if expected else 'DIFFERS'} {name}: {changed} px")
            failures += 0 if expected else 1
    print("identical" if failures == 0 else f"{failures} unexpected differences")
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    capture_parser = commands.add_parser("capture")
    capture_parser.add_argument("out_dir")
    capture_parser.add_argument("--appearance")
    compare_parser = commands.add_parser("compare")
    compare_parser.add_argument("baseline")
    compare_parser.add_argument("candidate")
    compare_parser.add_argument("--allow", nargs="*", default=[])
    arguments = parser.parse_args()
    if arguments.command == "capture":
        return capture(arguments.out_dir, arguments.appearance)
    return compare(arguments.baseline, arguments.candidate, set(arguments.allow))


if __name__ == "__main__":
    sys.exit(main())
