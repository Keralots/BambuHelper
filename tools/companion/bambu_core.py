#!/usr/bin/env python3
"""
BambuHelper Companion - core logic.

Everything here is free of console I/O: no print(), no input(). Functions
either return data or raise, and the long-running diagnostic reports progress
through a `log` callback. That is what lets the same code back both frontends -
the terminal wizard (cli.py) and the GUI (server.py + webui/).

Three groups:
  * Cloud       - Bambu account login (CSRF + Cloudflare), printer list, tokens
  * Device      - BambuHelper discovery on the LAN, pushing printer config
  * Diagnostic  - TCP / TLS / MQTT chain test with a pushall capture

Requirements:
    pip install paho-mqtt              # always required
    pip install -U curl_cffi           # only for Cloud mode (Cloudflare bypass)
"""

import base64
import concurrent.futures
import ipaddress
import json
import socket
import ssl
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

try:
    import paho.mqtt.client as mqtt
    MQTT_AVAILABLE = True
except ImportError:
    mqtt = None
    MQTT_AVAILABLE = False


# ───────────────────────────────────────────────────────────────────────────
#  Logging shim
# ───────────────────────────────────────────────────────────────────────────
# Callers pass a `log(text, level)` callable. Levels are advisory - the CLI
# renders them as [OK]/[FAIL] tags, the GUI colours them.
LEVELS = ("info", "ok", "warn", "fail", "step")


def _noop_log(text, level="info"):
    pass


class MqttMissing(RuntimeError):
    """paho-mqtt is not installed - only the diagnostic needs it."""


# ───────────────────────────────────────────────────────────────────────────
#  Cloud login (email + password + optional 2FA via Bambu's login API)
# ───────────────────────────────────────────────────────────────────────────
API_BASE_US = "https://api.bambulab.com"
API_BASE_CN = "https://api.bambulab.cn"
TFA_URL = "https://bambulab.com/api/sign-in/tfa"
WEB_ORIGIN = "https://bambulab.com"

# Cloudflare fronts the Bambu endpoints. curl_cffi's TLS/JA3 impersonation is
# what gets us past the fingerprint check, but that alone is not enough:
# Cloudflare hands out clearance cookies on the first response and expects them
# back on the next request, so every cloud call has to share ONE session. Doing
# a fresh request per call is what produced "Verification failed: HTTP Error 403"
# on the 2FA/verification step while the login call itself went through.
#
# Impersonation targets are tried in order. "chrome" is an alias for whatever
# Chrome build the installed curl_cffi ships with; the explicit ones are
# fallbacks for when Cloudflare starts rejecting that particular fingerprint.
# Unknown names (older curl_cffi) are skipped automatically.
IMPERSONATE_TARGETS = ["chrome", "chrome136", "chrome131", "safari18_0"]

# bambulab.com's double-submit CSRF (see _ensure_csrf)
CSRF_COOKIE = "bbl_csrf_token"
CSRF_HEADER = "x-bbl-csrf-token"
CSRF_URL = f"{WEB_ORIGIN}/api/csrf"

BROWSER_HEADERS = {
    "Accept": "application/json, text/plain, */*",
    "Accept-Language": "en-US,en;q=0.9",
    "Content-Type": "application/json",
    "Origin": WEB_ORIGIN,
    "Referer": f"{WEB_ORIGIN}/",
}

_session = None          # curl_cffi Session shared by every cloud call
_session_target = None   # impersonation target the live session was built with


class CurlCffiMissing(RuntimeError):
    """curl_cffi is required for Cloud mode and is not installed."""


def _require_curl_cffi():
    try:
        from curl_cffi import requests
        return requests
    except ImportError:
        raise CurlCffiMissing(
            "'curl_cffi' is required for Cloud mode (Cloudflare bypass).\n"
            "Install with: pip install curl_cffi")


def _build_session(target):
    """New session on a given impersonation target, or None if unsupported."""
    requests = _require_curl_cffi()
    try:
        session = requests.Session(impersonate=target)
    except Exception:
        return None  # target unknown to this curl_cffi build
    session.headers.update(BROWSER_HEADERS)
    return session


