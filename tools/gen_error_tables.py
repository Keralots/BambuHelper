#!/usr/bin/env python3
"""Generate the embedded HMS / print_error text tables from Bambu's own feeds.

Source of truth, one feed per printer model (<sn> = first 3 serial characters):

    https://e.bambulab.com/query.php?lang=en&d=<sn>

    data.device_hms.en[]    { ecode: <16 hex>, intro: <text> }   attr<<32 | code
    data.device_error.en[]  { ecode: <8 hex>,  intro: <text> }   print_error

Without `d=` the server answers with the X1C feed. It lacks most H2/H3/X2D/P3C
and AMS lite codes, and describes codes another model blanks on purpose (issue
#211), so the tables are the union of every model feed with a per-code record of
which models describe it. Bambu Studio picks the feed the same way.

Emits generated headers consumed only by src/hms_lookup.cpp:

    include/error_tables/error_models.h          serial prefix -> model index
    include/error_tables/hms_table.h             ~520 KB  8/16 MB boards
    include/error_tables/print_error_table.h     ~170 KB  8/16 MB boards
    include/error_tables/hms_known.h             ~6 KB    4 MB boards
    include/error_tables/print_error_lite.h      ~36 KB   4 MB boards except C3

The full tables hold one row per (code, text): a sorted key array with repeats,
a model-mask id and a string id per row, and a deduped NUL-separated blob. The
first row of a code is its default text, used when the model is unknown.

hms_known.h answers one question - "does Bambu describe this code for this
model" - which is what decides whether an HMS entry reaches the screen (issue
#164). It carries no severity-1 codes, because FATAL is never suppressed.

print_error_lite.h is the X1C feed alone: it is what a 4 MB app slot has room
for. The portal resolves everything else from the mirror.

Processing rules:
  * entries with a blank `intro` are DROPPED - the lookup treats them as absent
  * a code listed more than once in ONE feed with different text keeps the
    longest variant (ties broken lexicographically)
  * between models nothing is merged - "right extruder" wording must not reach a
    single-nozzle printer
  * text is ASCII-folded: the bundled VLW pack is 322 glyphs and the feed
    carries fullwidth commas, ideographic stops, curly quotes and a degree sign
  * text is truncated to 200 chars on a word boundary, ellipsis included

Also writes the portal's text mirror:

    docs/errors/hms_en.json     {"ver", "hms": {...}, "err": {...},
                                 "alt": {"hms": {...}, "err": {...}}, "wiki": {...}}

`hms` / `err` map a code to its default text, the shape every fielded portal
reads. `alt` maps a code to {text: "sn,sn,..."} for models whose text differs.

`wiki` maps a code to the path of its Bambu wiki page, relative to `/en/`. The
URL is NOT derivable from the code: the page sits under a per-model segment
(x1, a1, a1-mini, a2l, h2d, ams-ht, ...) that appears nowhere in the code, which
is why the old fixed `/en/x1/...` link 404'd for most codes. The stored value is
the href exactly as the wiki writes it, separators and case included. Codes
missing from it have no page at all and the portal links to the index instead.

The portal page reads the mirror from GitHub Pages in the user's browser. It
cannot read Bambu's feed directly - e.bambulab.com sends no
Access-Control-Allow-Origin header at all - and the device cannot resolve text it
has no table for. The mirror keeps the untruncated, unfolded strings.

Never hand-edit the output. Re-run this script; the feed's `ver` stamp is
written into each header and into the mirror so refreshes diff cleanly.

Usage:
    python tools/gen_error_tables.py                  # fetch + write everything
    python tools/gen_error_tables.py --save DIR       # ... and keep the feeds
    python tools/gen_error_tables.py --input DIR      # read <sn>.json from DIR
    python tools/gen_error_tables.py --dry-run        # stats only
"""

import argparse
import json
import os
import re
import sys
import unicodedata
import urllib.request

FEED_URL = "https://e.bambulab.com/query.php?lang=en"

