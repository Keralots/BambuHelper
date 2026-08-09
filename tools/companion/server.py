"""Local HTTP server backing the BambuHelper Companion GUI.

Binds 127.0.0.1 only and serves two things: the static web UI, and a small JSON
API that wraps bambu_core. Long jobs (the subnet sweep, the 30-second
diagnostic) run on worker threads via app_state.Job; the UI polls for their log
output.

Security notes - this page handles a Bambu Lab password:
  * The listener is on the loopback interface, so nothing off-machine can
    reach it.
  * Every request must carry a Host header naming localhost, which blocks
    DNS-rebinding (a hostile domain resolving to 127.0.0.1 to reach this API
    from a page the user has open elsewhere).
  * Every /api/ call must carry an X-Companion-Token header holding a random
    per-run value. It is substituted into index.html at serve time, so only a
    page served by this process knows it, and a custom header forces a CORS
    preflight that a cross-origin page cannot satisfy.
  * The password is used for the login request and never stored - see
    app_state.AppState.
"""

import json
import mimetypes
import os
import secrets
import socket
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

import bambu_core as core

APP_VERSION = "1.0"

_CTYPES = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".svg": "image/svg+xml",
}

# Random per-run value; see the module docstring.
SESSION_TOKEN = secrets.token_urlsafe(24)


def webui_dir():
    """Bundled assets, in both script and PyInstaller onefile modes."""
    base = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(base, "webui")


def _host_is_local(host_header):
    if not host_header:
        return False
    host = host_header.rsplit(":", 1)[0].strip("[]").lower()
    return host in ("127.0.0.1", "localhost", "::1")


