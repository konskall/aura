"""Build deterministic RGB565 LVGL assets from the editable AURA source artwork."""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageEnhance


@dataclass(frozen=True)
class Asset:
    source: str
    symbol: str
    size: tuple[int, int]
    crop: tuple[int, int, int, int]


ASSETS = (
    Asset("aura-home-orb-source.png", "aura_asset_home_orb", (340, 340), (0, 0, 1024, 1024)),
    Asset("aura-listening-source.png", "aura_asset_listening", (404, 280), (0, 190, 1024, 898)),
    Asset("aura-thinking-source.png", "aura_asset_thinking", (340, 340), (0, 0, 1024, 1024)),
    Asset("aura-speaking-source.png", "aura_asset_speaking", (404, 292), (0, 138, 1024, 878)),
    Asset("aura-coast-source.png", "aura_asset_coast_thumb", (138, 138), (0, 0, 1024, 1024)),
    Asset("aura-coast-source.png", "aura_asset_coast_wide", (404, 270), (0, 150, 1024, 834)),
    Asset("aura-map-source.png", "aura_asset_demo_map", (448, 448), (0, 0, 1024, 1024)),
)


def rgb565_bytes(image: Image.Image) -> bytes:
    output = bytearray()
    for red, green, blue in image.convert("RGB").getdata():
        value = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)
        output.extend(struct.pack("<H", value))
    return bytes(output)


def emit_array(data: bytes) -> str:
    rows = []
    for offset in range(0, len(data), 16):
        chunk = data[offset : offset + 16]
        rows.append("    " + ", ".join(f"0x{value:02X}" for value in chunk) + ",")
    return "\n".join(rows)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sources", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--preview", type=Path, required=True)
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    args.preview.mkdir(parents=True, exist_ok=True)

    header = [
        "#pragma once",
        "",
        '#include "lvgl.h"',
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
    ]
    source = [
        '#include "aura_visual_assets.h"',
        "",
    ]

    for asset in ASSETS:
        source_path = args.sources / asset.source
        if not source_path.is_file():
            raise FileNotFoundError(source_path)
        with Image.open(source_path) as original:
            image = original.convert("RGB").crop(asset.crop)
            image = image.resize(asset.size, Image.Resampling.LANCZOS)
            image = ImageEnhance.Contrast(image).enhance(1.08)
            image.save(args.preview / f"{asset.symbol}.png", optimize=True)
        data = rgb565_bytes(image)
        width, height = asset.size
        header.append(f"LV_IMAGE_DECLARE({asset.symbol});")
        source.extend(
            [
                f"static const uint8_t {asset.symbol}_map[] = {{",
                emit_array(data),
                "};",
                "",
                f"const lv_image_dsc_t {asset.symbol} = {{",
                "    .header = {",
                "        .magic = LV_IMAGE_HEADER_MAGIC,",
                "        .cf = LV_COLOR_FORMAT_RGB565,",
                "        .flags = 0,",
                f"        .w = {width},",
                f"        .h = {height},",
                f"        .stride = {width * 2},",
                "        .reserved_2 = 0,",
                "    },",
                f"    .data_size = sizeof({asset.symbol}_map),",
                f"    .data = {asset.symbol}_map,",
                "    .reserved = nullptr,",
                "    .reserved_2 = nullptr,",
                "};",
                "",
            ]
        )

    header.extend(["", "#ifdef __cplusplus", "}", "#endif", ""])
    (args.output / "aura_visual_assets.h").write_text("\n".join(header), encoding="utf-8")
    (args.output / "aura_visual_assets.cpp").write_text("\n".join(source), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
