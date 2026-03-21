"""
Bambu Lab MQTT Diagnostic Tool
Tests TLS+MQTT connection to a Bambu printer in LAN mode.

Usage: python mqtt_test.py
  - requires: pip install paho-mqtt
  - edit PRINTER_IP, ACCESS_CODE, SERIAL below before running
"""

import ssl
import socket
import time
import json
import paho.mqtt.client as mqtt

# ==== EDIT THESE WITH YOUR PRINTER INFO ====
PRINTER_IP   = "YOUR_PRINTER_IP"       # e.g. "192.168.1.100"
ACCESS_CODE  = "YOUR_ACCESS_CODE"      # 8 chars from printer LCD
SERIAL       = "YOUR_SERIAL_NUMBER"    # e.g. "01P00C..." (MUST be UPPERCASE)
# ============================================

PORT          = 8883
USERNAME      = "bblp"
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
    print(f"  Testing {PRINTER_IP}:{PORT} ...")
    t0 = time.time()
    try:
        sock = socket.create_connection((PRINTER_IP, PORT), timeout=5)
        ms = (time.time() - t0) * 1000
        sock.close()
        diag["tcp_ok"] = True
        diag["tcp_ms"] = round(ms)
        print(f"  [OK] TCP connected in {diag['tcp_ms']}ms")
    except Exception as e:
        print(f"  [FAIL] {e}")
        print(f"  --> Check: printer powered on? same network? firewall?")

# ---- STEP 3: TLS handshake ----
def check_tls():
    section("STEP 3: TLS Handshake")
    print(f"  Testing TLS to {PRINTER_IP}:{PORT} ...")
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    try:
        raw = socket.create_connection((PRINTER_IP, PORT), timeout=10)
        wrapped = ctx.wrap_socket(raw, server_hostname=PRINTER_IP)
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
            "pushing": {"sequence_id": "1", "command": "pushall"}
        })
        client.publish(TOPIC_REQUEST, pushall, qos=0)
        diag["pushall_sent"] = True
        print(f"  [PUSHALL] sent to {TOPIC_REQUEST}")
    else:
        if rc == 4 or rc == 5:
            print(f"  --> Check: is Access Code correct? (8 chars from printer LCD)")

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
            # Save full pushall to file for sharing
            with open("pushall_dump.json", "w", encoding="utf-8") as f:
                json.dump(data, f, indent=2, ensure_ascii=False)
            print(f"  [SAVED] Full pushall response -> pushall_dump.json")
        else:
            diag["delta_count"] += 1
            if diag["delta_count"] <= 3:
                print(f"  [DELTA] {size}B fields=[{', '.join(keys[:5])}]")
            elif diag["delta_count"] == 4:
                print(f"  ... (suppressing further deltas)")
    except json.JSONDecodeError:
        print(f"  [MSG] {size}B (not JSON): {payload[:200]}")

def check_mqtt():
    section("STEP 4: MQTT Connect + Data")
    print(f"  Broker:    {PRINTER_IP}:{PORT}")
    print(f"  Username:  {USERNAME}")
    print(f"  Client ID: bambu_diag_test")
    print(f"  Protocol:  MQTT v3.1.1")
    print()

    client = mqtt.Client(client_id="bambu_diag_test", protocol=mqtt.MQTTv311)
    client.username_pw_set(USERNAME, ACCESS_CODE)
    client.tls_set(cert_reqs=ssl.CERT_NONE)
    client.tls_insecure_set(True)
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(PRINTER_IP, PORT, keepalive=60)
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
        print(f"  -> Re-check Access Code on printer LCD (Settings > LAN Only Mode)")
    elif not d["tcp_ok"]:
        print(f"\n  Printer not reachable on network.")
        print(f"  -> Check IP, same subnet, printer powered on")

    print(f"\n  Full pushall saved to: pushall_dump.json")
    print(f"  Share this summary (redact serial/code if needed) for support.")

def main():
    section("Bambu Lab MQTT Diagnostic Tool")
    print(f"  Printer: {PRINTER_IP}")
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