def _cloud_session():
    global _session, _session_target
    if _session is None:
        for target in IMPERSONATE_TARGETS:
            session = _build_session(target)
            if session is not None:
                _session, _session_target = session, target
                break
        if _session is None:
            raise CurlCffiMissing(
                "No usable browser impersonation target in curl_cffi.\n"
                "Upgrade it with: pip install -U curl_cffi")
    return _session


def reset_cloud_session():
    """Drop the shared session so the next call logs in cold.

    The GUI can start a second login attempt in the same process (wrong
    password, user goes back a step). Without this the stale cookie jar - and
    any half-completed 2FA state - would ride along into the retry.
    """
    global _session, _session_target
    _session, _session_target = None, None


def _next_impersonate_target():
    """Rebuild the session on the next target after a 403. False when exhausted."""
    global _session, _session_target
    try:
        start = IMPERSONATE_TARGETS.index(_session_target) + 1
    except ValueError:
        start = len(IMPERSONATE_TARGETS)
    for target in IMPERSONATE_TARGETS[start:]:
        session = _build_session(target)
        if session is not None:
            # New session = new cookie jar, so the CSRF token is refetched on
            # the next write.
            _session, _session_target = session, target
            return True
    return False


def _csrf_token():
    for cookie in _cloud_session().cookies.jar:
        if cookie.name == CSRF_COOKIE:
            return cookie.value
    return None


def _ensure_csrf(refresh=False):
    """Fetch the CSRF cookie bambulab.com requires on writes (added mid-2026).

    The site does double-submit CSRF: `GET /api/csrf` sets a `bbl_csrf_token`
    cookie and every POST has to echo that value back in an `x-bbl-csrf-token`
    header. Without it the 2FA endpoint answers 403 `CSRF error: missing_cookie`
    - which is what broke Cloud mode. Only bambulab.com does this; the
    api.bambulab.com endpoints do not.
    """
    if refresh or not _csrf_token():
        try:
            _cloud_session().get(CSRF_URL, timeout=15)
        except Exception:
            pass
    return _csrf_token()


def _body_snippet(resp):
    try:
        return resp.text[:300]
    except Exception:
        return ""


def _cloud_request(method, url, **kwargs):
    """Cloud HTTP call, handling both CSRF and a Cloudflare fingerprint block."""
    kwargs.setdefault("timeout", 15)
    needs_csrf = (url.startswith(WEB_ORIGIN)
                  and method.upper() not in ("GET", "HEAD", "OPTIONS"))
    csrf_retried = False

    while True:
        if needs_csrf:
            token = _ensure_csrf()
            if token:
                headers = dict(kwargs.get("headers") or {})
                headers[CSRF_HEADER] = token
                kwargs["headers"] = headers

        resp = _cloud_session().request(method, url, **kwargs)

        if resp.status_code != 403:
            if resp.status_code >= 400:
                # raise_for_status() alone gives "HTTP Error 400:" with no hint
                # of what the server objected to; the body usually says.
                raise RuntimeError(f"HTTP {resp.status_code} - {_body_snippet(resp)}")
            return resp

        body = _body_snippet(resp)

        # A CSRF rejection is the site's own guard, not Cloudflare - swapping
        # browser fingerprints would not help. Refetch the token and retry once.
        if "csrf" in body.lower() and needs_csrf and not csrf_retried:
            csrf_retried = True
            _ensure_csrf(refresh=True)
            continue

        if not _next_impersonate_target():
            raise RuntimeError(
                "HTTP 403 - the request was refused on every browser "
                "fingerprint this curl_cffi build offers.\n"
                "  Try:  pip install -U curl_cffi\n"
                "  If it still fails, use LAN mode, or grab the token from the "
                "Bambu Handy app / bambulab.com and configure the device by hand.\n"
                f"  Response: {body}")


def api_base(region):
    return API_BASE_CN if region == "cn" else API_BASE_US


def broker_for_region(region):
    return "cn.mqtt.bambulab.com" if region == "cn" else "us.mqtt.bambulab.com"


def cloud_login(email, password, region):
    url = f"{api_base(region)}/v1/user-service/user/login"
    resp = _cloud_request("POST", url, json={"account": email, "password": password})
    return resp.json()


