#!/usr/bin/env python3
"""Generate the embedded HMS / print_error text tables from Bambu's own feed.

Source of truth:  https://e.bambulab.com/query.php?lang=en

    data.device_hms.en[]    { ecode: <16 hex>, intro: <text> }   attr<<32 | code
    data.device_error.en[]  { ecode: <8 hex>,  intro: <text> }   print_error

Emits three generated headers consumed only by src/hms_lookup.cpp:

    include/error_tables/print_error_table.h    ~36 KB
    include/error_tables/hms_table.h            ~430 KB
    include/error_tables/hms_known.h            ~12 KB

The first two use the same shape: a sorted key array (binary searched at
runtime), a parallel uint16 string id per key, and a single NUL-separated blob
of deduped strings addressed through an offset array.

hms_known.h is the same key set as hms_table.h with the text thrown away, in a
form small enough to compile into a 4 MB board. It answers one question - "does
Bambu describe this code at all" - which is what decides whether an HMS entry
reaches the screen. Codes Bambu registers but ships no sentence for are not
shown by Bambu's own printer UI or by Bambu Studio, and showing them was issue
#164: an X2D reported 0500_0100_0002_000B and 0503_0000_0003_0027, neither of
which the printer itself lists. Boards carrying hms_table.h never compile this
header - their key array already is the answer.

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

    docs/errors/hms_en.json     {"ver": ..., "hms": {...}, "err": {...}, "wiki": {...}}

`wiki` maps a code to the path of its Bambu wiki page, relative to `/en/`. The
URL is NOT derivable from the code: the page sits under a per-model segment
(x1, a1, a1-mini, a2l, h2d, ams-ht, ...) that appears nowhere in the code, which
is why the old fixed `/en/x1/...` link 404'd for most codes. The stored value is
the href exactly as the wiki writes it, separators and case included - 11 of the
index's links put "-" between the code groups instead of "_", so rebuilding the
tail from the code would be a guess. Scraped from the wiki's own HMS index;
codes missing from it have no page at all (only ~225 of 3958 are documented) and
the portal links to the index instead.

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
    python tools/gen_error_tables.py --known-only     # rebuild hms_known.h from
                                                      # the mirror already in the
                                                      # repo, fetching nothing
"""

import argparse
import json
import os
import re
import sys
import unicodedata
import urllib.request

FEED_URL = "https://e.bambulab.com/query.php?lang=en"
MAX_TEXT = 200          # stored chars per string, ellipsis included
ELLIPSIS = "..."

# The wiki's own index of every documented HMS code. Every per-code page is
# linked from here, which is the only place the model path segment can be
# learned. A plain urllib UA is refused outright, so send a browser one.
WIKI_INDEX_URL = "https://wiki.bambulab.com/en/hms/home"
WIKI_UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
           "(KHTML, like Gecko) Chrome/120.0 Safari/537.36")
# href="/en/<model>/troubleshooting/hmscode/<CODE>" - the index uses both "_"
# and "-" between the four groups, and mixed case in the hex.
WIKI_HREF_RE = re.compile(
    r'href="/en/([A-Za-z0-9-]+)/troubleshooting/hmscode/'
    r'([0-9A-Fa-f]{4}[_-][0-9A-Fa-f]{4}[_-][0-9A-Fa-f]{4}[_-][0-9A-Fa-f]{4})"')

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


# hms_known.h packs each code's severity and subcode into one uint16. The feed
# has never carried a severity above 3 or a subcode above 0x0086, so 2 + 14 bits
# is generous - but a feed refresh that broke either assumption would silently
# corrupt the set, so the generator asserts instead of masking.
KNOWN_SEV_MAX = 3
KNOWN_SUB_MAX = 0x3FFF


