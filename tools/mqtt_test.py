"""
Bambu Lab MQTT Diagnostic Tool
Tests TLS+MQTT connection to a Bambu printer in LAN or Cloud mode.

Usage: python mqtt_test.py
  - requires: pip install paho-mqtt
  - edit the config section below before running

LAN mode:  fill in PRINTER_IP, ACCESS_CODE, SERIAL, set MODE = "lan"
Cloud mode: fill in SERIAL, CLOUD_TOKEN, CLOUD_REGION, set MODE = "cloud"
  - Token: grab from browser dev tools on bambulab.com (cookie or API token)
  - Region: "us" (also for EU accounts) or "cn"
"""

import ssl
import socket
import time
import json
import base64
import paho.mqtt.client as mqtt

# ==== EDIT THESE WITH YOUR PRINTER INFO ====
MODE         = "lan"                   # "lan" or "cloud"

# -- LAN mode settings --
PRINTER_IP   = "YOUR_PRINTER_IP"       # e.g. "192.168.1.100"
ACCESS_CODE  = "YOUR_ACCESS_CODE"      # 8 chars from printer LCD

# -- Cloud mode settings --
CLOUD_TOKEN  = "YOUR_CLOUD_TOKEN"      # JWT token from bambulab.com
CLOUD_REGION = "us"                    # "us" (also for EU) or "cn"

# -- Both modes --
SERIAL       = "YOUR_SERIAL_NUMBER"    # e.g. "01P00C..." (MUST be UPPERCASE)
# ============================================

PORT          = 8883

# Derive cloud userId from JWT token
def extract_user_id(token):
    """Extract uid from JWT payload and return 'u_{uid}'."""
    try:
        parts = token.split(".")
        if len(parts) < 2:
            return None
        # Fix base64url padding
        payload_b64 = parts[1]
        payload_b64 += "=" * (4 - len(payload_b64) % 4)
        payload_b64 = payload_b64.replace("-", "+").replace("_", "/")
        decoded = base64.b64decode(payload_b64)
        data = json.loads(decoded)
        uid = data.get("uid") or data.get("sub") or data.get("user_id")
        if uid:
            return f"u_{uid}"
    except Exception as e:
        print(f"  [WARN] Failed to decode JWT: {e}")
    return None

# Set up connection params based on mode
if MODE == "cloud":
    CLOUD_USER_ID = extract_user_id(CLOUD_TOKEN)
    if not CLOUD_USER_ID:
        print("ERROR: Could not extract userId from cloud token!")
        print("       Check that CLOUD_TOKEN is a valid JWT.")
        exit(1)
    BROKER   = "cn.mqtt.bambulab.com" if CLOUD_REGION == "cn" else "us.mqtt.bambulab.com"
    USERNAME = CLOUD_USER_ID
    PASSWORD = CLOUD_TOKEN
    print(f"  Cloud mode: broker={BROKER}, userId={CLOUD_USER_ID}")
else:
    BROKER   = PRINTER_IP
USERNAME      = "bblp"
    PASSWORD = ACCESS_CODE

TOPIC_REPORT  = f"device/{SERIAL}/report"
TOPIC_REQUEST = f"device/{SERIAL}/request"

# Collect results for summary
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

def section(title):
    print(f"\n{'='*60}")
    print(f"  {title}")
    print(f"{'='*60}")

# ---- STEP 1: Validate serial ----
def check_serial():
    section("STEP 1: Serial Number Check")
    print(f"  Serial: {SERIAL}")
    print(f"  Length:  {len(SERIAL)}")
    if SERIAL != SERIAL.upper():
        print(f"  [WARN] Serial has lowercase chars! Should be: {SERIAL.upper()}")
        print(f"         Bambu MQTT topics are CASE-SENSITIVE.")
        diag["serial_ok"] = False
    else:
        print(f"  [OK] All uppercase")
    if len(SERIAL) < 10:
        print(f"  [WARN] Serial looks too short (expected 15 chars)")
        diag["serial_ok"] = False

# ---- STEP 2: TCP reachability ----
def check_tcp():
    section("STEP 2: TCP Reachability")
    print(f"  Testing {BROKER}:{PORT} ...")
    t0 = time.time()
    try:
        sock = socket.create_connection((BROKER, PORT), timeout=5)
        ms = (time.time() - t0) * 1000
        sock.close()
        diag["tcp_ok"] = True
        diag["tcp_ms"] = round(ms)
        print(f"  [OK] TCP connected in {diag['tcp_ms']}ms")
    except Exception as e:
        print(f"  [FAIL] {e}")
        if MODE == "cloud":
            print(f"  --> Check: internet connection? DNS resolution? firewall?")
        else:
        print(f"  --> Check: printer powered on? same network? firewall?")

