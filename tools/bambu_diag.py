#!/usr/bin/env python3
"""
BambuHelper Companion Tool

Two modes in one interactive tool:
  1. Run diagnostic (test connection + dump status for support)
  2. Configure BambuHelper device (one-click cloud/LAN setup over LAN)

No file editing, no copy-pasting serials. Handles login + 2FA, fetches your
printer list automatically, and either runs the full diagnostic or pushes
the config straight to a BambuHelper device on your network.

Usage:
    python bambu_diag.py

Requirements:
    pip install paho-mqtt              # always required
    pip install curl_cffi              # only for Cloud mode (Cloudflare bypass)
"""

import atexit
import sys
import ssl
import socket
import time
import json
import base64
import getpass
import urllib.parse
import urllib.request

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("ERROR: 'paho-mqtt' package required.")
    print("Install with: pip install paho-mqtt")
    sys.exit(1)

# ────────────────────────────────────────────────────────────────────────────
#  Cloud login (email + password + optional 2FA via Bambu's login API)
# ────────────────────────────────────────────────────────────────────────────
API_BASE_US = "https://api.bambulab.com"
API_BASE_CN = "https://api.bambulab.cn"
TFA_URL     = "https://bambulab.com/api/sign-in/tfa"
IMPERSONATE = "chrome"

def _require_curl_cffi():
    try:
        from curl_cffi import requests
        return requests
    except ImportError:
        print("\nERROR: 'curl_cffi' is required for Cloud mode (Cloudflare bypass).")
        print("Install with: pip install curl_cffi")
        sys.exit(1)

def cloud_login(email, password, region):
    requests = _require_curl_cffi()
    api_base = API_BASE_CN if region == "cn" else API_BASE_US
    url = f"{api_base}/v1/user-service/user/login"
    resp = requests.post(url, json={"account": email, "password": password},
                         impersonate=IMPERSONATE, timeout=15)
    resp.raise_for_status()
    return resp.json()

def cloud_verify_totp(tfa_code, tfa_key):
    requests = _require_curl_cffi()
    resp = requests.post(TFA_URL,
                         json={"tfaKey": tfa_key, "tfaCode": tfa_code},
                         impersonate=IMPERSONATE, timeout=15)
    resp.raise_for_status()
    token = resp.cookies.get("token")
    if not token:
        try:
            data = resp.json()
            token = data.get("accessToken") or data.get("token")
        except Exception:
            pass
    return token

def cloud_verify_email(email, code, region):
    requests = _require_curl_cffi()
    api_base = API_BASE_CN if region == "cn" else API_BASE_US
    url = f"{api_base}/v1/user-service/user/login"
    resp = requests.post(url, json={"account": email, "code": code},
                         impersonate=IMPERSONATE, timeout=15)
    resp.raise_for_status()
    return resp.json()

def cloud_fetch_devices(token, region):
    requests = _require_curl_cffi()
    api_base = API_BASE_CN if region == "cn" else API_BASE_US
    url = f"{api_base}/v1/iot-service/api/user/bind"
    resp = requests.get(url, headers={"Authorization": f"Bearer {token}"},
                        impersonate=IMPERSONATE, timeout=15)
    resp.raise_for_status()
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

def fetch_user_id_api(token, region):
    api_base = API_BASE_CN if region == "cn" else API_BASE_US
    url = f"{api_base}/v1/user-service/my/profile"
    req = urllib.request.Request(url, headers={
        "Authorization": f"Bearer {token}",
        "User-Agent": "bambu_network_agent/01.09.05.01",
        "Accept": "application/json",
    })
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            data = json.loads(resp.read().decode("utf-8"))
    except Exception as e:
        print(f"  [WARN] Profile API request failed: {e}")
        return None
    def pick(obj):
        if not isinstance(obj, dict):
            return None
        return (obj.get("uidStr") or obj.get("uid")
                or obj.get("userId") or obj.get("user_id"))
    uid = pick(data) or pick(data.get("data") if isinstance(data.get("data"), dict) else None)
    if uid is None:
        print(f"  [WARN] No uid in profile response: {json.dumps(data)[:300]}")
        return None
    return f"u_{uid}"

# ────────────────────────────────────────────────────────────────────────────
#  Interactive prompts
# ────────────────────────────────────────────────────────────────────────────
def ask(prompt, default=None):
    suffix = f" [{default}]" if default else ""
    s = input(f"{prompt}{suffix}: ").strip()
    return s or default