def cloud_verify_totp(tfa_code, tfa_key):
    # TFA_URL is on bambulab.com, so _cloud_request adds the CSRF header here.
    resp = _cloud_request("POST", TFA_URL,
                          json={"tfaKey": tfa_key, "tfaCode": tfa_code})
    token = resp.cookies.get("token")
    if not token:
        # The token cookie is set on the session, not necessarily echoed on this
        # response, depending on how the redirect chain resolves.
        for cookie in _cloud_session().cookies.jar:
            if cookie.name == "token":
                token = cookie.value
                break
    if not token:
        try:
            data = resp.json()
            token = data.get("accessToken") or data.get("token")
        except Exception:
            pass
    return token


def cloud_verify_email(email, code, region):
    url = f"{api_base(region)}/v1/user-service/user/login"
    resp = _cloud_request("POST", url, json={"account": email, "code": code})
    return resp.json()


def cloud_fetch_devices(token, region):
    url = f"{api_base(region)}/v1/iot-service/api/user/bind"
    resp = _cloud_request("GET", url, headers={"Authorization": f"Bearer {token}"})
    data = resp.json()
    devices = data.get("data", [])
    if not devices and isinstance(data.get("devices"), list):
        devices = data["devices"]
    return devices


def cloud_extract_token(data):
    token = data.get("accessToken")
    if not token and isinstance(data.get("data"), dict):
        token = data["data"].get("accessToken")
    return token if token else None


def extract_user_id_jwt(token):
    try:
        parts = token.split(".")
        if len(parts) < 2:
            return None
        payload_b64 = parts[1].replace("-", "+").replace("_", "/")
        payload_b64 += "=" * ((4 - len(payload_b64) % 4) % 4)
        decoded = base64.b64decode(payload_b64)
        data = json.loads(decoded)
        uid = data.get("uid") or data.get("sub") or data.get("user_id")
        if uid:
            return f"u_{uid}"
    except Exception:
        pass
    return None


def fetch_user_id_api(token, region, log=_noop_log):
    # Same Cloudflare-fronted host as the login calls, so this goes through the
    # shared impersonated session too - plain urllib gets a 403 here.
    url = f"{api_base(region)}/v1/user-service/my/profile"
    try:
        resp = _cloud_request("GET", url,
                              headers={"Authorization": f"Bearer {token}"})
        data = resp.json()
    except Exception as e:
        log(f"Profile API request failed: {e}", "warn")
        return None

    def pick(obj):
        if not isinstance(obj, dict):
            return None
        return (obj.get("uidStr") or obj.get("uid")
                or obj.get("userId") or obj.get("user_id"))

    uid = pick(data) or pick(data.get("data") if isinstance(data.get("data"), dict) else None)
    if uid is None:
        log(f"No uid in profile response: {json.dumps(data)[:300]}", "warn")
        return None
    return f"u_{uid}"


def resolve_user_id(token, region, log=_noop_log):
    """JWT first, profile API as the fallback. Raises when neither works."""
    user_id = extract_user_id_jwt(token)
    if not user_id:
        log("JWT decode failed - falling back to profile API...", "warn")
        user_id = fetch_user_id_api(token, region, log)
    if not user_id:
        raise RuntimeError("Could not determine the account userId.")
    return user_id


def device_label(dev):
    """Human label for a printer from the cloud bind list."""
    name = dev.get("name") or "?"
    model = dev.get("dev_product_name") or "?"
    serial = dev.get("dev_id") or "?"
    return f"{name} ({model}) - {serial}"


# ───────────────────────────────────────────────────────────────────────────
#  BambuHelper device: discovery + config push
# ───────────────────────────────────────────────────────────────────────────
# GET /status on the firmware answers with this key set. Matching several of
# them (rather than just "it replied with JSON") keeps a random printer, NAS or
# router web UI on the subnet from being listed as a BambuHelper.
_STATUS_MARKERS = ("heap_kb", "flash_kb", "uptime", "mac", "configured")

# Bypass any system proxy: a corporate PAC file would otherwise send the LAN
# sweep and the config POST out through a proxy that cannot reach 192.168.x.x.
_DIRECT_HTTP = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def local_ipv4s():
    """Best-effort list of this machine's LAN IPv4 addresses."""
    ips = set()
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            # No packet is sent - this just asks the routing table which
            # interface would be used, which is the one the device is on.
            s.connect(("8.8.8.8", 80))
            ips.add(s.getsockname()[0])
        finally:
            s.close()
    except OSError:
        pass
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ips.add(info[4][0])
    except OSError:
        pass
    return sorted(ip for ip in ips
                  if not ip.startswith("127.") and not ip.startswith("169.254."))