# ---- STEP 3: TLS handshake ----
def check_tls():
    section("STEP 3: TLS Handshake")
    print(f"  Testing TLS to {BROKER}:{PORT} ...")
    ctx = ssl.create_default_context()
    if MODE == "lan":
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    # Cloud mode: use default CA verification
    try:
        raw = socket.create_connection((BROKER, PORT), timeout=10)
        wrapped = ctx.wrap_socket(raw, server_hostname=BROKER)
        diag["tls_ok"] = True
        diag["tls_cipher"] = wrapped.cipher()[0] if wrapped.cipher() else "unknown"
        diag["tls_version"] = wrapped.version() or "unknown"
        print(f"  [OK] TLS version: {diag['tls_version']}")
        print(f"  [OK] Cipher:      {diag['tls_cipher']}")
        wrapped.close()
    except Exception as e:
        print(f"  [FAIL] TLS handshake failed: {e}")

# ---- STEP 4: MQTT connect + data ----
first_pushall_saved = False

def on_connect(client, userdata, flags, rc):
    rc_text = {
        0: "Connected OK",
        1: "Bad protocol version",
        2: "Bad client ID",
        3: "Server unavailable",
        4: "Bad credentials",
        5: "Not authorized",
    }
    diag["mqtt_rc"] = rc
    print(f"  [CONNECT] rc={rc} - {rc_text.get(rc, 'Unknown')}")
    if rc == 0:
        client.subscribe(TOPIC_REPORT, qos=0)
        diag["subscribed"] = True
        print(f"  [SUBSCRIBE] {TOPIC_REPORT}")
        pushall = json.dumps({
            "pushing": {"sequence_id": "1", "command": "pushall",
                        "version": 1, "push_target": 1}
        })
        client.publish(TOPIC_REQUEST, pushall, qos=0)
        diag["pushall_sent"] = True
        print(f"  [PUSHALL] sent to {TOPIC_REQUEST}")
    else:
        if rc == 4 or rc == 5:
            print(f"  --> Check: is Access Code correct? (8 chars from printer LCD)")

