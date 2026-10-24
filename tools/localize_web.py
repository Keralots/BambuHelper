"""Build-time localization of UI text; IDs, routes and form values stay intact."""
import ast
import html
import json
import re
from pathlib import Path


CATALOG = json.loads((Path(__file__).resolve().parents[1] / "locales" / "zh_CN.json").read_text(encoding="utf-8"))
JS_TOKEN = re.compile(r'''//[^\n]*|/\*[\s\S]*?\*/|'(?:\\.|[^'\\])*'|"(?:\\.|[^"\\])*"''')


def text(value):
    key = value.strip()
    translated = CATALOG.get(key, CATALOG.get(html.unescape(key)))
    return value if translated is None else value.replace(key, translated, 1)


def markup(source):
    source = re.sub(r">([^<>]+)<", lambda m: ">" + text(m[1]) + "<", source)
    return re.sub(r'''\b(placeholder|title|aria-label)=(['"])(.*?)\2''',
                  lambda m: m[1] + "=" + m[2] + html.escape(text(html.unescape(m[3])), quote=True) + m[2], source)


def javascript(source):
    def replace(match):
        token = match[0]
        if token.startswith("/"):
            return token
        value = ast.literal_eval(token)
        # Lowercase single tokens are form keys, API states, CSS classes or IDs.
        # Their visible occurrences inside markup are localized separately.
        translated = value if re.fullmatch(r"[a-z0-9_-]+", value) else text(value)
        if translated == value:
            translated = markup(value)
        return token if translated == value else json.dumps(translated, ensure_ascii=False)
    return JS_TOKEN.sub(replace, source)


def pages(source):
    def replace(match):
        body = markup(match[1]).replace('lang="en"', 'lang="zh-CN"')
        body = body.replace('<div id="fw-tab-auto">', '<div id="fw-tab-auto"><p class="banner banner-warn">在线更新安装上游英文固件；保留中文请手动上传中文固件。</p>')
        body = re.sub(r"(<script>)([\s\S]*?)(</script>)",
                      lambda m: m[1] + javascript(m[2]) + m[3], body)
        return 'R"rawliteral(' + body + ')rawliteral"'
    return re.sub(r'R"rawliteral\(([\s\S]*?)\)rawliteral"', replace, source)