class Handler(BaseHTTPRequestHandler):
    server_version = "BambuHelperCompanion/" + APP_VERSION
    protocol_version = "HTTP/1.1"

    # The default handler logs every request to stderr. In a --windowed exe
    # stderr may not exist, and the noise is useless either way.
    def log_message(self, fmt, *args):
        pass

    # ── plumbing ───────────────────────────────────────────────────────────
    @property
    def state(self):
        return self.server.state

    @property
    def bridge(self):
        """Native-window callbacks (file dialogs), or None in browser mode."""
        return getattr(self.server, "bridge", None)

    def _send(self, code, body, ctype="application/json; charset=utf-8"):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        # Nothing here should ever be cached - the UI polls these endpoints.
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _json(self, code, obj):
        self._send(code, json.dumps(obj))

    def _fail(self, code, message):
        self._json(code, {"error": message})

    def _read_json(self):
        try:
            length = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            return {}
        if length <= 0 or length > 1_000_000:
            return {}
        try:
            return json.loads(self.rfile.read(length).decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            return {}

    def _guard(self, api):
        """Host + token checks. Returns False once a response has been sent."""
        if not _host_is_local(self.headers.get("Host")):
            self._fail(403, "Refused: this server only answers on localhost.")
            return False
        if api and self.headers.get("X-Companion-Token") != SESSION_TOKEN:
            self._fail(403, "Refused: bad or missing session token.")
            return False
        return True

    # ── routing ────────────────────────────────────────────────────────────
    def do_GET(self):
        path = urlparse(self.path).path
        query = parse_qs(urlparse(self.path).query)
        is_api = path.startswith("/api/")
        if not self._guard(is_api):
            return
        try:
            if is_api:
                self.route_get(path, query)
            else:
                self.serve_static(path)
        except Exception as e:
            self._fail(500, str(e))

    def do_POST(self):
        path = urlparse(self.path).path
        if not self._guard(path.startswith("/api/")):
            return
        try:
            self.route_post(path, self._read_json())
        except core.CurlCffiMissing as e:
            self._fail(503, str(e))
        except core.MqttMissing as e:
            self._fail(503, str(e))
        except Exception as e:
            self._fail(500, str(e))

    # ── static ─────────────────────────────────────────────────────────────
    def serve_static(self, path):
        name = "index.html" if path in ("/", "") else path.lstrip("/")
        # Refuse anything that escapes webui/ - no traversal, no absolute paths.
        root = os.path.realpath(webui_dir())
        target = os.path.realpath(os.path.join(root, name))
        if not target.startswith(root + os.sep) or not os.path.isfile(target):
            self._send(404, "Not found", "text/plain; charset=utf-8")
            return

        ext = os.path.splitext(target)[1].lower()
        ctype = _CTYPES.get(ext) or mimetypes.guess_type(target)[0] or "application/octet-stream"
        with open(target, "rb") as f:
            body = f.read()
        if ext == ".html":
            body = body.replace(b"%SESSION_TOKEN%", SESSION_TOKEN.encode())
            body = body.replace(b"%APP_VERSION%", APP_VERSION.encode())
            body = body.replace(b"%NATIVE%", b"1" if self.bridge else b"0")
        self._send(200, body, ctype)

    # ── GET api ────────────────────────────────────────────────────────────
    def route_get(self, path, query):
        if path == "/api/hello":
            self._json(200, {
                "version": APP_VERSION,
                "mqtt": core.MQTT_AVAILABLE,
                "native": bool(self.bridge),
                "logged_in": self.state.logged_in(),
            })
            return

        if path == "/api/scan":
            cursor = int((query.get("since") or ["0"])[0])
            snap = self.state.scan.snapshot(cursor)
            snap["found"] = self.state.get_found()
            self._json(200, snap)
            return

        if path == "/api/diag":
            cursor = int((query.get("since") or ["0"])[0])
            snap = self.state.diag.snapshot(cursor)
            diag = getattr(self.server, "diagnostic", None)
            if diag is not None and not snap["running"]:
                level, text = diag.verdict()
                snap["summary"] = [{"ok": bool(ok), "label": label}
                                   for ok, label in diag.summary_rows()]
                snap["verdict"] = {"level": level, "text": text}
                snap["has_dump"] = diag.pushall is not None
            self._json(200, snap)
            return

        if path == "/api/diag/dump":
            what = (query.get("what") or ["pushall"])[0]
            text = self._dump_text(what)
            if text is None:
                self._fail(404, "No dump captured yet.")
                return
            self._send(200, text, "application/json; charset=utf-8")
            return

        self._fail(404, "Unknown endpoint.")

    def _dump_text(self, what):
        diag = getattr(self.server, "diagnostic", None)
        if diag is None:
            return None
        if what == "ams":
            data = diag.ams_extract
        elif what == "log":
            return self.state.diag.log.as_text()
        else:
            data = diag.pushall
        if data is None:
            return None
        return json.dumps(data, indent=2, ensure_ascii=False)

    # ── POST api ───────────────────────────────────────────────────────────
    def route_post(self, path, body):
        handler = {
            "/api/login": self.api_login,
            "/api/verify": self.api_verify,
            "/api/logout": self.api_logout,
            "/api/select": self.api_select,
            "/api/lan": self.api_lan,
            "/api/scan/start": self.api_scan_start,
            "/api/push": self.api_push,
            "/api/diag/start": self.api_diag_start,
            "/api/diag/cancel": self.api_diag_cancel,
            "/api/save": self.api_save,
            "/api/open": self.api_open,
            "/api/quit": self.api_quit,
        }.get(path)
        if handler is None:
            self._fail(404, "Unknown endpoint.")
            return
        handler(body)

    # ── cloud login flow ───────────────────────────────────────────────────
    def api_login(self, body):
        email = (body.get("email") or "").strip()
        password = body.get("password") or ""
        region = "cn" if body.get("region") == "cn" else "us"
        if not email or not password:
            self._fail(400, "Email and password are both required.")
            return

        # A retry after a failed attempt must not inherit the old cookie jar.
        core.reset_cloud_session()
        self.state.begin_login(email, region)

        data = core.cloud_login(email, password, region)
        token = core.cloud_extract_token(data)
        if token:
            self._finish_login(token, region)
            return

        login_type = data.get("loginType", "")
        tfa_key = data.get("tfaKey") or ""
        # A TOTP account gets the challenge as a tfaKey with an EMPTY loginType,
        # so the key itself is what decides here.
        if login_type == "tfa" or (not login_type and tfa_key):
            self.state.set_tfa_key(tfa_key)
            self._json(200, {"state": "tfa"})
        elif login_type == "verifyCode":
            self._json(200, {"state": "verifyCode"})
        else:
            self._fail(400, f"Unexpected login response (loginType={login_type or 'none'}). "
                            f"{json.dumps(data)[:200]}")

    def api_verify(self, body):
        code = (body.get("code") or "").strip()
        if not code:
            self._fail(400, "Enter the verification code.")
            return
        ctx = self.state.get_login_context()
        region = ctx["region"]

        if ctx["tfa_key"]:
            token = core.cloud_verify_totp(code, ctx["tfa_key"])
        else:
            data = core.cloud_verify_email(ctx["email"], code, region)
            token = core.cloud_extract_token(data)
        if not token:
            self._fail(400, "The code was not accepted and no access token came back.")
            return
        self._finish_login(token, region)

    def _finish_login(self, token, region):
        notes = []
        user_id = core.resolve_user_id(token, region,
                                       lambda t, lvl="info": notes.append(t))
        devices = core.cloud_fetch_devices(token, region)
        self.state.set_session(token, user_id, devices)
        self._json(200, {
            "state": "ok",
            "notes": notes,
            "devices": [{
                "serial": (d.get("dev_id") or "").upper(),
                "name": d.get("name") or "",
                "model": d.get("dev_product_name") or "",
                "online": bool(d.get("online")),
            } for d in devices if d.get("dev_id")],
        })

    def api_logout(self, body):
        self.state.forget_credentials()
        core.reset_cloud_session()
        self._json(200, {"ok": True})

    def api_select(self, body):
        serial = (body.get("serial") or "").strip().upper()
        token, user_id, region = self.state.get_session()
        if not token:
            self._fail(400, "Not signed in.")
            return
        match = next((d for d in self.state.get_devices()
                      if (d.get("dev_id") or "").upper() == serial), None)
        if match is None:
            self._fail(400, "That printer is not on the signed-in account.")
            return
        self.state.set_printer({
            "mode": "cloud",
            "broker": core.broker_for_region(region),
            "username": user_id,
            "password": token,
            "serial": serial,
            "region": region,
            "name": match.get("name") or "",
        })
        self._json(200, {"ok": True, "name": match.get("name") or "", "serial": serial})

    def api_lan(self, body):
        ip = (body.get("ip") or "").strip()
        code = (body.get("code") or "").strip()
        serial = (body.get("serial") or "").strip().upper()
        missing = [n for n, v in (("printer IP", ip), ("Access Code", code),
                                  ("serial number", serial)) if not v]
        if missing:
            self._fail(400, "Missing: " + ", ".join(missing))
            return
        self.state.set_printer({
            "mode": "lan",
            "broker": ip,
            "username": "bblp",
            "password": code,
            "serial": serial,
            "name": body.get("name") or "",
        })
        self._json(200, {"ok": True, "serial": serial})

    # ── device discovery + push ────────────────────────────────────────────
    def api_scan_start(self, body):
        state = self.state

        def work(job):
            found = core.discover_devices(job.emit, stop=job.stop)
            state.set_found(found)
            return {"count": len(found)}

        if not state.scan.start(work):
            self._fail(409, "A scan is already running.")
            return
        self._json(200, {"ok": True})

    def api_push(self, body):
        ip = (body.get("ip") or "").strip()
        if not ip:
            self._fail(400, "Enter the BambuHelper IP address.")
            return
        printer = self.state.get_printer()
        if not printer:
            self._fail(400, "Pick a printer first.")
            return
        try:
            slot = int(body.get("slot") or 0)
        except (TypeError, ValueError):
            slot = 0
        slot = 0 if slot not in (0, 1) else slot

        form = core.build_printer_form(slot, printer, (body.get("name") or "").strip())
        status, resp = core.push_config_to_device(ip, form)
        level, message = core.interpret_push_result(status, resp)
        self._json(200, {
            "level": level,
            "message": message,
            "http": status,
            "slot": slot + 1,
            "name": form["pname"],
            "serial": form["serial"],
            "mode": form["connmode"],
        })

    # ── diagnostic ─────────────────────────────────────────────────────────
    def api_diag_start(self, body):
        printer = self.state.get_printer()
        if not printer:
            self._fail(400, "Pick a printer first.")
            return
        if not core.MQTT_AVAILABLE:
            self._fail(503, "'paho-mqtt' is required for the diagnostic.\n"
                            "Install with: pip install paho-mqtt")
            return
        server = self.server

        def work(job):
            diag = core.Diagnostic(printer, log=job.emit, stop=job.stop)
            server.diagnostic = diag
            diag.run()
            return diag.result

        server.diagnostic = None
        if not self.state.diag.start(work):
            self._fail(409, "A diagnostic is already running.")
            return
        self._json(200, {"ok": True})

    def api_diag_cancel(self, body):
        self.state.diag.cancel()
        self._json(200, {"ok": True})

    # ── native bridge ──────────────────────────────────────────────────────
    def api_save(self, body):
        """Save a dump through the native file dialog.

        A Blob + <a download> is a no-op inside the embedded WebView2, so the
        native window routes Save through here. In browser mode there is no
        bridge and the UI downloads from /api/diag/dump instead.
        """
        bridge = self.bridge
        if bridge is None:
            self._fail(501, "No native window - use the download link instead.")
            return
        what = body.get("what") or "pushall"
        text = self._dump_text(what)
        if text is None:
            self._fail(404, "No dump captured yet.")
            return
        default = {"pushall": "pushall_dump.json",
                   "ams": "ams_dump.json",
                   "log": "diagnostic_log.txt"}.get(what, "dump.json")
        self._json(200, bridge.save_text(text, default))

    def api_open(self, body):
        bridge = self.bridge
        url = body.get("url") or ""
        if bridge is None:
            import webbrowser
            ok = webbrowser.open(url) if url.startswith(("http://", "https://")) else False
            self._json(200, {"ok": bool(ok)})
            return
        self._json(200, bridge.open_url(url))

    def api_quit(self, body):
        bridge = self.bridge
        self._json(200, {"ok": True})
        if bridge is not None:
            threading.Thread(target=bridge.quit, daemon=True).start()


class Server(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = False  # fail loudly rather than stealing a live port

    def __init__(self, addr, state, bridge=None):
        super().__init__(addr, Handler)
        self.state = state
        self.bridge = bridge
        self.diagnostic = None


def make_server(state, bridge=None, port=8747, tries=20):
    """Bind 127.0.0.1 on the first free port at or above `port`."""
    last = None
    for offset in range(tries):
        try:
            httpd = Server(("127.0.0.1", port + offset), state, bridge)
            return httpd, port + offset
        except OSError as e:
            last = e
    raise RuntimeError(f"Could not bind a local port in "
                       f"{port}-{port + tries - 1}: {last}")


def serve(httpd):
    threading.Thread(target=httpd.serve_forever, daemon=True,
                     name="http-server").start()