# Serial prefix -> model, from Bambu Studio resources/printers/*.json
# (sn_prefix / display_name). Add a model here when Studio does. N8 (35F) is
# left out on purpose: unreleased, and its 219-code feed would suppress almost
# everything - a prefix not listed here is judged against the whole union.
MODELS = [
    ("00M", "X1C"), ("00W", "X1"), ("03W", "X1E"),
    ("01S", "P1P"), ("01P", "P1S"), ("22E", "P2S"), ("331", "P3C"),
    ("030", "A1 mini"), ("039", "A1"), ("26A", "A2L"),
    ("093", "H2S"), ("094", "H2D"), ("239", "H2D Pro"), ("31B", "H2C"),
    ("32X", "H3C"), ("20P", "X2D"),
]
DEFAULT_MODEL = "00M"   # its text is the default variant; also the lite table
MIN_HMS_CODES = 1500    # smallest real feed is ~1900; an unknown prefix returns 0

MAX_TEXT = 200          # stored chars per string, ellipsis included
ELLIPSIS = "..."

# The wiki's own list of documented HMS codes, per model and paged (the HMS home
# page renders it from this API). It is the only place the model path segment of
# a page can be learned. Both "_" and "-" separators occur.
WIKI_API_URL = "https://e.bambulab.com/wikijs/GetHmsWikiList.php?lang=en&size=1000"
WIKI_MAX_ID = 40        # its model ids are sequential; stop at the first repeat
WIKI_UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
           "(KHTML, like Gecko) Chrome/120.0 Safari/537.36")
WIKI_PAGE_RE = re.compile(
    r'^https://wiki\.bambulab\.com/en/([A-Za-z0-9-]+/troubleshooting/hmscode/'
    r'[0-9A-Fa-f]{4}(?:[_-][0-9A-Fa-f]{4}){3})$')

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
    "‑": "-",      # non-breaking hyphen
    "–": "-",      # en dash
    "—": "-",      # em dash
    "…": "...",    # ellipsis
    "°": "deg",    # degree sign
    "µ": "u",      # micro sign
    "×": "x",      # multiplication sign
    "→": "->",     # rightwards arrow
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
    # already followed by a space), and the H3/P3 feeds carry raw newlines.
    return " ".join(folded.split())


def truncate(text):
    """Clamp to MAX_TEXT chars, breaking on a word boundary."""
    if len(text) <= MAX_TEXT:
        return text
    cut = text[: MAX_TEXT - len(ELLIPSIS)]
    space = cut.rfind(" ")
    if space > MAX_TEXT // 2:
        cut = cut[:space]
    return cut.rstrip(" ,.;:") + ELLIPSIS


def load_feeds(in_dir, save_dir):
    """serial prefix -> feed dict. All models or nothing.

    A missing or truncated feed would read as "this model describes nothing" and
    silently suppress its errors, so any problem aborts the run.
    """
    feeds = {}
    for sn, name in MODELS:
        if in_dir:
            with open(os.path.join(in_dir, sn + ".json"), "r", encoding="utf-8") as fh:
                raw = fh.read()
        else:
            req = urllib.request.Request("%s&d=%s" % (FEED_URL, sn),
                                         headers={"User-Agent": "BambuHelper-table-gen"})
            with urllib.request.urlopen(req, timeout=60) as resp:
                raw = resp.read().decode("utf-8")
        feed = json.loads(raw)
        data = feed.get("data") or {}
        n = len(collect((data.get("device_hms") or {}).get("en", []))[0])
        if feed.get("result") not in (None, 0) or n < MIN_HMS_CODES \
                or not (data.get("device_error") or {}).get("en"):
            raise SystemExit("feed for %s (%s) is unusable: result=%r, %d described "
                             "HMS codes" % (sn, name, feed.get("result"), n))
        if save_dir:
            os.makedirs(save_dir, exist_ok=True)
            with open(os.path.join(save_dir, sn + ".json"), "w", encoding="utf-8",
                      newline="\n") as fh:
                fh.write(raw)
        feeds[sn] = feed
        print("  %-4s %-8s %4d hms described" % (sn, name, n))
    return feeds


def feed_ver(feed):
    return str(feed.get("ver") or feed["data"]["device_hms"].get("ver") or "unknown")


def by_code(per_model):
    """{sn: {code: text}} -> {code: {sn: text}}, codes in sorted order."""
    out = {}
    for sn, _ in MODELS:
        for code, text in per_model[sn].items():
            out.setdefault(code, {})[sn] = text
    return {c: out[c] for c in sorted(out, key=lambda c: int(c, 16))}


