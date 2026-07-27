# BambuHelper Companion

Desktop helper for BambuHelper devices. Two jobs:

- **Set up a device** - sign into Bambu Cloud (or enter LAN details), pick a
  printer, and push the config straight to a BambuHelper on your network. No
  copying serials or tokens by hand.
- **Diagnose** - test the whole connection chain to a printer (TCP, TLS, MQTT
  auth) and capture a full status dump to send for support.

The interface is a web UI running in a native window. It reuses the stylesheet
from the web flasher, so the flasher page, the device's own config UI and this
app all look like one product.

## Running it

```
pip install -r requirements.txt
python bambu_companion.py
```

| Command | What you get |
| --- | --- |
| `python bambu_companion.py` | Native window (falls back to your browser) |
| `python bambu_companion.py --browser` | Skip the native window, use the browser |
| `python bambu_companion.py --cli` | The terminal wizard |

`python ../bambu_diag.py` still works and lands on the terminal wizard - older
instructions point at that path.

### Windows

`pywebview` uses the Edge WebView2 backend. It ships with Windows 11; on
Windows 10 either install Microsoft's Evergreen WebView2 runtime or just let it
fall back to the browser - the UI is identical either way.

### Linux / macOS

No exe is published. Run from source as above. `pywebview` needs GTK or Qt
bindings from your package manager (Debian/Ubuntu: `python3-gi` plus
`gir1.2-webkit2-4.1`); skip them and the browser fallback takes over.

## Building the Windows exe

```
python -m pip install -r requirements.txt
python -m pip install pyinstaller
build_exe.bat
```

Output is `dist/BambuHelper-CompanionTool.exe`, also copied to
`tools/BambuHelper-CompanionTool.exe` - the path `DIAGNOSTICS-HOWTO.md` links
to. The exe is built `--windowed`; `--cli` allocates its own console at
runtime, so one binary covers both frontends.

## Layout

| File | Purpose |
| --- | --- |
| `bambu_companion.py` | Entry point. Picks GUI or CLI, handles the frozen-console case. |
| `bambu_core.py` | All the logic, free of console I/O. Cloud login, device discovery, config push, diagnostic. |
| `cli.py` | Terminal frontend. |
| `server.py` | Local HTTP server: static UI plus the JSON API over `bambu_core`. |
| `app_state.py` | Thread-safe state and the job/log plumbing the UI polls. |
| `app_window.py` | Native window via pywebview, with the browser fallback. |
| `webui/` | The interface. `styles.css` is a verbatim copy of `docs/styles.css`. |

`webui/styles.css` is copied, not edited - re-copy it from `docs/` whenever the
flasher's styling changes. Companion-only styling lives in `webui/app.css`.

## How the pieces talk

Both frontends drive `bambu_core`, so they cannot drift apart in behaviour.
The GUI adds a local HTTP server because the work does not fit in a single
request: the diagnostic runs for 30 seconds, the network sweep for about ten,
and cloud login pauses in the middle for a 2FA code. Those run on worker
threads (`app_state.Job`) and the UI polls for new log lines using a cursor.

### Security

The listener binds `127.0.0.1` only. On top of that, every `/api/` call needs
a `X-Companion-Token` header holding a random per-run value that is injected
into `index.html` at serve time, and every request must carry a `Host` header
naming localhost. Together those stop a page you have open elsewhere in the
browser from driving this API through DNS rebinding.

Your Bambu password is used for the login request and then dropped - it is
never written to disk and is cleared from the page once sign-in succeeds. The
access token stays in memory so it can be pushed to the device, and goes away
when you quit.

## Notes for future edits

- **paho-mqtt is pinned below 3.x on purpose.** The diagnostic reads raw
  v3.1.1 CONNACK codes (4 = bad credentials, 5 = not authorized) to tell the
  user whether their Access Code or cloud token is wrong. That needs paho's v1
  callback API - under the v2 API paho rewrites those into MQTT5-style reason
  codes and the check silently stops matching. See `_make_mqtt_client`.
- **Keep `curl_cffi` current.** Cloudflare fronts Bambu's login servers and
  retires browser fingerprints over time, so an old build eventually starts
  collecting 403s. `IMPERSONATE_TARGETS` is walked in order on a 403.
- **All cloud calls share one session.** Cloudflare hands out clearance
  cookies on the first response and expects them back; a fresh request per call
  is what used to break the 2FA step specifically.
