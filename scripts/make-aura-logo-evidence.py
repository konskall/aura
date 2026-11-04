"""Build one review sheet from the real Aura wordmark simulator captures."""

from pathlib import Path

from PIL import Image, ImageDraw


APPEARANCES = ("halo", "marble", "aurora", "graphite")
VARIANTS = (
    ("el", "home"),
    ("el", "boot"),
    ("el", "power-off"),
    ("en", "home"),
    ("en", "boot"),
    ("en", "power-off"),
)


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    root = repo / "artifacts" / "aura-logo"
    tile = 240
    label_height = 28
    sheet = Image.new("RGB", (len(VARIANTS) * tile, len(APPEARANCES) * (tile + label_height)), "#000105")
    draw = ImageDraw.Draw(sheet)

    for row, appearance in enumerate(APPEARANCES):
        for column, (language, screen) in enumerate(VARIANTS):
            path = root / f"aura-logo-{appearance}-{language}-{screen}.bmp"
            with Image.open(path) as source:
                if source.size != (480, 480):
                    raise ValueError(f"{path}: expected 480x480, got {source.size}")
                frame = source.convert("RGB").resize((tile, tile), Image.Resampling.LANCZOS)
            x = column * tile
            y = row * (tile + label_height)
            sheet.paste(frame, (x, y))
            draw.text((x + 7, y + tile + 7), f"{appearance} · {language.upper()} · {screen}", fill="#BACBE3")

    output = root / "AURA_LOGO_REVIEW.png"
    sheet.save(output, optimize=True)
    print(f"Aura logo evidence: 24 captures, sheet={output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
