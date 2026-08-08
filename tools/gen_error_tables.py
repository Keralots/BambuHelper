#!/usr/bin/env python3
"""Generate the embedded HMS / print_error text tables from Bambu's own feed.

Source of truth:  https://e.bambulab.com/query.php?lang=en

    data.device_hms.en[]    { ecode: <16 hex>, intro: <text> }   attr<<32 | code
    data.device_error.en[]  { ecode: <8 hex>,  intro: <text> }   print_error

Emits two generated headers consumed only by src/hms_lookup.cpp:

    include/error_tables/print_error_table.h    ~36 KB
    include/error_tables/hms_table.h            ~430 KB

Both use the same shape: a sorted key array (binary searched at runtime), a
parallel uint16 string id per key, and a single NUL-separated blob of deduped
strings addressed through an offset array.

Processing rules:
  * entries with a blank `intro` are DROPPED - the lookup treats them as absent
    and the caller falls back to the generic wording
  * identical strings are deduped across the whole table
  * a code that appears more than once with DIFFERENT text keeps the longest
    variant (ties broken lexicographically) - the feed has 376 such HMS codes
    and the longer string is consistently the more specific one
  * text is ASCII-folded: the bundled VLW pack is 322 glyphs and the feed
    carries fullwidth commas, ideographic stops, curly quotes and a degree sign
  * text is truncated to 200 chars on a word boundary, ellipsis included

Also writes the portal's text mirror:

    docs/errors/hms_en.json     {"ver": ..., "hms": {...}, "err": {...}}

The portal page reads that from GitHub Pages in the user's browser. It cannot
read Bambu's feed directly - e.bambulab.com sends no Access-Control-Allow-Origin
header at all - and the device cannot resolve text it has no table for. The
mirror keeps the untruncated, unfolded strings: the browser has none of the
322-glyph VLW limits the embedded tables are shaped around.

Never hand-edit the output. Re-run this script; the feed's `ver` stamp is
written into each header and into the mirror so refreshes diff cleanly.

Usage:
    python tools/gen_error_tables.py                  # fetch + write everything
    python tools/gen_error_tables.py --input feed.json
    python tools/gen_error_tables.py --dry-run        # stats only
    python tools/gen_error_tables.py --mirror-only    # refresh docs/ mirror only
"""

import argparse
import json
import os
import sys
import unicodedata
import urllib.request

FEED_URL = "https://e.bambulab.com/query.php?lang=en"
MAX_TEXT = 200          # stored chars per string, ellipsis included
ELLIPSIS = "..."

# Non-ASCII actually present in the feed, plus the neighbours likely to show up
# in a later refresh. Anything not listed falls through to NFKD + drop.
FOLD = {
    "，": ", ",     # fullwidth comma
    "。": ". ",     # ideographic full stop
    "：": ": ",     # fullwidth colon
    "；": "; ",     # fullwidth semicolon
    "（": " (",     # fullwidth parens
    "）": ") ",
    "“": '"',      # curly double quotes
    "”": '"',
    "‘": "'",      # curly single quotes
    "’": "'",
    "–": "-",      # en dash
    "—": "-",      # em dash
    "…": "...",    # ellipsis
    "°": "deg",    # degree sign
    "µ": "u",      # micro sign
    "×": "x",      # multiplication sign
    "≥": ">=",
    "≤": "<=",
    " ": " ",      # nbsp
}


def ascii_fold(text):
    """Map every non-ASCII character to its nearest ASCII equivalent."""
    out = []
    for ch in text:
        if ord(ch) < 128:
            out.append(ch)
            continue
        if ch in FOLD:
            out.append(FOLD[ch])
            continue
        # Strip diacritics, then drop whatever still will not fit in ASCII.
        decomposed = unicodedata.normalize("NFKD", ch)
        out.append("".join(c for c in decomposed if ord(c) < 128))
    folded = "".join(out)
    # Folding can introduce double spaces ("a, , b" from a fullwidth comma
    # already followed by a space).
    while "  " in folded:
        folded = folded.replace("  ", " ")
    return folded.strip()