def variants(texts):
    """{sn: text} -> [(text, [sn, ...])], default variant first."""
    groups = {}
    for sn, text in texts.items():
        groups.setdefault(text, []).append(sn)
    return sorted(groups.items(),
                  key=lambda g: (DEFAULT_MODEL not in g[1], -len(g[1]), -len(g[0]), g[0]))


def model_classes(*domains):
    """sn -> index. Models with identical feeds share one mask bit."""
    seen, index = {}, {}
    for sn, _ in MODELS:
        sig = tuple(json.dumps(d[sn], sort_keys=True) for d in domains)
        index[sn] = seen.setdefault(sig, len(seen))
    if len(seen) > 32:
        raise SystemExit("more than 32 distinct models - widen the uint32 masks")
    return index


def mask_of(sns, index):
    mask = 0
    for sn in sns:
        mask |= 1 << index[sn]
    return mask


def build_models_header(ver, index):
    lines = [
        "// Serial prefix -> model index - GENERATED, DO NOT EDIT.",
        "// Regenerate with: python tools/gen_error_tables.py",
        "//",
        "// Feed ver: %s" % ver,
        "// The index is a bit position in the model masks of the other tables.",
        "// Models whose feeds are identical share one.",
        "#ifndef ERROR_MODELS_H",
        "#define ERROR_MODELS_H",
        "",
        "#include <Arduino.h>",
        "",
        "#define ERROR_MODEL_COUNT %u" % len(MODELS),
        "",
        "static const char ERROR_MODEL_SN[ERROR_MODEL_COUNT][4] PROGMEM = {",
    ]
    lines += ['  "%s",  // %s' % m for m in MODELS]
    lines += ["};",
              "static const uint8_t ERROR_MODEL_IDX[ERROR_MODEL_COUNT] PROGMEM = {",
              "  " + " ".join("%u," % index[sn] for sn, _ in MODELS),
              "};", "", "#endif  // ERROR_MODELS_H", ""]
    return "\n".join(lines)


class MaskPool:
    """Distinct model masks, addressed by a uint8 id."""

    def __init__(self):
        self.ids = {}

    def id(self, mask):
        mid = self.ids.setdefault(mask, len(self.ids))
        if mid > 0xFF:
            raise SystemExit("more than 256 distinct model masks - widen the uint8 ids")
        return mid

    def emit(self, lines, prefix):
        emit_array(lines, "static const uint32_t %s_MASKS[%u] PROGMEM"
                   % (prefix, len(self.ids)), list(self.ids), 8, "0x%08XUL,")


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