def build_known_header(ver, codes):
    """Render include/error_tables/hms_known.h. Returns (text, stats).

    Grouped by `attr` rather than kept as flat 64-bit keys: 705 attrs own 3958
    codes, so paying 4 bytes per attr once and 2 bytes per code beats 8 bytes
    per key three times over (12.1 KB against 31.7 KB), and a 4 MB board has no
    room for the difference. Lookup is a binary search over the attr array for
    the run, then a binary search inside the run.
    """
    groups = {}
    for code in codes:
        key = int(code, 16)
        attr, low = key >> 32, key & 0xFFFFFFFF
        sev, sub = low >> 16, low & 0xFFFF
        if sev > KNOWN_SEV_MAX or sub > KNOWN_SUB_MAX:
            raise SystemExit(
                "hms_known: %s does not fit the 2+14-bit packing (sev=%d, sub=0x%04X). "
                "Widen the packing in tools/gen_error_tables.py and in "
                "hmsIsDescribed() together." % (code, sev, sub))
        groups.setdefault(attr, []).append((sev << 14) | sub)

    attrs = sorted(groups)
    idx, packed = [], []
    for attr in attrs:
        idx.append(len(packed))
        packed.extend(sorted(groups[attr]))
    idx.append(len(packed))          # one past the end, so every run is idx[i]..idx[i+1]

    lines = [
        "// HMS known-code set - GENERATED, DO NOT EDIT.",
        "// Regenerate with: python tools/gen_error_tables.py --known-only",
        "//",
        "// Source: %s (via docs/errors/hms_en.json)" % FEED_URL,
        "// Feed ver: %s" % ver,
        "// %u codes across %u attr groups - the same key set as hms_table.h with"
        % (len(packed), len(attrs)),
        "// the text dropped. Compiled only where hms_table.h is not: there, its own",
        "// key array answers the same question for free.",
        "//",
        "// Each code is packed as (severity << 14) | subcode. Runs are sorted, so a",
        "// lookup is a binary search for the attr followed by one inside its run.",
        "#ifndef HMS_KNOWN_H",
        "#define HMS_KNOWN_H",
        "",
        "#include <Arduino.h>",
        "",
        '#define HMS_KNOWN_VER         "%s"' % ver,
        "#define HMS_KNOWN_ATTR_COUNT  %u" % len(attrs),
        "#define HMS_KNOWN_CODE_COUNT  %u" % len(packed),
        "#define HMS_KNOWN_SEV_SHIFT   14",
        "#define HMS_KNOWN_SUB_MASK    0x%04X" % KNOWN_SUB_MAX,
        "",
    ]

    emit_array(lines, "static const uint32_t HMS_KNOWN_ATTR[HMS_KNOWN_ATTR_COUNT] PROGMEM",
               attrs, 8, "0x%08XUL,")
    emit_array(lines, "static const uint16_t HMS_KNOWN_IDX[HMS_KNOWN_ATTR_COUNT + 1] PROGMEM",
               idx, 12, "%u,")
    emit_array(lines, "static const uint16_t HMS_KNOWN_CODE[HMS_KNOWN_CODE_COUNT] PROGMEM",
               packed, 12, "0x%04X,")

    lines.append("#endif  // HMS_KNOWN_H")
    lines.append("")

    stats = {
        "attrs": len(attrs),
        "codes": len(packed),
        "flash": len(attrs) * 4 + (len(attrs) + 1) * 2 + len(packed) * 2,
    }
    return "\n".join(lines), stats


def shipped_table_ver(out_dir):
    """Feed stamp of the hms_table.h currently in the tree, or None."""
    path = os.path.join(out_dir, "hms_table.h")
    try:
        with open(path, "r", encoding="ascii") as fh:
            head = fh.read(4096)
    except OSError:
        return None
    m = re.search(r'#define\s+HMS_TABLE_VER\s+"([^"]+)"', head)
    return m.group(1) if m else None


def write_known(out_dir, ver, codes):
    text, stats = build_known_header(ver, codes)
    path = os.path.join(out_dir, "hms_known.h")
    os.makedirs(out_dir, exist_ok=True)
    with open(path, "w", encoding="ascii", newline="\n") as fh:
        fh.write(text)
    print("hms_known    %4u codes in %u attr groups, ~%.1f KB flash"
          % (stats["codes"], stats["attrs"], stats["flash"] / 1024.0))
    print("             -> %s (%u B)" % (path, os.path.getsize(path)))


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


def fetch_wiki_index():
    """code (16 hex, no separators, upper) -> page path relative to /en/.

    A wiki outage must never sink a table refresh, so every failure degrades to
    an empty map: the portal then links every row at the HMS index, which is
    exactly what it does for an undocumented code anyway.
    """
    try:
        req = urllib.request.Request(WIKI_INDEX_URL, headers={"User-Agent": WIKI_UA})
        with urllib.request.urlopen(req, timeout=60) as resp:
            html = resp.read().decode("utf-8", "replace")
    except Exception as exc:                                # noqa: BLE001
        print("WARNING: wiki index unreachable (%s) - links fall back to the "
              "index page" % exc, file=sys.stderr)
        return {}
    index = {}
    for model, code in WIKI_HREF_RE.findall(html):
        # First link wins: a code listed under several models resolves under
        # each of them, so any one of them is a working link. The value keeps
        # the href verbatim - separator and case are the wiki's, not ours.
        key = code.upper().replace("-", "").replace("_", "")
        index.setdefault(key, "%s/troubleshooting/hmscode/%s" % (model, code))
    if not index:
        print("WARNING: wiki index parsed to 0 codes - page layout changed?",
              file=sys.stderr)
    return index