def truncate(text):
    """Clamp to MAX_TEXT chars, breaking on a word boundary."""
    if len(text) <= MAX_TEXT:
        return text
    cut = text[: MAX_TEXT - len(ELLIPSIS)]
    space = cut.rfind(" ")
    if space > MAX_TEXT // 2:
        cut = cut[:space]
    return cut.rstrip(" ,.;:") + ELLIPSIS


def load_feed(path):
    if path:
        with open(path, "r", encoding="utf-8") as fh:
            return json.load(fh)
    req = urllib.request.Request(FEED_URL, headers={"User-Agent": "BambuHelper-table-gen"})
    with urllib.request.urlopen(req, timeout=60) as resp:
        return json.loads(resp.read().decode("utf-8"))


def collect(entries):
    """ecode -> best text. Blank intros drop out entirely."""
    best = {}
    conflicts = 0
    for entry in entries:
        ecode = entry["ecode"].strip().upper()
        text = truncate(ascii_fold(entry.get("intro", "")))
        if not text:
            continue
        prev = best.get(ecode)
        if prev is None:
            best[ecode] = text
        elif prev != text:
            conflicts += 1
            # Longest wins; lexicographic tie-break keeps output stable.
            if (len(text), text) > (len(prev), prev):
                best[ecode] = text
    return best, conflicts


def c_escape(text):
    return text.replace("\\", "\\\\").replace('"', '\\"')


def emit_blob(lines, name, strings):
    """One C string literal per entry, joined by standalone "\\0" literals.

    Adjacent literals concatenate into a single character sequence, and keeping
    the NUL in a literal of its own stops it from swallowing a following digit
    as an octal escape. The final implicit NUL terminates the last string.
    """
    lines.append("static const char %s[] PROGMEM =" % name)
    for i, text in enumerate(strings):
        sep = ' "\\0"' if i + 1 < len(strings) else ";"
        lines.append('  "%s"%s' % (c_escape(text), sep))
    lines.append("")


def emit_array(lines, decl, values, per_line, fmt):
    lines.append("%s = {" % decl)
    for i in range(0, len(values), per_line):
        chunk = values[i:i + per_line]
        lines.append("  " + " ".join(fmt % v for v in chunk))
    lines.append("};")
    lines.append("")