def ask_choice(prompt, choices):
    """choices = list of (label, value) tuples. Returns chosen value."""
    print(prompt)
    for i, (label, _) in enumerate(choices, 1):
        print(f"  {i}. {label}")
    while True:
        s = input(f"Choice [1-{len(choices)}]: ").strip()
        if s.isdigit() and 1 <= int(s) <= len(choices):
            return choices[int(s) - 1][1]
        print("  Invalid choice, try again.")

def prompt_lan():
    print("\n--- LAN Mode Setup ---")
    print("You'll need 3 things from your printer:")
    print("  1. IP address       (Settings > WLAN, on printer LCD)")
    print("  2. Access Code      (Settings > LAN Only Mode, 8 chars)")
    print("  3. Serial Number    (Settings > Device, ~15 chars, UPPERCASE)")
    print()
    ip     = ask("Printer IP address")
    code   = ask("Access Code (8 chars)")
    serial = ask("Serial Number").upper()
    if not ip or not code or not serial:
        print("ERROR: All three fields are required.")
        sys.exit(1)
    return {
        "mode": "lan",
        "broker": ip,
        "username": "bblp",
        "password": code,
        "serial": serial,
    }

def prompt_cloud():
    print("\n--- Cloud Mode Setup ---")
    print("Logs into your Bambu Lab account to fetch your printer list.")
    print("Credentials are sent only to api.bambulab.com (or .cn), never stored.")
    print()

    region = ask_choice("Region:", [
        ("US / EU (most users)", "us"),
        ("China (CN)",           "cn"),
    ])

    email = ask("Bambu Lab email")
    if not email:
        print("ERROR: Email required.")
        sys.exit(1)
    password = getpass.getpass("Password: ")
    if not password:
        print("ERROR: Password required.")
        sys.exit(1)

    print("\nLogging in...")
    try:
        data = cloud_login(email, password, region)
    except Exception as e:
        print(f"Login failed: {e}")
        sys.exit(1)

    token = cloud_extract_token(data)

    if not token:
        login_type = data.get("loginType", "")
        tfa_key = data.get("tfaKey", "")
        if login_type == "tfa":
            print("TOTP 2FA required. Enter code from your authenticator app.")
            code = input("Authenticator code: ").strip()
            print("Verifying...")
            try:
                token = cloud_verify_totp(code, tfa_key)
            except Exception as e:
                print(f"Verification failed: {e}")
                sys.exit(1)
        elif login_type == "verifyCode":
            print("Email verification required. Check your email for the code.")
            code = input("Verification code: ").strip()
            print("Verifying...")
            try:
                data = cloud_verify_email(email, code, region)
            except Exception as e:
                print(f"Verification failed: {e}")
                sys.exit(1)
            token = cloud_extract_token(data)
        else:
            print(f"Unknown loginType: {login_type}")
            print(f"Response: {json.dumps(data, indent=2)[:500]}")
            sys.exit(1)

    if not token:
        print("ERROR: Could not get access token.")
        sys.exit(1)
    print("Login OK.")

    # Resolve userId
    user_id = extract_user_id_jwt(token)
    if not user_id:
        print("JWT decode failed - falling back to profile API...")
        user_id = fetch_user_id_api(token, region)
    if not user_id:
        print("ERROR: Could not determine userId.")
        sys.exit(1)

    # Fetch printers
    print("Fetching your printers...")
    try:
        devices = cloud_fetch_devices(token, region)
    except Exception as e:
        print(f"ERROR: Could not fetch printers: {e}")
        sys.exit(1)

    if not devices:
        print("No printers found on this account.")
        sys.exit(1)

    if len(devices) == 1:
        dev = devices[0]
        print(f"Using only printer on account: {dev.get('name', '?')} "
              f"({dev.get('dev_product_name', '?')}) - {dev.get('dev_id', '?')}")
    else:
        labels = [(f"{d.get('name','?')}  ({d.get('dev_product_name','?')})  "
                   f"- Serial: {d.get('dev_id','?')}", d) for d in devices]
        dev = ask_choice("\nMultiple printers found - pick one:", labels)

    serial = (dev.get("dev_id") or "").upper()
    if not serial:
        print("ERROR: Selected printer has no serial.")
        sys.exit(1)

    broker = "cn.mqtt.bambulab.com" if region == "cn" else "us.mqtt.bambulab.com"
    return {
        "mode": "cloud",
        "broker": broker,
        "username": user_id,
        "password": token,
        "serial": serial,
        "region": region,
        "name": dev.get("name", "") or "",
    }

