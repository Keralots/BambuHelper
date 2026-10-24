"""Store bilingual templates as independent 4 KB raw-DEFLATE blocks.

Keep the original C++ feature gates, and never split a template placeholder.
ESP32's ROM inflater expands one block at a time, without a 32 KB dictionary.
"""
import re
import zlib

BLOCK_SIZE = 4096
PLACEHOLDER = re.compile(rb"%[A-Z][A-Z0-9_]{0,28}%")
TOKEN = re.compile(r'static const char (PAGE_\w+)\[\] PROGMEM =|R"rawliteral\(([\s\S]*?)\)rawliteral"(\s*;)?')


def blocks(raw):
    start = 0
    placeholders = list(PLACEHOLDER.finditer(raw))
    while start < len(raw):
        end = min(start + BLOCK_SIZE, len(raw))
        for token in placeholders:
            if token.start() < end < token.end():
                end = token.start()
                break
        piece = raw[start:end]
        compressor = zlib.compressobj(9, wbits=-15)
        yield compressor.compress(piece) + compressor.flush(), len(piece)
        start = end


def header(source, suffix):
    definitions = []
    current = ""

    def replace(match):
        nonlocal current
        if match[1]:
            current = match[1]
            if current == "PAGE_AP_HTML":
                return f"static const TemplateSegment PAGE_AP_{suffix}[] = {{"
            if current == "PAGE_HTML_1":
                return f"static const TemplateSegment PAGE_MAIN_{suffix}[] = {{"
            return ""
        references = []
        for blob, length in blocks(match[2].encode("utf-8")):
            name = f"WEB_PAGE_{suffix}_{len(definitions)}"
            data = ",".join(f"0x{x:02x}" for x in blob)
            definitions.append(f"static const uint8_t {name}[] PROGMEM = {{{data}}};")
            references.append(f"  {{(const char*){name}, {len(blob)}, {length}}},")
        if match[3] and current in ("PAGE_AP_HTML", "PAGE_HTML_2"):
            references.append("};")
        return "\n".join(references)

    transformed = TOKEN.sub(replace, source)
    return "\n".join(definitions) + "\n" + transformed
