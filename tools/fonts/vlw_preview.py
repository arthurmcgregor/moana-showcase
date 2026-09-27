#!/usr/bin/env python3
"""Decode a .vlw and composite a sample string — proves the file, not the encoder.

Deliberately written against the format spec rather than by importing anything
from vlw_encode.py: a shared helper would hide a systematic misread of the
layout. Draws the way M5GFX drawChar() does — pen on the baseline, glyph placed
at (pen + dX, baseline - dY).
"""

import argparse
import struct
from pathlib import Path

from PIL import Image


def decode(path):
    blob = path.read_bytes()
    count, version, size, _mbox, ascent, descent = struct.unpack_from(">6I", blob, 0)
    glyphs, offset = {}, 24 + count * 28
    for i in range(count):
        cp, h, w, advance, dy, dx, _ = struct.unpack_from(">7I", blob, 24 + i * 28)
        dy = dy - (1 << 32) if dy >= (1 << 31) else dy
        dx = dx - (1 << 32) if dx >= (1 << 31) else dx
        glyphs[cp] = dict(w=w, h=h, advance=advance, dx=dx, dy=dy,
                          bits=blob[offset:offset + w * h])
        offset += w * h
    return dict(count=count, version=version, size=size, ascent=ascent,
                descent=descent, glyphs=glyphs, trailing=len(blob) - offset)


def render(font, text, pad=8):
    width = sum(font["glyphs"][ord(c)]["advance"] for c in text) + pad * 2
    height = font["ascent"] + font["descent"] + pad * 2
    canvas = Image.new("L", (width, height), 0)
    pen, baseline = pad, pad + font["ascent"]
    for ch in text:
        g = font["glyphs"][ord(ch)]
        if g["w"] and g["h"]:
            glyph = Image.frombytes("L", (g["w"], g["h"]), g["bits"])
            canvas.paste(glyph, (pen + g["dx"], baseline - g["dy"]))
        pen += g["advance"]
    return canvas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("vlw", type=Path)
    ap.add_argument("--text", default="12.3 SOG 0123456789")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    font = decode(args.vlw)
    print(f"{args.vlw.name}: v{font['version']} size={font['size']} "
          f"glyphs={font['count']} ascent={font['ascent']} descent={font['descent']} "
          f"trailing_bytes={font['trailing']}")

    advances = {c: font["glyphs"][ord(c)]["advance"] for c in "0123456789"}
    lo, hi = min(advances.values()), max(advances.values())
    print(f"digit advance: min={lo} max={hi} jitter={(hi - lo) / hi * 100:.1f}%")

    render(font, args.text).save(args.out)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