def dump_ams_details(p):
    """Print all AMS unit-level and tray-level fields for drying/humidity analysis."""
    ams_obj = p.get("ams")
    if not ams_obj:
        print(f"\n  [AMS] No 'ams' object in payload")
        return

    # Top-level AMS fields (outside the units array)
    top_keys = [k for k in ams_obj.keys() if k != "ams"]
    if top_keys:
        print(f"\n  [AMS TOP-LEVEL FIELDS]")
        for k in sorted(top_keys):
            print(f"    {k}: {json.dumps(ams_obj[k])}")

    # Per-unit fields
    units = ams_obj.get("ams", [])
    if not units:
        print(f"  [AMS] No 'ams' units array found")
        return

    print(f"\n  [AMS UNITS] {len(units)} unit(s) detected")
    for unit in units:
        uid = unit.get("id", "?")
        print(f"\n  --- AMS Unit {uid} ---")
        # Print ALL unit-level fields (excluding tray array)
        for k in sorted(unit.keys()):
            if k == "tray":
                continue
            print(f"    {k}: {json.dumps(unit[k])}")

        # Highlight drying-related fields
        drying_keys = [k for k in unit.keys()
                       if any(w in k.lower() for w in ["dry", "humid", "temp", "heat"])]
        if drying_keys:
            print(f"    >> DRYING-RELATED: {', '.join(drying_keys)}")

        # Per-tray fields
        trays = unit.get("tray", [])
        for tray in trays:
            tid = tray.get("id", "?")
            ttype = tray.get("tray_sub_brands") or tray.get("tray_type", "empty")
            print(f"    Tray {tid} ({ttype}):")
            for k in sorted(tray.keys()):
                print(f"      {k}: {json.dumps(tray[k])}")

    # Also check for vt_tray
    vt = p.get("vt_tray")
    if vt:
        print(f"\n  [VT_TRAY (external spool)]")
        for k in sorted(vt.keys()):
            print(f"    {k}: {json.dumps(vt[k])}")

    # Save AMS-only extract for easy sharing
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

        # First large message = pushall response
        if not first_pushall_saved and size > 1000:
            first_pushall_saved = True
            diag["pushall_bytes"] = size
            diag["first_pushall_keys"] = keys
            print(f"\n  [PUSHALL RESPONSE] {size} bytes, {len(keys)} fields")
            print(f"  Fields: {', '.join(sorted(keys))}")
            # Show key status fields
            status_keys = ["gcode_state", "mc_percent", "nozzle_temper", "bed_temper",
                           "chamber_temper", "nozzle_target_temper", "bed_target_temper",
                           "layer_num", "total_layer_num", "gcode_file",
                           "subtask_name", "wifi_signal", "nozzle_diameter"]
            found = {k: p[k] for k in status_keys if k in p}
            if found:
                print(f"  Status: {json.dumps(found, indent=4)}")

            # Dump full AMS details (drying, humidity, all fields)
            dump_ams_details(p)

            # Save full pushall to file for sharing
            with open("pushall_dump.json", "w", encoding="utf-8") as f:
                json.dump(data, f, indent=2, ensure_ascii=False)
            print(f"  [SAVED] Full pushall response -> pushall_dump.json")
        else:
            diag["delta_count"] += 1
            if diag["delta_count"] <= 3:
                print(f"  [DELTA] {size}B fields=[{', '.join(keys[:5])}]")
                # Check for AMS deltas with drying changes
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
    print(f"  Mode:      {MODE.upper()}")
    print(f"  Broker:    {BROKER}:{PORT}")
    print(f"  Username:  {USERNAME}")
    print(f"  Client ID: bambu_diag_test")
    print(f"  Protocol:  MQTT v3.1.1")
    print()

    client = mqtt.Client(client_id="bambu_diag_test", protocol=mqtt.MQTTv311)
    client.username_pw_set(USERNAME, PASSWORD)

    if MODE == "cloud":
        # Cloud: use proper CA verification
        client.tls_set()
    else:
        # LAN: printer uses self-signed cert
    client.tls_set(cert_reqs=ssl.CERT_NONE)
    client.tls_insecure_set(True)
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(BROKER, PORT, keepalive=60)
    except Exception as e:
        print(f"  [FAIL] Connection error: {e}")
        return

    print(f"  Waiting for data (30s)...")
    t0 = time.time()
    while time.time() - t0 < 30:
        client.loop(timeout=1.0)

    client.disconnect()

# ---- SUMMARY ----
def print_summary():
    section("DIAGNOSTIC SUMMARY")
    d = diag

    def status(ok, label):
        tag = "PASS" if ok else "FAIL"
        print(f"  [{tag}] {label}")

    status(d["serial_ok"],  f"Serial number format ({SERIAL})")
    status(d["tcp_ok"],     f"TCP reachable ({d['tcp_ms']}ms)")
    status(d["tls_ok"],     f"TLS handshake ({d['tls_version']} / {d['tls_cipher']})")
    status(d["mqtt_rc"]==0, f"MQTT auth (rc={d['mqtt_rc']})")
    status(d["subscribed"], f"Topic subscribed")
    status(d["pushall_sent"], f"Pushall request sent")
    status(d["messages_rx"]>0, f"Messages received ({d['messages_rx']} total)")
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
        if MODE == "cloud":
            print(f"  -> Cloud token may be expired (valid ~3 months)")
            print(f"  -> Re-extract from bambulab.com browser session")
        else:
        print(f"  -> Re-check Access Code on printer LCD (Settings > LAN Only Mode)")
    elif not d["tcp_ok"]:
        print(f"\n  Printer not reachable on network.")
        if MODE == "cloud":
            print(f"  -> Check internet connection and DNS")
        else:
        print(f"  -> Check IP, same subnet, printer powered on")

    print(f"\n  Full pushall saved to: pushall_dump.json")
    print(f"  Share this summary (redact serial/code if needed) for support.")

def main():
    section("Bambu Lab MQTT Diagnostic Tool")
    print(f"  Mode:    {MODE.upper()}")
    print(f"  Broker:  {BROKER}")
    print(f"  Serial:  {SERIAL}")

    check_serial()
    check_tcp()
    if not diag["tcp_ok"]:
        print_summary()
        return
    check_tls()
    check_mqtt()
    print_summary()

if __name__ == "__main__":
    main()