def probe_device(ip, timeout=1.2):
    """GET /status and decide whether `ip` is a BambuHelper. None if it isn't."""
    try:
        req = urllib.request.Request(f"http://{ip}/status", method="GET")
        with _DIRECT_HTTP.open(req, timeout=timeout) as resp:
            if resp.status != 200:
                return None
            raw = resp.read(4096).decode("utf-8", errors="replace")
        data = json.loads(raw)
    except Exception:
        return None
    if not isinstance(data, dict):
        return None
    if sum(1 for k in _STATUS_MARKERS if k in data) < len(_STATUS_MARKERS) - 1:
        return None
    return {
        "ip": ip,
        "name": (data.get("name") or "").strip(),
        "mac": data.get("mac", ""),
        "configured": bool(data.get("configured")),
        "connected": bool(data.get("connected")),
        "uptime": data.get("uptime", 0),
        "heap_kb": data.get("heap_kb", 0),
    }


def discover_devices(log=_noop_log, timeout=1.2, workers=64, stop=None):
    """Sweep the local /24(s) for BambuHelper devices answering on port 80."""
    hosts = []
    for ip in local_ipv4s():
        try:
            net = ipaddress.ip_network(f"{ip}/24", strict=False)
        except ValueError:
            continue
        for candidate in net.hosts():
            text = str(candidate)
            if text != ip and text not in hosts:
                hosts.append(text)

    if not hosts:
        log("No usable network interface found for the scan.", "warn")
        return []

    total = len(hosts)
    log(f"Scanning {total} addresses on your network...", "info")
    found = []
    done = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {pool.submit(probe_device, h, timeout): h for h in hosts}
        for fut in concurrent.futures.as_completed(futures):
            if stop is not None and stop.is_set():
                log("Scan cancelled.", "warn")
                break
            done += 1
            try:
                hit = fut.result()
            except Exception:
                hit = None
            if hit:
                found.append(hit)
                label = hit["name"] or "unnamed"
                log(f"Found BambuHelper at {hit['ip']} ({label})", "ok")
            # A sweep of a couple of /24s takes ~10s. Without a heartbeat the
            # UI sits on one line the whole time and reads as a hang.
            elif done % 64 == 0:
                log(f"Checked {done} of {total}...", "info")

    found.sort(key=lambda d: tuple(int(p) for p in d["ip"].split(".")))
    if not found:
        log("No BambuHelper found. Enter its IP by hand - it is shown on the "
            "device screen.", "warn")
    return found


def build_printer_form(slot, printer, friendly=""):
    """Form body for POST /save/printer.

    Field names must match the handler in src/web_server.cpp::handleSavePrinter
    (connmode/pname, region as "us"/"cn") - the web UI's savePrinter() JS is the
    source of truth for this shape.
    """
    name = friendly or printer.get("name") or ("Bambu " + printer["serial"][:4])
    if printer["mode"] == "lan":
        return {
            "slot": str(slot),
            "connmode": "lan",
            "pname": name,
            "serial": printer["serial"],
            "ip": printer["broker"],
            "code": printer["password"],
        }
    return {
        "slot": str(slot),
        "connmode": "cloud_all",
        "pname": name,
        "serial": printer["serial"],
        "token": printer["password"],
        "region": "cn" if printer.get("region") == "cn" else "us",
    }


def push_config_to_device(bh_ip, form):
    """POST form-urlencoded config to BambuHelper /save/printer.

    Returns (status, body). status is -1 when the device could not be reached.
    """
    body = urllib.parse.urlencode(form).encode("utf-8")
    req = urllib.request.Request(
        f"http://{bh_ip}/save/printer", data=body, method="POST",
        headers={"Content-Type": "application/x-www-form-urlencoded"},
    )
    try:
        with _DIRECT_HTTP.open(req, timeout=15) as resp:
            return resp.status, resp.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", errors="replace") if e.fp else ""
    except Exception as e:
        return -1, str(e)