def write_mirror(path, ver, data, wiki):
    hms = collect_raw(data["device_hms"]["en"])
    doc = {
        "ver": ver,
        "hms": hms,
        "err": collect_raw(data["device_error"]["en"]),
        # Only codes we actually ship text for - the index also lists codes the
        # feed has never carried, and they would just bloat the download.
        "wiki": {code: model for code, model in wiki.items() if code in hms},
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
    ap.add_argument("--known-only", action="store_true",
                    help="rebuild hms_known.h from the mirror already in the repo "
                         "and fetch nothing - use this to refresh the known-code "
                         "set without disturbing the shipped text tables")
    args = ap.parse_args()

    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_dir = args.out_dir or os.path.join(repo, "include", "error_tables")
    mirror_path = os.path.join(repo, "docs", "errors", "hms_en.json")

    # The mirror is the shipped snapshot, in the repo, and its `hms` keys are
    # exactly the codes the tables carry text for - so it can regenerate the
    # known-code set on its own. Keeping the two in step matters more than
    # freshness: a device that suppressed a code the tables can describe, or
    # listed one they cannot, would be worse than a week-old set.
    #
    # Which is why this mode refuses when they have already drifted. release.py
    # rewrites the mirror on every release (--mirror-only) and never touches
    # hms_table.h, so after any release the mirror can be newer than the text
    # table. Regenerating the key set from it then would ship a build where a
    # 4 MB board lists a code and a 8/16 MB board suppresses the same one -
    # hmsIsDescribed() reads the key set on the first and the text table on the
    # second. Same printer, same fault, opposite behaviour by board.
    if args.known_only:
        with open(mirror_path, "r", encoding="utf-8") as fh:
            doc = json.load(fh)
        mirror_ver = str(doc["ver"])
        table_ver = shipped_table_ver(out_dir)
        if table_ver is not None and table_ver != mirror_ver:
            raise SystemExit(
                "refusing to write hms_known.h: the mirror is ver %s but "
                "include/error_tables/hms_table.h is ver %s. The key set and the "
                "text table must come from one snapshot. Run a full "
                "`python tools/gen_error_tables.py` to move both, or check out "
                "the mirror that matches the tables."
                % (mirror_ver, table_ver))
        print("Mirror ver %s (%u hms codes)" % (mirror_ver, len(doc["hms"])))
        write_known(out_dir, mirror_ver, doc["hms"].keys())
        return

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
        print("Loading wiki index...")
        wiki = fetch_wiki_index()
        doc, size = write_mirror(mirror_path, ver, data, wiki)
        print("mirror       %u hms + %u print_error strings, %u wiki links "
              "(%u indexed), %.1f KB"
              % (len(doc["hms"]), len(doc["err"]), len(doc["wiki"]), len(wiki),
                 size / 1024.0))
        print("             -> %s" % mirror_path)
    if args.mirror_only:
        # hms_known.h is deliberately NOT written here. It is a source file the
        # firmware compiles in, and release.py calls this mode after the binaries
        # are already built - writing it would leave the shipped image judging
        # codes by one feed snapshot and describing them from another.
        return

    hms_best = None
    for domain, entries, key_bits, filename in targets:
        best, conflicts = collect(entries)
        if domain == "hms":
            hms_best = best
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

    if not args.dry_run:
        # Last, and from the mirror written above rather than from the feed, so
        # the key set a board suppresses by and the text it describes with come
        # out of one snapshot. They must never drift apart: a code in one and not
        # the other either hides a real error or resurrects a silent one.
        #
        # The two are built by different filters, so "same snapshot" is not the
        # same as "same keys": collect() drops an entry whose text is empty after
        # ASCII folding and truncation, collect_raw() drops it when the raw text
        # is empty. An intro made entirely of characters ascii_fold discards
        # would survive one and die in the other, and the resulting key would be
        # listed on a 4 MB board and suppressed on an 8/16 MB one. It has never
        # happened on any feed we have seen; checking costs nothing.
        only_mirror = sorted(set(doc["hms"]) - set(hms_best))
        only_table = sorted(set(hms_best) - set(doc["hms"]))
        if only_mirror or only_table:
            raise SystemExit(
                "hms_table.h and the mirror disagree about which codes have "
                "text - the embedded and browser filters diverged.\n"
                "  mirror only (%u): %s\n"
                "  table only  (%u): %s\n"
                "Fix ascii_fold()/truncate() or collect_raw() so both drop the "
                "same entries before regenerating."
                % (len(only_mirror), only_mirror[:10],
                   len(only_table), only_table[:10]))
        write_known(out_dir, ver, doc["hms"].keys())


if __name__ == "__main__":
    main()
