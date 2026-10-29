"""Make the static instances of the appearance fonts from their variable sources.

lv_font_conv cannot read variable fonts, so every weight, width and optical
size used by the Marble, Aurora and Graphite appearances is pinned here.

Usage: python tools/fonts/instance-fonts.py <source_dir>
"""

from __future__ import annotations

import sys
from pathlib import Path

import fontTools
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

# Variable sources, saved under bracket-free names by build-fonts.ps1.
COMMISSIONER = "Commissioner-VF.ttf"
NOTO_SERIF = "NotoSerifDisplay-VF.ttf"
MANROPE = "Manrope-VF.ttf"
ROBOTO_FLEX = "RobotoFlex-VF.ttf"

# Output file -> (variable source, axis values). Axes not named stay at their defaults.
INSTANCES = {
    "Commissioner-Regular.ttf": (COMMISSIONER, {"wght": 400}),
    "Commissioner-Medium.ttf": (COMMISSIONER, {"wght": 500}),
    "NotoSerifDisplay-Regular.ttf": (NOTO_SERIF, {"wght": 400, "wdth": 100}),
    "NotoSerifDisplay-Light.ttf": (NOTO_SERIF, {"wght": 300, "wdth": 100}),
    "NotoSerifDisplay-ExtraLight.ttf": (NOTO_SERIF, {"wght": 200, "wdth": 100}),
    "Manrope-Medium.ttf": (MANROPE, {"wght": 500}),
    "Manrope-SemiBold.ttf": (MANROPE, {"wght": 600}),
    "Manrope-Bold.ttf": (MANROPE, {"wght": 700}),
    "Manrope-Light.ttf": (MANROPE, {"wght": 300}),
    "Manrope-ExtraLight.ttf": (MANROPE, {"wght": 200}),
    # Roboto Flex: the optical size follows the pixel size it is drawn at.
    "RobotoFlex-Medium-22.ttf": (ROBOTO_FLEX, {"wght": 500, "wdth": 100, "opsz": 22}),
    "RobotoFlex-Regular-26.ttf": (ROBOTO_FLEX, {"wght": 400, "wdth": 100, "opsz": 26}),
    "RobotoFlex-SemiBoldCondensed-28.ttf": (ROBOTO_FLEX, {"wght": 600, "wdth": 88, "opsz": 28}),
    "RobotoFlex-SemiBold-32.ttf": (ROBOTO_FLEX, {"wght": 600, "wdth": 100, "opsz": 32}),
    "RobotoFlex-LightCondensed-64.ttf": (ROBOTO_FLEX, {"wght": 300, "wdth": 46, "opsz": 64}),
    "RobotoFlex-LightCondensed-96.ttf": (ROBOTO_FLEX, {"wght": 300, "wdth": 46, "opsz": 96}),
    "RobotoFlex-LightCondensed-116.ttf": (ROBOTO_FLEX, {"wght": 300, "wdth": 46, "opsz": 116}),
    "RobotoFlex-LightCondensed-204.ttf": (ROBOTO_FLEX, {"wght": 300, "wdth": 46, "opsz": 144}),
}


def instance(source: Path, axes: dict[str, float], output: Path) -> None:
    font = TTFont(source)
    limits = {axis.axisTag: axis.defaultValue for axis in font["fvar"].axes}
    for tag, value in axes.items():
        if tag not in limits:
            raise SystemExit(f"{source.name} has no {tag} axis")
        limits[tag] = value
    static = instantiateVariableFont(font, limits, updateFontNames=False)
    static.save(output)


def main() -> int:
    source_dir = Path(sys.argv[1])
    print(f"fontTools {fontTools.version}")
    for name, (variable, axes) in INSTANCES.items():
        output = source_dir / name
        instance(source_dir / variable, axes, output)
        print(f"{name}: {axes}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