def interpret_push_result(status, body):
    """Turn the device's reply into (level, message).

    level is "ok", "warn" or "fail". The device answers 200 with a `warning`
    field when it saved the config but flagged a missing/invalid value, so a
    bare status code is not enough to tell success from partial success.
    """
    if status == -1:
        return "fail", (f"Could not reach BambuHelper: {body}\n"
                        "Check the IP on the device screen, that you are on the "
                        "same WiFi, and that no firewall is blocking it.")
    try:
        parsed = json.loads(body)
    except (json.JSONDecodeError, TypeError):
        if status == 200:
            return "ok", "Config sent (the device did not return JSON to parse)."
        return "fail", f"HTTP {status} - the device did not return JSON."

    resp_status = parsed.get("status", "")
    warning = (parsed.get("warning") or "").strip()
    message = (parsed.get("message") or "").strip()

    if resp_status == "ok" and not warning:
        return "ok", ("Config sent. The device should start connecting to your "
                      "printer within a few seconds.")
    if resp_status == "ok" and warning:
        return "warn", (f"The device accepted the request but reported: {warning}\n"
                        "The config was still written - open the BambuHelper web "
                        "UI to see what landed and fix any gaps there.")
    return "fail", message or f"The device rejected the config (HTTP {status})."


# ───────────────────────────────────────────────────────────────────────────
#  Diagnostic (TCP / TLS / MQTT auth / pushall capture)
# ───────────────────────────────────────────────────────────────────────────
PORT = 8883

_RC_TEXT = {
    0: "Connected OK", 1: "Bad protocol version", 2: "Bad client ID",
    3: "Server unavailable", 4: "Bad credentials", 5: "Not authorized",
}

# A Bambu printer with every local MQTT slot taken does not refuse the client,
# it just stops servicing it. Confirmed on a P1S with three BambuHelper devices
# connected: the fourth client either times out on the TCP connect or gets the
# TCP session and then stalls in the TLS handshake. Both look like a dead
# printer from the outside, so every LAN-side failure path says this.
_SLOT_LIMIT_HINT = (
    "Bambu printers accept only about 3 local MQTT clients at once, counting "
    "every BambuHelper device, Panda Touch, Home Assistant and Bambu Studio "
    "session in LAN mode. Once the slots are full the printer stops answering "
    "on port 8883, which looks exactly like a printer that is switched off. "
    "Disconnect one client and try again, or use Cloud mode - cloud "
    "connections do not use the printer's local slots."
)

_SLOT_LIMIT_VERDICT = (
    "The printer's local MQTT slots are probably full. Bambu printers take "
    "only about 3 clients at once (BambuHelper devices, Panda Touch, Home "
    "Assistant, Bambu Studio in LAN mode) and a full printer accepts the TCP "
    "connection but never finishes the handshake. Disconnect one client, or "
    "switch to Cloud mode."
)


def _make_mqtt_client(client_id):
    """paho Client on the v1 callback API, on both paho 1.x and 2.x.

    paho 2.0 added a callback API version argument and defaults to v1 with a
    DeprecationWarning; asking for it explicitly silences that. Staying on v1
    is deliberate rather than lazy: under the v2 API paho rewrites a v3.1.1
    CONNACK code into an MQTT5-style ReasonCode (bad credentials becomes 0x86,
    not 4), which would break both the rc table above and the `rc in (4, 5)`
    check that tells the user their Access Code or cloud token is wrong.
    """
    if hasattr(mqtt, "CallbackAPIVersion"):
        return mqtt.Client(mqtt.CallbackAPIVersion.VERSION1,
                           client_id=client_id, protocol=mqtt.MQTTv311)
    return mqtt.Client(client_id=client_id, protocol=mqtt.MQTTv311)

_STATUS_KEYS = [
    "gcode_state", "mc_percent", "nozzle_temper", "bed_temper",
    "chamber_temper", "nozzle_target_temper", "bed_target_temper",
    "layer_num", "total_layer_num", "gcode_file",
    "subtask_name", "print_type", "wifi_signal", "nozzle_diameter",
]


