"""Bounded Edge Read Aloud adapter for the local AURA development bridge.

The caller passes text as Base64, stdout contains MP3 only, and no prompt or
credential is logged. This is an optional simulator/gateway adapter for an
unofficial consumer endpoint; it is never compiled into ESP32 firmware.
"""

from __future__ import annotations

import argparse
import asyncio
import base64
import binascii
import sys

import edge_tts


MAX_TEXT_BYTES = 2_000
MAX_AUDIO_BYTES = 8 * 1024 * 1024
VOICES = {
    "el-GR-AthinaNeural",
    "el-GR-NestorasNeural",
    "en-US-AvaMultilingualNeural",
    "en-US-AndrewMultilingualNeural",
    "de-DE-KatjaNeural",
    "de-DE-ConradNeural",
    "fr-FR-DeniseNeural",
    "fr-FR-HenriNeural",
    "it-IT-ElsaNeural",
    "it-IT-DiegoNeural",
    "es-ES-ElviraNeural",
    "es-ES-AlvaroNeural",
}


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--text-base64", required=True)
    parser.add_argument("--voice", required=True, choices=sorted(VOICES))
    parser.add_argument("--probe", action="store_true")
    return parser.parse_args()


def decode_text(encoded: str) -> str:
    try:
        raw = base64.b64decode(encoded, validate=True)
        text = raw.decode("utf-8", errors="strict")
    except (binascii.Error, UnicodeDecodeError):
        raise ValueError("INVALID_TEXT") from None
    text = " ".join(text.split()).strip()
    if not text or len(text.encode("utf-8")) > MAX_TEXT_BYTES:
        raise ValueError("INVALID_TEXT")
    return text


async def synthesize(text: str, voice: str) -> bytes:
    # The consumer endpoint can end a valid turn without audio. A small,
    # bounded retry makes short interactive turns resilient without hiding a
    # persistent provider failure.
    for attempt in range(3):
        audio = bytearray()
        try:
            async for chunk in edge_tts.Communicate(text, voice).stream():
                if chunk.get("type") != "audio":
                    continue
                data = chunk.get("data", b"")
                if len(audio) + len(data) > MAX_AUDIO_BYTES:
                    raise ValueError("AUDIO_TOO_LARGE")
                audio.extend(data)
            if len(audio) >= 128:
                return bytes(audio)
        except ValueError:
            raise
        except Exception:
            pass
        if attempt < 2:
            await asyncio.sleep(0.4 * (attempt + 1))
    raise RuntimeError("NO_AUDIO")


async def main() -> int:
    args = arguments()
    try:
        text = decode_text(args.text_base64)
        audio = await asyncio.wait_for(synthesize(text, args.voice), timeout=38.0)
    except ValueError:
        return 2
    except (TimeoutError, RuntimeError):
        return 4
    if args.probe:
        print(len(audio))
    else:
        sys.stdout.buffer.write(audio)
        sys.stdout.buffer.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