def build_header(domain, ver, table, key_bits, index):
    """Render one generated header from {code: {sn: text}}.

    index=None renders the lite form: one row per code, no model masks.
    Returns (text, stats).
    """
    rows = []          # (code, mask, text), default variant first within a code
    for code, texts in table.items():
        for text, sns in variants(texts):
            rows.append((code, mask_of(sns, index) if index else 0, text))

    # Dedupe strings; first use (in sorted code order) defines the id.
    ids = {}
    strings = []
    for _, _, text in rows:
        if text not in ids:
            ids[text] = len(strings)
            strings.append(text)
    if len(rows) > 0xFFFE:
        raise SystemExit("%s: too many rows for uint16 indices" % domain)

    offsets = []
    pos = 0
    for text in strings:
        offsets.append(pos)
        pos += len(text) + 1  # + NUL separator

    prefix = domain.upper()
    key_type = "uint64_t" if key_bits == 64 else "uint32_t"
    key_fmt = "0x%016XULL," if key_bits == 64 else "0x%08XUL,"
    key_per_line = 4 if key_bits == 64 else 8
    guard = "%s_%s_H" % (prefix, "TABLE" if index else "LITE")

    lines = [
        "// %s error text table - GENERATED, DO NOT EDIT." % domain,
        "// Regenerate with: python tools/gen_error_tables.py",
        "//",
        "// Source: %s%s" % (FEED_URL, "&d=<model>, every model" if index
                             else "&d=%s" % DEFAULT_MODEL),
        "// Feed ver: %s" % ver,
        "// %u codes, %u rows, %u unique strings, blank intros dropped."
        % (len(table), len(rows), len(strings)),
        "//",
        "// Keys are sorted ascending for binary search" +
        ("; a code whose text differs" if index else "."),
    ]
    if index:
        lines.append("// between models repeats, default text first.")
    lines += [
        "// Text is ASCII-folded and truncated to %d chars; see the generator." % MAX_TEXT,
        "#ifndef %s" % guard,
        "#define %s" % guard,
        "",
        "#include <Arduino.h>",
        "",
        '#define %s_TABLE_VER   "%s"' % (prefix, ver),
        "#define %s_TABLE_COUNT %u" % (prefix, len(rows)),
        "#define %s_TEXT_COUNT  %u" % (prefix, len(strings)),
        "",
    ]

    emit_array(lines,
               "static const %s %s_KEYS[%s_TABLE_COUNT] PROGMEM" % (key_type, prefix, prefix),
               [int(c, 16) for c, _, _ in rows], key_per_line, key_fmt)
    masks = MaskPool()
    if index:
        emit_array(lines, "static const uint8_t %s_MASK_ID[%s_TABLE_COUNT] PROGMEM"
                   % (prefix, prefix), [masks.id(m) for _, m, _ in rows], 20, "%u,")
        masks.emit(lines, prefix)
    emit_array(lines,
               "static const uint16_t %s_TEXT_ID[%s_TABLE_COUNT] PROGMEM" % (prefix, prefix),
               [ids[t] for _, _, t in rows], 12, "%u,")
    emit_array(lines,
               "static const uint32_t %s_TEXT_OFF[%s_TEXT_COUNT] PROGMEM" % (prefix, prefix),
               offsets, 10, "%u,")
    emit_blob(lines, "%s_TEXT_BLOB" % prefix, strings)

    lines.append("#endif  // %s" % guard)
    lines.append("")

    stats = {
        "codes": len(table),
        "rows": len(rows),
        "strings": len(strings),
        "blob": pos,
        "flash": pos + len(rows) * (key_bits // 8 + 2 + (1 if index else 0))
                 + len(strings) * 4 + len(masks.ids) * 4,
    }
    return "\n".join(lines), stats


# hms_known.h packs each code's severity and subcode into one uint16. The feed
# has never carried a severity above 3 or a subcode above 0x0086, so 2 + 14 bits
# is generous - but a feed refresh that broke either assumption would silently
# corrupt the set, so the generator asserts instead of masking.
KNOWN_SEV_MAX = 3
KNOWN_SUB_MAX = 0x3FFF


def build_known_header(ver, table, index):
    """Render include/error_tables/hms_known.h. Returns (text, stats).

    Grouped by `attr`, and the groups' code runs are deduped: AMS A-D and AMS-HT
    A-H repeat the same list per slot, so ~630 attrs share ~150 runs. Severity 1
    is left out - FATAL is never suppressed, so it is never looked up here.
    """
    masks = MaskPool()
    groups = {}
    for code, texts in table.items():
        key = int(code, 16)
        attr, low = key >> 32, key & 0xFFFFFFFF
        sev, sub = low >> 16, low & 0xFFFF
        if sev == 1:
            continue
        if sev > KNOWN_SEV_MAX or sub > KNOWN_SUB_MAX:
            raise SystemExit(
                "hms_known: %s does not fit the 2+14-bit packing (sev=%d, sub=0x%04X). "
                "Widen the packing in tools/gen_error_tables.py and in "
                "hmsIsDescribed() together." % (code, sev, sub))
        groups.setdefault(attr, []).append(
            ((sev << 14) | sub, masks.id(mask_of(texts, index))))

    attrs = sorted(groups)
    run_ids, run_of = {}, []
    for attr in attrs:
        run = tuple(sorted(groups[attr]))
        run_of.append(run_ids.setdefault(run, len(run_ids)))
    if len(run_ids) > 0x100:
        raise SystemExit("hms_known: more than 256 code runs - widen the uint8 run ids")
    idx, packed, packed_mask = [], [], []
    for run in run_ids:
        idx.append(len(packed))
        packed.extend(c for c, _ in run)
        packed_mask.extend(m for _, m in run)
    idx.append(len(packed))          # one past the end, so every run is idx[i]..idx[i+1]
    total = sum(len(g) for g in groups.values())

    lines = [
        "// HMS known-code set - GENERATED, DO NOT EDIT.",
        "// Regenerate with: python tools/gen_error_tables.py",
        "//",
        "// Source: %s&d=<model>, every model" % FEED_URL,
        "// Feed ver: %s" % ver,
        "// %u codes across %u attrs sharing %u runs - the hms_table.h keys without"
        % (total, len(attrs), len(run_ids)),
        "// severity 1 and without text. Compiled only where hms_table.h is not.",
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
        "#define HMS_KNOWN_RUN_COUNT   %u" % len(run_ids),
        "#define HMS_KNOWN_CODE_COUNT  %u" % len(packed),
        "#define HMS_KNOWN_SEV_SHIFT   14",
        "#define HMS_KNOWN_SUB_MASK    0x%04X" % KNOWN_SUB_MAX,
        "",
    ]

    emit_array(lines, "static const uint32_t HMS_KNOWN_ATTR[HMS_KNOWN_ATTR_COUNT] PROGMEM",
               attrs, 8, "0x%08XUL,")
    emit_array(lines, "static const uint8_t HMS_KNOWN_RUN[HMS_KNOWN_ATTR_COUNT] PROGMEM",
               run_of, 20, "%u,")
    emit_array(lines, "static const uint16_t HMS_KNOWN_IDX[HMS_KNOWN_RUN_COUNT + 1] PROGMEM",
               idx, 12, "%u,")
    emit_array(lines, "static const uint16_t HMS_KNOWN_CODE[HMS_KNOWN_CODE_COUNT] PROGMEM",
               packed, 12, "0x%04X,")
    emit_array(lines, "static const uint8_t HMS_KNOWN_MASK_ID[HMS_KNOWN_CODE_COUNT] PROGMEM",
               packed_mask, 20, "%u,")
    masks.emit(lines, "HMS_KNOWN")

    lines.append("#endif  // HMS_KNOWN_H")
    lines.append("")

    stats = {
        "attrs": len(attrs),
        "runs": len(run_ids),
        "codes": total,
        "flash": len(attrs) * 5 + (len(run_ids) + 1) * 2 + len(packed) * 3
                 + len(masks.ids) * 4,
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


def fetch_wiki_index():
    """code (16 hex, no separators, upper) -> page path relative to /en/.

    A wiki outage must never sink a table refresh: any failure returns {} and the
    caller keeps the links already in the mirror.
    """
    models = [sn for sn, _ in MODELS]
    found = {}                          # sn -> {code: path}
    try:
        for dev_id in range(1, WIKI_MAX_ID + 1):
            page, seen = 1, 0
            while True:
                req = urllib.request.Request(
                    "%s&id=%d&page=%d" % (WIKI_API_URL, dev_id, page),
                    headers={"User-Agent": WIKI_UA})
                with urllib.request.urlopen(req, timeout=60) as resp:
                    data = json.loads(resp.read().decode("utf-8"))["data"]
                sn, records = data.get("devicecode"), data.get("records") or []
                if data.get("deviceid") != dev_id:
                    break                # unknown id: the API answers for id 1
                for rec in records:
                    m = WIKI_PAGE_RE.match(rec.get("wiki_en") or "")
                    if m:
                        found.setdefault(sn, {}).setdefault(rec["ecode"].upper(), m.group(1))
                seen += len(records)
                if not records or seen >= int(data.get("total") or 0):
                    break
                page += 1
            if data.get("deviceid") != dev_id:
                break
    except Exception as exc:                                # noqa: BLE001
        print("WARNING: wiki list unreachable (%s)" % exc, file=sys.stderr)
        return {}
    index = {}
    for sn in models:                   # MODELS order decides between models
        for code, path in found.get(sn, {}).items():
            index.setdefault(code, path)
    if not index:
        print("WARNING: wiki list parsed to 0 codes - API changed?", file=sys.stderr)
    return index


def write_mirror(path, ver, hms, err, wiki):
    """hms / err: {code: {sn: raw text}}."""
    doc = {"ver": ver, "alt": {}}
    for key, table in (("hms", hms), ("err", err)):
        doc[key], doc["alt"][key] = {}, {}
        for code, texts in table.items():
            v = variants(texts)
            doc[key][code] = v[0][0]
            if len(v) > 1:
                doc["alt"][key][code] = {t: ",".join(sns) for t, sns in v[1:]}
    # Only codes we actually ship text for - the index also lists codes the
    # feeds have never carried, and they would just bloat the download.
    doc["wiki"] = {code: page for code, page in wiki.items() if code in doc["hms"]}
    os.makedirs(os.path.dirname(path), exist_ok=True)
    # Sorted keys and no spaces: stable diffs, smallest download.
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(doc, fh, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return doc, os.path.getsize(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--input", metavar="DIR",
                    help="read <sn>.json feeds from a directory instead of fetching")
    ap.add_argument("--save", metavar="DIR", help="also write the fetched feeds there")
    ap.add_argument("--out-dir", default=None,
                    help="output directory (default: <repo>/include/error_tables)")
    ap.add_argument("--dry-run", action="store_true", help="print stats, write nothing")
    args = ap.parse_args()

    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_dir = args.out_dir or os.path.join(repo, "include", "error_tables")
    mirror_path = os.path.join(repo, "docs", "errors", "hms_en.json")

    print("Loading feeds...")
    feeds = load_feeds(args.input, args.save)
    vers = sorted({feed_ver(f) for f in feeds.values()})
    if len(vers) > 1:
        print("NOTE: model feeds carry different vers: %s" % ", ".join(vers))
    ver = vers[-1]
    print("Feed ver %s" % ver)

    def per_model(domain, fn):
        return {sn: fn(f["data"][domain]["en"]) for sn, f in feeds.items()}

    hms_m = per_model("device_hms", lambda e: collect(e)[0])
    err_m = per_model("device_error", lambda e: collect(e)[0])
    hms, err = by_code(hms_m), by_code(err_m)
    raw_hms = by_code(per_model("device_hms", collect_raw))
    raw_err = by_code(per_model("device_error", collect_raw))
    index = model_classes(hms_m, err_m)

    # The embedded tables and the mirror use different blank filters (folded and
    # truncated vs raw). They must agree per model, or a 4 MB board and a 16 MB
    # board would judge the same code differently.
    for name, a, b in (("hms", hms, raw_hms), ("print_error", err, raw_err)):
        bad = [c for c in set(a) | set(b) if set(a.get(c, ())) != set(b.get(c, ()))]
        if bad:
            raise SystemExit("%s: embedded and mirror filters disagree on %d codes: %s"
                             % (name, len(bad), sorted(bad)[:10]))

    lite = {c: {DEFAULT_MODEL: t[DEFAULT_MODEL]} for c, t in err.items()
            if DEFAULT_MODEL in t}
    known_text, known = build_known_header(ver, hms, index)
    outputs = [("error_models.h", build_models_header(ver, index)),
               ("hms_known.h", known_text)]
    print("models       %u prefixes, %u distinct feeds"
          % (len(MODELS), len(set(index.values()))))
    print("hms_known    %4u codes, %u attrs, %u runs, ~%.1f KB flash"
          % (known["codes"], known["attrs"], known["runs"], known["flash"] / 1024.0))
    for domain, table, key_bits, filename, idx in (
            ("hms", hms, 64, "hms_table.h", index),
            ("print_error", err, 32, "print_error_table.h", index),
            ("print_error", lite, 32, "print_error_lite.h", None)):
        text, stats = build_header(domain, ver, table, key_bits, idx)
        outputs.append((filename, text))
        print("%-22s %4u codes, %4u rows, %u strings, blob %u B, ~%.1f KB flash"
              % (filename, stats["codes"], stats["rows"], stats["strings"],
                 stats["blob"], stats["flash"] / 1024.0))
    if args.dry_run:
        return

    print("Loading wiki index...")
    wiki = fetch_wiki_index()
    if not wiki and os.path.isfile(mirror_path):
        with open(mirror_path, "r", encoding="utf-8") as fh:
            wiki = json.load(fh).get("wiki", {})
        print("WARNING: keeping the %u wiki links already in the mirror" % len(wiki),
              file=sys.stderr)
    doc, size = write_mirror(mirror_path, ver, raw_hms, raw_err, wiki)
    print("mirror       %u hms + %u print_error, %u + %u with variants, %u wiki "
          "links (%u indexed), %.1f KB"
          % (len(doc["hms"]), len(doc["err"]), len(doc["alt"]["hms"]),
             len(doc["alt"]["err"]), len(doc["wiki"]), len(wiki), size / 1024.0))
    os.makedirs(out_dir, exist_ok=True)
    for filename, text in outputs:
        with open(os.path.join(out_dir, filename), "w", encoding="ascii", newline="\n") as fh:
            fh.write(text)


if __name__ == "__main__":
    main()