class Diagnostic:
    """One diagnostic run against one printer.

    Progress goes to the `log` callback as it happens; the captured payloads
    stay on the instance so a caller can save them wherever it likes (the CLI
    writes them next to itself, the GUI offers a native Save dialog).
    """

    def __init__(self, cfg, log=_noop_log, stop=None, listen_secs=30):
        self.cfg = cfg
        self.log = log
        self.stop = stop or threading.Event()
        self.listen_secs = listen_secs
        self.pushall = None      # full parsed pushall payload
        self.ams_extract = None  # {"ams": ..., "vt_tray": ...}
        self._first_pushall_saved = False
        self.result = {
            "serial_ok": True,
            "tcp_ok": False,
            "tcp_ms": 0,
            "tls_ok": False,
            "tls_cipher": "",
            "tls_version": "",
            "mqtt_rc": -1,
            "subscribed": False,
            "pushall_sent": False,
            "messages_rx": 0,
            "first_pushall_keys": [],
            "pushall_bytes": 0,
            "delta_count": 0,
        }

    # ── steps ──────────────────────────────────────────────────────────────
    def check_serial(self):
        self.log("STEP 1: Serial number check", "step")
        serial = self.cfg["serial"]
        self.log(f"Serial: {serial} ({len(serial)} chars)")
        if serial != serial.upper():
            self.log(f"Serial has lowercase characters - should be {serial.upper()}. "
                     "Bambu MQTT topics are case-sensitive.", "warn")
            self.result["serial_ok"] = False
        else:
            self.log("All uppercase", "ok")
        if len(serial) < 10:
            self.log("Serial looks too short (expected around 15 characters)", "warn")
            self.result["serial_ok"] = False

    def check_tcp(self):
        self.log("STEP 2: TCP reachability", "step")
        broker = self.cfg["broker"]
        self.log(f"Testing {broker}:{PORT} ...")
        t0 = time.time()
        try:
            sock = socket.create_connection((broker, PORT), timeout=5)
            ms = (time.time() - t0) * 1000
            sock.close()
            self.result["tcp_ok"] = True
            self.result["tcp_ms"] = round(ms)
            self.log(f"TCP connected in {self.result['tcp_ms']}ms", "ok")
        except Exception as e:
            self.log(str(e), "fail")
            if self.cfg["mode"] == "cloud":
                self.log("Check your internet connection, DNS, and firewall.", "info")
            else:
                self.log("Check the printer is powered on, on the same network, "
                         "and not blocked by a firewall.", "info")
                self.log("If the printer is definitely online: " + _SLOT_LIMIT_HINT,
                         "info")

    def check_tls(self):
        self.log("STEP 3: TLS handshake", "step")
        broker = self.cfg["broker"]
        self.log(f"Testing TLS to {broker}:{PORT} ...")
        ctx = ssl.create_default_context()
        if self.cfg["mode"] == "lan":
            # The printer presents a self-signed certificate on the LAN.
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
        try:
            raw = socket.create_connection((broker, PORT), timeout=10)
            wrapped = ctx.wrap_socket(raw, server_hostname=broker)
            self.result["tls_ok"] = True
            self.result["tls_cipher"] = wrapped.cipher()[0] if wrapped.cipher() else "unknown"
            self.result["tls_version"] = wrapped.version() or "unknown"
            self.log(f"TLS version: {self.result['tls_version']}", "ok")
            self.log(f"Cipher: {self.result['tls_cipher']}", "ok")
            wrapped.close()
        except Exception as e:
            self.log(f"TLS handshake failed: {e}", "fail")
            if self.cfg["mode"] == "lan" and self.result["tcp_ok"]:
                self.log("The printer accepted the TCP connection but never "
                         "completed the handshake. " + _SLOT_LIMIT_HINT, "info")

    # ── MQTT callbacks ─────────────────────────────────────────────────────
    def _on_connect(self, client, userdata, flags, rc):
        self.result["mqtt_rc"] = rc
        self.log(f"CONNECT rc={rc} - {_RC_TEXT.get(rc, 'Unknown')}",
                 "ok" if rc == 0 else "fail")
        if rc != 0:
            if rc in (4, 5):
                if self.cfg["mode"] == "lan":
                    self.log("Check the Access Code - 8 characters from the "
                             "printer LCD (Settings > LAN Only Mode).", "info")
                else:
                    self.log("The cloud token may be expired or invalid.", "info")
            return

        topic_report = f"device/{self.cfg['serial']}/report"
        topic_request = f"device/{self.cfg['serial']}/request"
        client.subscribe(topic_report, qos=0)
        self.result["subscribed"] = True
        self.log(f"SUBSCRIBE {topic_report}", "ok")
        pushall = json.dumps({
            "pushing": {"sequence_id": "1", "command": "pushall",
                        "version": 1, "push_target": 1}
        })
        client.publish(topic_request, pushall, qos=0)
        self.result["pushall_sent"] = True
        self.log(f"PUSHALL sent to {topic_request}", "ok")

    def _dump_ams(self, p):
        ams_obj = p.get("ams")
        if not ams_obj:
            self.log("AMS: no 'ams' object in the payload", "info")
            return

        top_keys = [k for k in ams_obj.keys() if k != "ams"]
        if top_keys:
            self.log("AMS top-level fields:", "info")
            for k in sorted(top_keys):
                self.log(f"    {k}: {json.dumps(ams_obj[k])}")

        units = ams_obj.get("ams", [])
        if not units:
            self.log("AMS: no units array found", "info")
            return

        self.log(f"AMS: {len(units)} unit(s) detected", "ok")
        for unit in units:
            uid = unit.get("id", "?")
            self.log(f"  --- AMS unit {uid} ---")
            for k in sorted(unit.keys()):
                if k == "tray":
                    continue
                self.log(f"    {k}: {json.dumps(unit[k])}")
            drying_keys = [k for k in unit.keys()
                           if any(w in k.lower() for w in ("dry", "humid", "temp", "heat"))]
            if drying_keys:
                self.log(f"    drying-related: {', '.join(drying_keys)}", "info")
            for tray in unit.get("tray", []):
                tid = tray.get("id", "?")
                ttype = tray.get("tray_sub_brands") or tray.get("tray_type", "empty")
                self.log(f"    Tray {tid} ({ttype}):")
                for k in sorted(tray.keys()):
                    self.log(f"      {k}: {json.dumps(tray[k])}")

        vt = p.get("vt_tray")
        if vt:
            self.log("vt_tray (external spool):", "info")
            for k in sorted(vt.keys()):
                self.log(f"    {k}: {json.dumps(vt[k])}")

        self.ams_extract = {"ams": ams_obj}
        if vt:
            self.ams_extract["vt_tray"] = vt

    def _on_message(self, client, userdata, msg):
        self.result["messages_rx"] += 1
        size = len(msg.payload)
        payload = msg.payload.decode("utf-8", errors="replace")

        try:
            data = json.loads(payload)
        except json.JSONDecodeError:
            self.log(f"{size}B (not JSON): {payload[:200]}", "info")
            return

        p = data.get("print", {})
        keys = list(p.keys())

        if not self._first_pushall_saved and size > 1000:
            self._first_pushall_saved = True
            self.pushall = data
            self.result["pushall_bytes"] = size
            self.result["first_pushall_keys"] = keys
            self.log(f"PUSHALL RESPONSE: {size} bytes, {len(keys)} fields", "ok")
            self.log(f"Fields: {', '.join(sorted(keys))}")
            found = {k: p[k] for k in _STATUS_KEYS if k in p}
            if found:
                self.log(f"Status: {json.dumps(found, indent=2)}")
            self._dump_ams(p)
            return

        self.result["delta_count"] += 1
        if self.result["delta_count"] <= 3:
            self.log(f"DELTA {size}B fields=[{', '.join(keys[:5])}]")
            if "ams" in p:
                for unit in p["ams"].get("ams", []):
                    uid = unit.get("id", "?")
                    drying = {k: unit[k] for k in unit.keys()
                              if k != "tray" and any(w in k.lower()
                              for w in ("dry", "humid", "temp", "heat"))}
                    if drying:
                        self.log(f"AMS delta unit {uid} drying: {json.dumps(drying)}")
        elif self.result["delta_count"] == 4:
            self.log("... (suppressing further deltas)")

    def check_mqtt(self):
        if not MQTT_AVAILABLE:
            raise MqttMissing("'paho-mqtt' is required for the diagnostic.\n"
                              "Install with: pip install paho-mqtt")
        self.log("STEP 4: MQTT connect + data", "step")
        self.log(f"Mode:      {self.cfg['mode'].upper()}")
        self.log(f"Broker:    {self.cfg['broker']}:{PORT}")
        self.log(f"Username:  {self.cfg['username']}")
        self.log("Client ID: bambu_diag_test")
        self.log("Protocol:  MQTT v3.1.1")

        client = _make_mqtt_client("bambu_diag_test")
        client.username_pw_set(self.cfg["username"], self.cfg["password"])
        if self.cfg["mode"] == "cloud":
            client.tls_set()
        else:
            client.tls_set(cert_reqs=ssl.CERT_NONE)
            client.tls_insecure_set(True)
        client.on_connect = self._on_connect
        client.on_message = self._on_message

        try:
            client.connect(self.cfg["broker"], PORT, keepalive=60)
        except Exception as e:
            self.log(f"Connection error: {e}", "fail")
            if self.cfg["mode"] == "lan" and self.result["tcp_ok"]:
                self.log(_SLOT_LIMIT_HINT, "info")
            return

        self.log(f"Waiting for data ({self.listen_secs}s)...", "info")
        t0 = time.time()
        while time.time() - t0 < self.listen_secs and not self.stop.is_set():
            client.loop(timeout=1.0)
        client.disconnect()

    # ── summary ────────────────────────────────────────────────────────────
    def summary_rows(self):
        d = self.result
        return [
            (d["serial_ok"], f"Serial number format ({self.cfg['serial']})"),
            (d["tcp_ok"], f"TCP reachable ({d['tcp_ms']}ms)"),
            (d["tls_ok"], f"TLS handshake ({d['tls_version']} / {d['tls_cipher']})"),
            (d["mqtt_rc"] == 0, f"MQTT auth (rc={d['mqtt_rc']})"),
            (d["subscribed"], "Topic subscribed"),
            (d["pushall_sent"], "Pushall request sent"),
            (d["messages_rx"] > 0, f"Messages received ({d['messages_rx']} total)"),
            (d["pushall_bytes"] > 0,
             f"Pushall response ({d['pushall_bytes']} bytes, "
             f"{len(d['first_pushall_keys'])} fields)"),
        ]

    def verdict(self):
        """One paragraph explaining what the numbers mean. (level, text)."""
        d = self.result
        if d["messages_rx"] > 0:
            return "ok", ("The printer is responding normally. If BambuHelper still "
                          "shows UNKNOWN, the problem is in the device config rather "
                          "than the printer connection.")
        if d["mqtt_rc"] == 0:
            return "warn", ("MQTT connected but no messages arrived. Usually a serial "
                            "number mismatch (the topic never matches) or a printer "
                            "firmware issue.")
        if d["mqtt_rc"] in (4, 5):
            if self.cfg["mode"] == "cloud":
                return "fail", ("Authentication failed. The cloud token may be expired "
                                "- they last about three months. Log in again to get a "
                                "fresh one.")
            return "fail", ("Authentication failed. Re-check the Access Code on the "
                            "printer LCD (Settings > LAN Only Mode).")
        if not d["tcp_ok"]:
            if self.cfg["mode"] == "cloud":
                return "fail", "The broker is not reachable. Check internet and DNS."
            return "fail", ("The printer is not reachable. Check the IP, that you are "
                            "on the same subnet, and that the printer is powered on. "
                            "If it is online and other tools still talk to it, its "
                            "local MQTT slots are probably full - Bambu printers take "
                            "only about 3 clients at once, and a full printer stops "
                            "answering on port 8883. Disconnect one client, or switch "
                            "to Cloud mode.")
        # TCP came up but the session died before MQTT ever reported a return
        # code. On the LAN that is the slot limit far more often than anything
        # else - a printer that is off never gets this far.
        if self.cfg["mode"] == "lan" and d["mqtt_rc"] == -1:
            return "fail", _SLOT_LIMIT_VERDICT
        if not d["tls_ok"]:
            return "fail", ("The TLS handshake to the broker failed. Check the "
                            "system clock and that nothing is intercepting "
                            "port 8883.")
        return "warn", "The run did not get far enough to reach a conclusion."

    def run(self):
        self.log(f"Mode:   {self.cfg['mode'].upper()}", "info")
        self.log(f"Broker: {self.cfg['broker']}", "info")
        self.log(f"Serial: {self.cfg['serial']}", "info")
        self.check_serial()
        self.check_tcp()
        if not self.result["tcp_ok"]:
            return self.result
        self.check_tls()
        self.check_mqtt()
        return self.result
