"""Generate CYD's Chinese font subsets: python scripts/generate_zh_fonts.py FONT.ttf.

Source: google/fonts ofl/notosanssc/NotoSansSC[wght].ttf, SIL OFL 1.1.
Only translated screen text is included. Web UI uses the browser's fonts.
Latin glyphs keep the existing Inter faces and metrics.
"""
import re
import struct
import sys
from pathlib import Path

from generate_vlw_fonts import generate_vlw, vlw_to_header

ROOT = Path(__file__).resolve().parents[1]


def glyphs(blob):
    count = struct.unpack_from(">I", blob)[0]
    offset = 24 + count * 28
    result = {}
    for i in range(count):
        metric = blob[24 + i * 28:24 + (i + 1) * 28]
        code, height, width = struct.unpack_from(">III", metric)
        size = height * width
        result[code] = (metric, blob[offset:offset + size])
        offset += size
    return result


def main():
    source = Path(sys.argv[1])
    characters = set()
    for path in list((ROOT / "src").glob("display*.cpp")) + list((ROOT / "src").glob("clock*.cpp")) + [ROOT / "src" / "hms_lookup.h", ROOT / "src" / "hms_lookup.cpp", ROOT / "src" / "settings.cpp", ROOT / "include" / "bambu_state.h"]:
        for value in re.findall(r'BH_TEXT\("(?:\\.|[^"\\])*",\s*"((?:\\.|[^"\\])*)"\)', path.read_text(encoding="utf-8")):
            characters.update(ord(c) for c in value if ord(c) > 0x7F)
    for name, size, weight in [("inter_10", 12, 400), ("inter_14", 16, 400), ("inter_19", 22, 700), ("inter_card_lbl", 10, 700)]:
        original = (ROOT / "include" / "fonts" / (name + ".h")).read_text(encoding="utf-8")
        latin = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", original))
        chinese = generate_vlw(str(source), size, sorted(characters), weight)
        merged = glyphs(latin)
        # Chinese builds keep ASCII and the UI's degree / middot / euro signs.
        # Dropping unused extended Latin glyphs leaves space in CYD's OTA slot.
        merged = {c: g for c, g in merged.items() if c < 0x7F or c in (0xB0, 0xB7, 0x20AC)}
        added = glyphs(chinese)
        if set(added) != characters:
            raise ValueError("Source font does not cover every translated screen character")
        merged.update(added)
        ordered = [merged[c] for c in sorted(merged)]
        # Use actual ink bounds rather than Noto's generous face line spacing,
        # so the existing gauge and Card layouts retain their Inter line height.
        tops = [struct.unpack_from(">i", g[0], 16)[0] for g in ordered]
        bottoms = [struct.unpack_from(">I", g[0], 4)[0] - top for g, top in zip(ordered, tops)]
        ascent = max(struct.unpack_from(">I", latin, 16)[0], max(tops))
        descent = max(struct.unpack_from(">I", latin, 20)[0], max(bottoms))
        blob = struct.pack(">6I", len(ordered), 6, size, 0, ascent, descent)
        blob += b"".join(g[0] for g in ordered) + b"".join(g[1] for g in ordered)
        header = "// Noto Sans SC + Inter subset: SIL OFL 1.1, see fonts/OFL-NotoSansSC.txt and fonts/OFL-Inter.txt.\n" + vlw_to_header("zh_" + name, blob)
        (ROOT / "include" / "fonts" / ("zh_" + name + ".h")).write_text(header, encoding="utf-8", newline="\n")
        print(f"zh_{name}: {len(added)} extra glyphs, {len(blob)} bytes")


if __name__ == "__main__":
    main()
