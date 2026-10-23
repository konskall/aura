from __future__ import annotations

import json
import sys
from pathlib import Path

from fontTools.ttLib import TTFont


SAMPLE = (
    "Ακούω… Σκέφτομαι… Σου απαντώ… "
    "Δεν υπάρχει σύνδεση Δεν ολοκληρώθηκε — Προσπάθησε ξανά "
    "Σύνδεση Wi-Fi • Bluetooth • Μπαταρία 92% "
    "Ειδοποιήσεις και μηνύματα Aoraton "
    "Βάσια, μπορείς να με πάρεις τηλέφωνο άμεσα; "
    "Θερμοκρασία 22°C — υγρασία 45% — άνεμος 12 km/h "
    "Ά Έ Ή Ί Ό Ύ Ώ ά έ ή ί ό ύ ώ ϊ ϋ ΐ ΰ ς €"
)


def coverage(path: Path, sample: str) -> dict[str, object]:
    font = TTFont(path)
    cmap: set[int] = set()
    for table in font["cmap"].tables:
        cmap.update(table.cmap)
    missing = sorted({ord(char) for char in sample if not char.isspace()} - cmap)
    return {
        "file": path.name,
        "glyphs": len(cmap),
        "missing": [f"U+{codepoint:04X}" for codepoint in missing],
    }


def main() -> int:
    source = Path(sys.argv[1])
    reports = [
        coverage(source / "IBMPlexSans-Light.ttf", "0123456789:%+-./°C−"),
        coverage(source / "IBMPlexSans-Regular.ttf", SAMPLE),
        coverage(source / "IBMPlexSans-Medium.ttf", SAMPLE),
        coverage(source / "IBMPlexSans-SemiBold.ttf", SAMPLE),
        coverage(source / "SpaceGrotesk-Medium.ttf", "AURA"),
    ]
    print(json.dumps(reports, ensure_ascii=False, indent=2))
    return 1 if any(report["missing"] for report in reports) else 0


if __name__ == "__main__":
    raise SystemExit(main())

