"""Run with python -m unittest discover -s tests (no build dependencies)."""
import json
import re
import struct
import sys
import unittest
from html.parser import HTMLParser
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from localize_web import javascript, markup, pages


class Structure(HTMLParser):
    def __init__(self, source):
        super().__init__()
        self.tags = []
        self.feed(source)

    def handle_starttag(self, tag, attrs):
        self.tags.append((tag, [(k, v) for k, v in attrs if k not in ("lang", "title", "placeholder", "aria-label")]))


class LocalizationTests(unittest.TestCase):
    def test_markup_keeps_form_contract_and_escapes_attributes(self):
        source = '<label for="token">Access token</label><input id="token" name="token" value="Printer" placeholder="Password">'
        localized = markup(source)
        self.assertIn('>访问令牌<', localized)
        self.assertIn('placeholder="密码"', localized)
        self.assertEqual(Structure(source).tags, Structure(localized).tags)

    def test_javascript_keeps_protocol_tokens_and_translates_messages(self):
        source = "// 'Printer Settings'\nd.append('token', token); if (d.state === 'failed') showToast('Save failed'); var label = ' seconds';"
        localized = javascript(source)
        self.assertIn("d.append('token', token)", localized)
        self.assertIn("d.state === 'failed'", localized)
        self.assertIn('保存失败', localized)
        self.assertIn('// \'Printer Settings\'', localized)
        self.assertIn(' 秒', localized)

    def test_pages_preserve_inputs_routes_handlers_and_placeholders(self):
        source = (ROOT / "include" / "web_pages.h").read_text(encoding="utf-8")
        localized = pages(source)
        self.assertEqual(re.findall(r"%[A-Z0-9_]+%", source), re.findall(r"%[A-Z0-9_]+%", localized))
        self.assertEqual(Structure(source).tags, Structure(localized.replace('<p class="banner banner-warn">在线更新安装上游英文固件；保留中文请手动上传中文固件。</p>', '')).tags)
        self.assertIn('lang="zh-CN"', localized)
        self.assertIn('首次设置', localized)
        self.assertIn('保存打印机设置', localized)
        self.assertNotIn('>Printer Settings<', localized)

    def test_localized_json_keeps_machine_fields(self):
        pattern = r'BH_TEXT\(("(?:\\.|[^"\\])*"),\s*("(?:\\.|[^"\\])*")\)'
        for path in [ROOT / "src" / "web_server.cpp", ROOT / "src" / "web_template.cpp"]:
            for first, second in re.findall(pattern, path.read_text(encoding="utf-8")):
                en, zh = json.loads(first), json.loads(second)
                self.assertFalse(re.fullmatch(r"[a-z0-9_-]+", en), (path.name, en))
                if en.startswith('{'):
                    a, b = json.loads(en), json.loads(zh)
                    self.assertEqual(a.keys(), b.keys())
                    for key in a.keys() - {"message", "error", "msg"}:
                        self.assertEqual(a[key], b[key], (path.name, key))

    def test_printer_state_protocol_is_not_localized(self):
        source = (ROOT / "include" / "bambu_state.h").read_text(encoding="utf-8")
        parse = source.split("inline PrinterGcodeState parsePrinterGcodeState(", 1)[1].split("\n}", 1)[0]
        canonical = source.split("inline void setPrinterGcodeStateCanonical(", 1)[1].split("\n}", 1)[0]
        for token in ("IDLE", "RUNNING", "PAUSE", "PREPARE", "FINISH", "FAILED", "UNKNOWN"):
            self.assertIn('"' + token + '"', parse)
            self.assertIn('"' + token + '"', canonical)
        self.assertNotIn("BH_TEXT", parse)
        self.assertNotIn("BH_TEXT", canonical)

    def test_screen_translations_have_complete_sorted_font_subsets(self):
        translated = set()
        paths = list((ROOT / "src").glob("display*.cpp")) + list((ROOT / "src").glob("clock*.cpp")) + [ROOT / "include" / "bambu_state.h", ROOT / "src" / "hms_lookup.h", ROOT / "src" / "hms_lookup.cpp", ROOT / "src" / "settings.cpp"]
        for path in paths:
            for s in re.findall(r'BH_TEXT\("(?:\\.|[^"\\])*",\s*("(?:\\.|[^"\\])*")\)', path.read_text(encoding="utf-8")):
                translated.update(ord(c) for c in json.loads(s) if ord(c) > 127)
        for path in (ROOT / "include" / "fonts").glob("zh_*.h"):
            blob = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", path.read_text(encoding="utf-8")))
            count = struct.unpack_from(">I", blob)[0]
            codes = [struct.unpack_from(">I", blob, 24 + i * 28)[0] for i in range(count)]
            self.assertEqual(codes, sorted(set(codes)), path.name)
            self.assertTrue(translated <= set(codes), (path.name, translated - set(codes)))
            bitmap_size = sum(struct.unpack_from(">I", blob, 28 + i * 28)[0] * struct.unpack_from(">I", blob, 32 + i * 28)[0] for i in range(count))
            self.assertEqual(len(blob), 24 + count * 28 + bitmap_size)


if __name__ == "__main__":
    unittest.main()