# ────────────────────────────────────────────────────────────────────────────
#  Diagnostic (TCP / TLS / MQTT auth / pushall capture)
# ────────────────────────────────────────────────────────────────────────────
PORT = 8883

diag = {
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

cfg = {}  # populated in main() before diagnostic runs

def section(title):
    print(f"\n{'='*60}")
    print(f"  {title}")
    print(f"{'='*60}")

def check_serial():
    section("STEP 1: Serial Number Check")
    serial = cfg["serial"]
    print(f"  Serial: {serial}")
    print(f"  Length: {len(serial)}")
    if serial != serial.upper():
        print(f"  [WARN] Serial has lowercase chars! Should be: {serial.upper()}")
        print(f"         Bambu MQTT topics are CASE-SENSITIVE.")
        diag["serial_ok"] = False
    else:
        print(f"  [OK] All uppercase")
    if len(serial) < 10:
        print(f"  [WARN] Serial looks too short (expected ~15 chars)")
        diag["serial_ok"] = False

def check_tcp():
    section("STEP 2: TCP Reachability")
    broker = cfg["broker"]
    print(f"  Testing {broker}:{PORT} ...")
    t0 = time.time()
    try:
        sock = socket.create_connection((broker, PORT), timeout=5)
        ms = (time.time() - t0) * 1000
        sock.close()
        diag["tcp_ok"] = True
        diag["tcp_ms"] = round(ms)
        print(f"  [OK] TCP connected in {diag['tcp_ms']}ms")
    except Exception as e:
        print(f"  [FAIL] {e}")
        if cfg["mode"] == "cloud":
            print(f"  --> Check: internet connection? DNS resolution? firewall?")
        else:
            print(f"  --> Check: printer powered on? same network? firewall?")

def check_tls():
    section("STEP 3: TLS Handshake")
    broker = cfg["broker"]
    print(f"  Testing TLS to {broker}:{PORT} ...")
    ctx = ssl.create_default_context()
    if cfg["mode"] == "lan":
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
    try:
        raw = socket.create_connection((broker, PORT), timeout=10)
        wrapped = ctx.wrap_socket(raw, server_hostname=broker)
        diag["tls_ok"] = True
        diag["tls_cipher"] = wrapped.cipher()[0] if wrapped.cipher() else "unknown"
        diag["tls_version"] = wrapped.version() or "unknown"
        print(f"  [OK] TLS version: {diag['tls_version']}")
        print(f"  [OK] Cipher:      {diag['tls_cipher']}")
        wrapped.close()
    except Exception as e:
        print(f"  [FAIL] TLS handshake failed: {e}")

first_pushall_saved = False

def on_connect(client, userdata, flags, rc):
    rc_text = {
        0: "Connected OK", 1: "Bad protocol version", 2: "Bad client ID",
        3: "Server unavailable", 4: "Bad credentials", 5: "Not authorized",
    }
    diag["mqtt_rc"] = rc
    print(f"  [CONNECT] rc={rc} - {rc_text.get(rc, 'Unknown')}")
    if rc == 0:
        topic_report  = f"device/{cfg['serial']}/report"
        topic_request = f"device/{cfg['serial']}/request"
        client.subscribe(topic_report, qos=0)
        diag["subscribed"] = True
        print(f"  [SUBSCRIBE] {topic_report}")
        pushall = json.dumps({
            "pushing": {"sequence_id": "1", "command": "pushall",
                        "version": 1, "push_target": 1}
        })
        client.publish(topic_request, pushall, qos=0)
        diag["pushall_sent"] = True
        print(f"  [PUSHALL] sent to {topic_request}")
    else:
        if rc == 4 or rc == 5:
            if cfg["mode"] == "lan":
                print(f"  --> Check: is Access Code correct? (8 chars from printer LCD)")
            else:
                print(f"  --> Cloud token may be expired or invalid.")

def dump_ams_details(p):
    ams_obj = p.get("ams")
    if not ams_obj:
        print(f"\n  [AMS] No 'ams' object in payload")
        return

    top_keys = [k for k in ams_obj.keys() if k != "ams"]
    if top_keys:
        print(f"\n  [AMS TOP-LEVEL FIELDS]")
        for k in sorted(top_keys):
            print(f"    {k}: {json.dumps(ams_obj[k])}")

    units = ams_obj.get("ams", [])
    if not units:
        print(f"  [AMS] No 'ams' units array found")
        return

    print(f"\n  [AMS UNITS] {len(units)} unit(s) detected")
    for unit in units:
        uid = unit.get("id", "?")
        print(f"\n  --- AMS Unit {uid} ---")
        for k in sorted(unit.keys()):
            if k == "tray":
                continue
            print(f"    {k}: {json.dumps(unit[k])}")
        drying_keys = [k for k in unit.keys()
                       if any(w in k.lower() for w in ["dry", "humid", "temp", "heat"])]
        if drying_keys:
            print(f"    >> DRYING-RELATED: {', '.join(drying_keys)}")
        trays = unit.get("tray", [])
        for tray in trays:
            tid = tray.get("id", "?")
            ttype = tray.get("tray_sub_brands") or tray.get("tray_type", "empty")
            print(f"    Tray {tid} ({ttype}):")
            for k in sorted(tray.keys()):
                print(f"      {k}: {json.dumps(tray[k])}")

    vt = p.get("vt_tray")
    if vt:
        print(f"\n  [VT_TRAY (external spool)]")
        for k in sorted(vt.keys()):
            print(f"    {k}: {json.dumps(vt[k])}")

    ams_extract = {"ams": ams_obj}
    if vt:
        ams_extract["vt_tray"] = vt
    with open("ams_dump.json", "w", encoding="utf-8") as f:
        json.dump(ams_extract, f, indent=2, ensure_ascii=False)
    print(f"\n  [SAVED] AMS extract -> ams_dump.json")

def on_message(client, userdata, msg):
    global first_pushall_saved
    diag["messages_rx"] += 1
    payload = msg.payload.decode("utf-8", errors="replace")
    size = len(msg.payload)

    try:
        data = json.loads(payload)
        p = data.get("print", {})
        keys = list(p.keys())

        if not first_pushall_saved and size > 1000:
            first_pushall_saved = True
            diag["pushall_bytes"] = size
            diag["first_pushall_keys"] = keys
            print(f"\n  [PUSHALL RESPONSE] {size} bytes, {len(keys)} fields")
            print(f"  Fields: {', '.join(sorted(keys))}")
            status_keys = ["gcode_state", "mc_percent", "nozzle_temper", "bed_temper",
                           "chamber_temper", "nozzle_target_temper", "bed_target_temper",
                           "layer_num", "total_layer_num", "gcode_file",
                           "subtask_name", "print_type", "wifi_signal", "nozzle_diameter"]
            found = {k: p[k] for k in status_keys if k in p}
            if found:
                print(f"  Status: {json.dumps(found, indent=4)}")

            dump_ams_details(p)

            with open("pushall_dump.json", "w", encoding="utf-8") as f:
                json.dump(data, f, indent=2, ensure_ascii=False)
            print(f"  [SAVED] Full pushall response -> pushall_dump.json")
        else:
            diag["delta_count"] += 1
            if diag["delta_count"] <= 3:
                print(f"  [DELTA] {size}B fields=[{', '.join(keys[:5])}]")
                if "ams" in p:
                    ams_units = p["ams"].get("ams", [])
                    for unit in ams_units:
                        uid = unit.get("id", "?")
                        drying_keys = {k: unit[k] for k in unit.keys()
                                       if k != "tray" and any(w in k.lower()
                                       for w in ["dry", "humid", "temp", "heat"])}
                        if drying_keys:
                            print(f"  [AMS DELTA] Unit {uid} drying: {json.dumps(drying_keys)}")
            elif diag["delta_count"] == 4:
                print(f"  ... (suppressing further deltas)")
    except json.JSONDecodeError:
        print(f"  [MSG] {size}B (not JSON): {payload[:200]}")

def check_mqtt():
    section("STEP 4: MQTT Connect + Data")
    print(f"  Mode:      {cfg['mode'].upper()}")
    print(f"  Broker:    {cfg['broker']}:{PORT}")
    print(f"  Username:  {cfg['username']}")
    print(f"  Client ID: bambu_diag_test")
    print(f"  Protocol:  MQTT v3.1.1")
    print()

    client = mqtt.Client(client_id="bambu_diag_test", protocol=mqtt.MQTTv311)
    client.username_pw_set(cfg["username"], cfg["password"])

    if cfg["mode"] == "cloud":
        client.tls_set()
    else:
        client.tls_set(cert_reqs=ssl.CERT_NONE)
        client.tls_insecure_set(True)

    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(cfg["broker"], PORT, keepalive=60)
    except Exception as e:
        print(f"  [FAIL] Connection error: {e}")
        return

    print(f"  Waiting for data (30s)...")
    t0 = time.time()
    while time.time() - t0 < 30:
        client.loop(timeout=1.0)
    client.disconnect()

def print_summary():
    section("DIAGNOSTIC SUMMARY")
    d = diag
    def status(ok, label):
        tag = "PASS" if ok else "FAIL"
        print(f"  [{tag}] {label}")
    status(d["serial_ok"],       f"Serial number format ({cfg['serial']})")
    status(d["tcp_ok"],          f"TCP reachable ({d['tcp_ms']}ms)")
    status(d["tls_ok"],          f"TLS handshake ({d['tls_version']} / {d['tls_cipher']})")
    status(d["mqtt_rc"] == 0,    f"MQTT auth (rc={d['mqtt_rc']})")
    status(d["subscribed"],      f"Topic subscribed")
    status(d["pushall_sent"],    f"Pushall request sent")
    status(d["messages_rx"] > 0, f"Messages received ({d['messages_rx']} total)")
    status(d["pushall_bytes"]>0, f"Pushall response ({d['pushall_bytes']} bytes, {len(d['first_pushall_keys'])} fields)")

    if d["messages_rx"] > 0:
        print(f"\n  Printer is responding normally.")
        print(f"  If BambuHelper still shows UNKNOWN, the issue is in the ESP config.")
    elif d["mqtt_rc"] == 0:
        print(f"\n  MQTT connected but NO messages received.")
        print(f"  Possible causes:")
        print(f"    - Serial number mismatch (topic won't match)")
        print(f"    - Printer firmware issue")
    elif d["mqtt_rc"] in (4, 5):
        print(f"\n  Authentication failed.")
        if cfg["mode"] == "cloud":
            print(f"  -> Cloud token may be expired (valid ~3 months)")
            print(f"  -> Try logging in again")
        else:
            print(f"  -> Re-check Access Code on printer LCD (Settings > LAN Only Mode)")
    elif not d["tcp_ok"]:
        print(f"\n  Printer not reachable on network.")
        if cfg["mode"] == "cloud":
            print(f"  -> Check internet connection and DNS")
        else:
            print(f"  -> Check IP, same subnet, printer powered on")

    print(f"\n  Full pushall saved to: pushall_dump.json")
    print(f"  Share this summary (redact serial/code if needed) for support.")

def push_config_to_device(bh_ip, form):
    """POST form-urlencoded config to BambuHelper /save/printer endpoint."""
    body = urllib.parse.urlencode(form).encode("utf-8")
    url = f"http://{bh_ip}/save/printer"
    req = urllib.request.Request(
        url, data=body, method="POST",
        headers={"Content-Type": "application/x-www-form-urlencoded"},
    )
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            return resp.status, resp.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", errors="replace") if e.fp else ""
    except Exception as e:
        return -1, str(e)

def prompt_push_config():
    """Setup mode: gather config (cloud or LAN) and POST it to a BambuHelper device."""
    section("Configure BambuHelper Device")
    print("  This will send your printer config to a BambuHelper on your network.")
    print("  Find the BambuHelper IP on the device screen (shown after WiFi setup).")
    print()

    mode = ask_choice("Printer connection type:", [
        ("Cloud (login with Bambu account)",                       "cloud"),
        ("LAN (printer on local network, LAN Only Mode enabled)",  "lan"),
    ])

    if mode == "lan":
        data = prompt_lan()
        # prompt_lan returns: mode, broker (=ip), username (=bblp), password (=code), serial
        printer_name = ""
    else:
        data = prompt_cloud()
        printer_name = data.get("name", "")

    print("\n--- BambuHelper Device ---")
    bh_ip = ask("BambuHelper IP (shown on its screen)")
    if not bh_ip:
        print("ERROR: BambuHelper IP required.")
        sys.exit(1)

    slot_choice = ask_choice("Configure which slot on the BambuHelper?", [
        ("Printer 1 (default)", 0),
        ("Printer 2 (only if dual-printer mode is enabled)", 1),
    ])

    suggested = printer_name or ("Bambu " + data["serial"][:4])
    friendly = ask("Friendly name for this printer", default=suggested)

    # Field names must match the handler in src/web_server.cpp::handleSavePrinter
    # (connmode/pname, region as "us"/"eu"/"cn") - the web UI's savePrinter() JS
    # is the source of truth for this shape.
    if mode == "lan":
        form = {
            "slot": str(slot_choice),
            "connmode": "lan",
            "pname": friendly,
            "serial": data["serial"],
            "ip": data["broker"],
            "code": data["password"],
        }
    else:
        region_str = "cn" if data.get("region") == "cn" else "us"
        form = {
            "slot": str(slot_choice),
            "connmode": "cloud_all",
            "pname": friendly,
            "serial": data["serial"],
            "token": data["password"],
            "region": region_str,
        }

    section("Sending config to BambuHelper")
    print(f"  Target:  http://{bh_ip}/save/printer")
    print(f"  Slot:    {slot_choice + 1}")
    print(f"  Mode:    {form['connmode']}")
    print(f"  Serial:  {form['serial']}")
    print(f"  Name:    {friendly}")
    print()
    print("  POSTing...")

    status, body = push_config_to_device(bh_ip, form)

    if status == -1:
        print(f"\n  [FAIL] Could not reach BambuHelper at {bh_ip}: {body}")
        print(f"  --> Check IP on device screen, same WiFi, no firewall blocking.")
        sys.exit(1)

    print(f"  HTTP {status}")
    print(f"  Response: {body[:400]}")

    parsed = None
    try:
        parsed = json.loads(body)
    except json.JSONDecodeError:
        pass

    if parsed is None:
        if status == 200:
            print("\n  [OK] Config sent (no JSON response to parse).")
        else:
            print(f"\n  [FAIL] HTTP {status} - device did not return JSON.")
            sys.exit(1)
        return

    resp_status = parsed.get("status", "")
    warning = parsed.get("warning", "").strip()
    message = parsed.get("message", "").strip()

    if resp_status == "ok" and not warning:
        print("\n  [SUCCESS] Config sent. Check the BambuHelper screen - it should")
        print("            start connecting to your printer within a few seconds.")
    elif resp_status == "ok" and warning:
        # Device saved but flagged missing/invalid fields - treat as partial.
        print(f"\n  [WARNING] Device accepted the request but reported issues:")
        print(f"     {warning}")
        print(f"\n  This usually means a required field was missing or rejected.")
        print(f"  The config was still written - check the BambuHelper web UI to")
        print(f"  see what landed and fix any gaps there.")
        sys.exit(2)
    else:
        print(f"\n  [FAIL] Device rejected the config.")
        if message:
            print(f"     {message}")
        sys.exit(1)

def run_diagnostic():
    """Diagnostic mode: gather config and run the full TLS/MQTT/AMS check."""
    section("Bambu Lab MQTT Diagnostic")
    print("  Tests the full connection chain and saves a pushall dump.")
    print()

    mode = ask_choice("Connection mode:", [
        ("LAN (printer on local network, LAN Only Mode enabled)", "lan"),
        ("Cloud (login with Bambu account)",                       "cloud"),
    ])

    global cfg
    cfg = prompt_lan() if mode == "lan" else prompt_cloud()

    section("Starting diagnostic")
    print(f"  Mode:    {cfg['mode'].upper()}")
    print(f"  Broker:  {cfg['broker']}")
    print(f"  Serial:  {cfg['serial']}")

    check_serial()
    check_tcp()
    if not diag["tcp_ok"]:
        print_summary()
        return
    check_tls()
    check_mqtt()
    print_summary()

def main():
    section("BambuHelper Companion Tool")
    print("  Set up your BambuHelper device, or diagnose printer connection issues.")
    print()

    action = ask_choice("What would you like to do?", [
        ("Configure BambuHelper device (one-click setup)",  "setup"),
        ("Run printer diagnostic (test connection + dump)", "diag"),
    ])

    if action == "setup":
        prompt_push_config()
    else:
        run_diagnostic()

# Keep the console window open after a double-click run finishes (PyInstaller
# sets sys.frozen). Runs on every exit path including sys.exit() and crashes.
def _pause_on_exit():
    if getattr(sys, "frozen", False):
        try:
            input("\nPress Enter to exit...")
        except (EOFError, KeyboardInterrupt):
            pass
atexit.register(_pause_on_exit)

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\nAborted by user.")
        sys.exit(130)