def build_header(domain, ver, best, key_bits):
    """Render one generated header. Returns (text, stats)."""
    codes = sorted(best, key=lambda c: int(c, 16))

    # Dedupe strings; first use (in sorted code order) defines the id.
    ids = {}
    strings = []
    for code in codes:
        text = best[code]
        if text not in ids:
            ids[text] = len(strings)
            strings.append(text)

    offsets = []
    pos = 0
    for text in strings:
        offsets.append(pos)
        pos += len(text) + 1  # + NUL separator

    prefix = domain.upper()
    key_type = "uint64_t" if key_bits == 64 else "uint32_t"
    key_fmt = "0x%016XULL," if key_bits == 64 else "0x%08XUL,"
    key_per_line = 4 if key_bits == 64 else 8

    lines = [
        "// %s error text table - GENERATED, DO NOT EDIT." % domain,
        "// Regenerate with: python tools/gen_error_tables.py",
        "//",
        "// Source: %s" % FEED_URL,
        "// Feed ver: %s" % ver,
        "// %u codes, %u unique strings, blank intros dropped." % (len(codes), len(strings)),
        "//",
        "// Keys are sorted ascending for binary search. Text is ASCII-folded and",
        "// truncated to %d chars; see the generator for the exact rules." % MAX_TEXT,
        "#ifndef %s_TABLE_H" % prefix,
        "#define %s_TABLE_H" % prefix,
        "",
        "#include <Arduino.h>",
        "",
        '#define %s_TABLE_VER   "%s"' % (prefix, ver),
        "#define %s_TABLE_COUNT %u" % (prefix, len(codes)),
        "#define %s_TEXT_COUNT  %u" % (prefix, len(strings)),
        "",
    ]

    emit_array(lines,
               "static const %s %s_KEYS[%s_TABLE_COUNT] PROGMEM" % (key_type, prefix, prefix),
               [int(c, 16) for c in codes], key_per_line, key_fmt)
    emit_array(lines,
               "static const uint16_t %s_TEXT_ID[%s_TABLE_COUNT] PROGMEM" % (prefix, prefix),
               [ids[best[c]] for c in codes], 12, "%u,")
    emit_array(lines,
               "static const uint32_t %s_TEXT_OFF[%s_TEXT_COUNT] PROGMEM" % (prefix, prefix),
               offsets, 10, "%u,")
    emit_blob(lines, "%s_TEXT_BLOB" % prefix, strings)

    lines.append("#endif  // %s_TABLE_H" % prefix)
    lines.append("")

    stats = {
        "codes": len(codes),
        "strings": len(strings),
        "blob": pos,
        "flash": pos + len(codes) * (key_bits // 8 + 2) + len(strings) * 4,
    }
    return "\n".join(lines), stats


def collect_raw(entries):
    """ecode -> best text, unfolded and untruncated. For the browser mirror."""
    best = {}
    for entry in entries:
        ecode = entry["ecode"].strip().upper()
        text = " ".join((entry.get("intro") or "").split())
        if not text:
            continue
        prev = best.get(ecode)
        # Same tie-break as the embedded tables so the two never disagree about
        # which variant of a duplicated code is the real one.
        if prev is None or (len(text), text) > (len(prev), prev):
            best[ecode] = text
    return best


def write_mirror(path, ver, data):
    doc = {
        "ver": ver,
        "hms": collect_raw(data["device_hms"]["en"]),
        "err": collect_raw(data["device_error"]["en"]),
    }
    os.makedirs(os.path.dirname(path), exist_ok=True)
    # Sorted keys and no spaces: stable diffs, smallest download.
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(doc, fh, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return doc, os.path.getsize(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--input", help="read the feed from a file instead of fetching")
    ap.add_argument("--out-dir", default=None,
                    help="output directory (default: <repo>/include/error_tables)")
    ap.add_argument("--dry-run", action="store_true", help="print stats, write nothing")
    ap.add_argument("--mirror-only", action="store_true",
                    help="refresh docs/errors/hms_en.json and skip the headers")
    args = ap.parse_args()

    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_dir = args.out_dir or os.path.join(repo, "include", "error_tables")
    mirror_path = os.path.join(repo, "docs", "errors", "hms_en.json")

    print("Loading feed...")
    feed = load_feed(args.input)
    if feed.get("result") not in (None, 0):
        print("Feed reported result=%r" % feed.get("result"), file=sys.stderr)
    data = feed["data"]
    ver = str(feed.get("ver") or data.get("device_hms", {}).get("ver") or "unknown")
    print("Feed ver %s" % ver)

    targets = [
        ("print_error", data["device_error"]["en"], 32, "print_error_table.h"),
        ("hms",         data["device_hms"]["en"],   64, "hms_table.h"),
    ]

    if not args.dry_run:
        os.makedirs(out_dir, exist_ok=True)

    if not args.dry_run:
        doc, size = write_mirror(mirror_path, ver, data)
        print("mirror       %u hms + %u print_error strings, %.1f KB"
              % (len(doc["hms"]), len(doc["err"]), size / 1024.0))
        print("             -> %s" % mirror_path)
    if args.mirror_only:
        return

    for domain, entries, key_bits, filename in targets:
        best, conflicts = collect(entries)
        text, stats = build_header(domain, ver, best, key_bits)
        dropped = len({e["ecode"].strip().upper() for e in entries}) - stats["codes"]
        print("%-12s %4u raw -> %4u codes (%u blank dropped, %u text conflicts), "
              "%u strings, blob %u B, ~%.1f KB flash"
              % (domain, len(entries), stats["codes"], dropped, conflicts,
                 stats["strings"], stats["blob"], stats["flash"] / 1024.0))
        if args.dry_run:
            continue
        path = os.path.join(out_dir, filename)
        with open(path, "w", encoding="ascii", newline="\n") as fh:
            fh.write(text)
        print("             -> %s (%u B)" % (path, os.path.getsize(path)))


if __name__ == "__main__":
    main()
