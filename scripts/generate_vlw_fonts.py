#!/usr/bin/env python3
"""Generate TFT_eSPI-compatible VLW font PROGMEM headers from a TTF file.

VLW format (Bodmer/TFT_eSPI smooth font):
  Header:  glyph_count(u32) | version(u32=6) | font_size(u32) | padding(u32)
           ascent(u32) | descent(u32)
  Per glyph metrics (28 bytes each, 7 x uint32 big-endian):
           unicode(u32) | height(u32) | width(u32) | advance(u32)
           top_offset(i32) | left_offset(i32) | padding(u32, ignored by reader)
  Glyph bitmaps: 8-bit alpha, row-major, width*height bytes each.

Usage:
  python scripts/generate_vlw_fonts.py
"""

import struct
import sys
from pathlib import Path

try:
    import freetype
except ImportError:
    print("Install freetype-py:  pip install freetype-py")
    sys.exit(1)

# Character set: printable ASCII + degree symbol
CHARSET = list(range(0x20, 0x7F)) + [0xB0]

# Font sizes to generate: (name, ttf_filename, pixel_size)
# Mixed weights match TFT_eSPI bitmap fonts: Font 1/2 were medium, Font 4 was bold.
FONTS = [
    ("inter_10", "Inter-Regular.ttf", 12),
    ("inter_14", "Inter-Regular.ttf", 16),
    ("inter_19", "Inter-Bold.ttf",    22),
]

FONTS_DIR = Path(__file__).parent.parent / "fonts"
OUT_DIR = Path(__file__).parent.parent / "include" / "fonts"


def generate_vlw(ttf_path: str, pixel_size: int) -> bytes:
    """Generate VLW binary data for the given font and size."""
    face = freetype.Face(str(ttf_path))
    face.set_pixel_sizes(0, pixel_size)

    glyphs = []
    for codepoint in CHARSET:
        face.load_char(chr(codepoint), freetype.FT_LOAD_RENDER)
        g = face.glyph
        bmp = g.bitmap

        width = bmp.width
        height = bmp.rows
        advance = g.advance.x >> 6  # 26.6 fixed point to pixels
        top_offset = g.bitmap_top
        left_offset = g.bitmap_left

        # Extract 8-bit alpha bitmap
        if width > 0 and height > 0:
            alpha = bytes(bmp.buffer)
            # Handle pitch != width (padding per row)
            if bmp.pitch != width:
                alpha = b""
                for row in range(height):
                    start = row * bmp.pitch
                    alpha += bytes(bmp.buffer[start:start + width])
        else:
            alpha = b""

        glyphs.append({
            "unicode": codepoint,
            "height": height,
            "width": width,
            "advance": advance,
            "top_offset": top_offset,
            "left_offset": left_offset,
            "bitmap": alpha,
        })

    # Compute font metrics
    ascent = face.size.ascender >> 6
    descent = -(face.size.descender >> 6)  # TFT_eSPI expects positive descent

    # Build VLW binary
    buf = bytearray()

    # Header (6 x uint32 big-endian)
    glyph_count = len(glyphs)
    buf += struct.pack(">I", glyph_count)
    buf += struct.pack(">I", 6)  # version
    buf += struct.pack(">I", pixel_size)
    buf += struct.pack(">I", 0)  # padding
    buf += struct.pack(">I", ascent)
    buf += struct.pack(">I", descent)

    # Glyph metrics table (28 bytes each — 7 x uint32)
    # TFT_eSPI reads: unicode, height, width, xAdvance, dY, dX, padding(ignored)
    for g in glyphs:
        buf += struct.pack(">I", g["unicode"])
        buf += struct.pack(">I", g["height"])
        buf += struct.pack(">I", g["width"])
        buf += struct.pack(">I", g["advance"])
        buf += struct.pack(">i", g["top_offset"])
        buf += struct.pack(">i", g["left_offset"])
        buf += struct.pack(">I", 0)  # padding — TFT_eSPI reads and discards

    # Glyph bitmaps (sequential, same order as metrics)
    for g in glyphs:
        buf += g["bitmap"]

    return bytes(buf)


def vlw_to_header(name: str, vlw_data: bytes) -> str:
    """Convert VLW binary to a C PROGMEM header."""
    lines = [
        f"// Auto-generated VLW font: {name}",
        f"// Size: {len(vlw_data)} bytes ({len(vlw_data) / 1024:.1f} KB)",
        "#pragma once",
        "#include <pgmspace.h>",
        "",
        f"const uint8_t {name}[] PROGMEM = {{",
    ]

    # Emit bytes, 16 per line
    for i in range(0, len(vlw_data), 16):
        chunk = vlw_data[i:i + 16]
        hex_vals = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"    {hex_vals},")

    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    for name, ttf_filename, size in FONTS:
        ttf_path = FONTS_DIR / ttf_filename
        if not ttf_path.exists():
            print(f"TTF not found: {ttf_path}")
            sys.exit(1)

        print(f"Generating {name} ({ttf_filename} @ {size}px)...", end=" ")
        vlw_data = generate_vlw(str(ttf_path), size)
        header = vlw_to_header(name, vlw_data)

        out_path = OUT_DIR / f"{name}.h"
        # Force LF line endings so generated headers are consistent across
        # platforms and don't trigger `git diff --check` whitespace warnings.
        out_path.write_bytes(header.encode("utf-8"))
        print(f"{len(vlw_data)} bytes -> {out_path}")

    print("Done.")


if __name__ == "__main__":
    main()
