#!/usr/bin/env python3
"""
BambuHelper Companion - terminal frontend.

Same two modes as the GUI, driven from a console instead:
  1. Configure a BambuHelper device (one-click cloud/LAN setup over the LAN)
  2. Run a printer diagnostic (test the connection + dump status for support)

All the actual work lives in bambu_core; this file only asks questions and
prints answers, so the GUI and the terminal can never drift apart in behaviour.
"""

import getpass
import json
import os
import sys

import bambu_core as core

# Level -> console tag. bambu_core emits these as it works.
_TAGS = {"ok": "[OK] ", "fail": "[FAIL] ", "warn": "[WARN] ", "info": "", "step": ""}


def section(title):
    print(f"\n{'=' * 60}")
    print(f"  {title}")
    print(f"{'=' * 60}")


def log(text, level="info"):
    if level == "step":
        section(text)
        return
    print(f"  {_TAGS.get(level, '')}{text}")


def ask(prompt, default=None):
    suffix = f" [{default}]" if default else ""
    s = input(f"{prompt}{suffix}: ").strip()
    return s or default


def ask_choice(prompt, choices):
    """choices = list of (label, value) tuples. Returns the chosen value."""
    print(prompt)
    for i, (label, _) in enumerate(choices, 1):
        print(f"  {i}. {label}")
    while True:
        s = input(f"Choice [1-{len(choices)}]: ").strip()
        if s.isdigit() and 1 <= int(s) <= len(choices):
            return choices[int(s) - 1][1]
        print("  Invalid choice, try again.")


def die(message):
    print(f"\nERROR: {message}")
    sys.exit(1)


# ───────────────────────────────────────────────────────────────────────────
#  Gathering printer details
# ───────────────────────────────────────────────────────────────────────────
def prompt_lan():
    print("\n--- LAN Mode Setup ---")
    print("You'll need 3 things from your printer:")
    print("  1. IP address       (Settings > WLAN, on printer LCD)")
    print("  2. Access Code      (Settings > LAN Only Mode, 8 chars)")
    print("  3. Serial Number    (Settings > Device, ~15 chars, UPPERCASE)")
    print()
    ip = ask("Printer IP address")
    code = ask("Access Code (8 chars)")
    serial = (ask("Serial Number") or "").upper()
    if not ip or not code or not serial:
        die("All three fields are required.")
    return {"mode": "lan", "broker": ip, "username": "bblp",
            "password": code, "serial": serial, "name": ""}


def prompt_cloud():
    print("\n--- Cloud Mode Setup ---")
    print("Logs into your Bambu Lab account to fetch your printer list.")
    print("Credentials are sent only to api.bambulab.com (or .cn), never stored.")
    print()

    region = ask_choice("Region:", [
        ("US / EU (most users)", "us"),
        ("China (CN)", "cn"),
    ])

    email = ask("Bambu Lab email")
    if not email:
        die("Email required.")
    password = getpass.getpass("Password: ")
    if not password:
        die("Password required.")

    print("\nLogging in...")
    try:
        data = core.cloud_login(email, password, region)
    except Exception as e:
        die(f"Login failed: {e}")

    token = core.cloud_extract_token(data)
    if not token:
        login_type = data.get("loginType", "")
        if login_type == "tfa":
            print("TOTP 2FA required. Enter the code from your authenticator app.")
            code = input("Authenticator code: ").strip()
            print("Verifying...")
            try:
                token = core.cloud_verify_totp(code, data.get("tfaKey", ""))
            except Exception as e:
                die(f"Verification failed: {e}")
        elif login_type == "verifyCode":
            print("Email verification required. Check your email for the code.")
            code = input("Verification code: ").strip()
            print("Verifying...")
            try:
                data = core.cloud_verify_email(email, code, region)
            except Exception as e:
                die(f"Verification failed: {e}")
            token = core.cloud_extract_token(data)
        else:
            die(f"Unknown loginType: {login_type}\n"
                f"Response: {json.dumps(data, indent=2)[:500]}")

    if not token:
        die("Could not get an access token.")
    print("Login OK.")

    try:
        user_id = core.resolve_user_id(token, region, log)
    except Exception as e:
        die(str(e))

    print("Fetching your printers...")
    try:
        devices = core.cloud_fetch_devices(token, region)
    except Exception as e:
        die(f"Could not fetch printers: {e}")
    if not devices:
        die("No printers found on this account.")

    if len(devices) == 1:
        dev = devices[0]
        print(f"Using the only printer on the account: {core.device_label(dev)}")
    else:
        dev = ask_choice("\nMultiple printers found - pick one:",
                         [(core.device_label(d), d) for d in devices])

    serial = (dev.get("dev_id") or "").upper()
    if not serial:
        die("The selected printer has no serial number.")

    return {"mode": "cloud", "broker": core.broker_for_region(region),
            "username": user_id, "password": token, "serial": serial,
            "region": region, "name": dev.get("name") or ""}


def prompt_printer():
    mode = ask_choice("Printer connection type:", [
        ("Cloud (login with Bambu account)", "cloud"),
        ("LAN (printer on local network, LAN Only Mode enabled)", "lan"),
    ])
    return prompt_lan() if mode == "lan" else prompt_cloud()


# ───────────────────────────────────────────────────────────────────────────
#  Mode 1: configure a device
# ───────────────────────────────────────────────────────────────────────────
def choose_device_ip():
    """Offer a LAN scan, fall back to typing the IP."""
    how = ask_choice("\nHow should we find your BambuHelper?", [
        ("Scan my network for it (recommended)", "scan"),
        ("I'll type its IP address", "manual"),
    ])
    if how == "manual":
        return ask("BambuHelper IP (shown on its screen)")

    found = core.discover_devices(log)
    if not found:
        return ask("BambuHelper IP (shown on its screen)")
    if len(found) == 1:
        d = found[0]
        label = d["name"] or "unnamed"
        print(f"\nFound one BambuHelper: {d['ip']} ({label})")
        return d["ip"]
    choices = [(f"{d['ip']}  {d['name'] or 'unnamed'}"
                f"{'' if d['configured'] else '  (not configured yet)'}", d["ip"])
               for d in found]
    choices.append(("Enter an IP by hand", None))
    picked = ask_choice("\nSeveral BambuHelper devices found - pick one:", choices)
    return picked or ask("BambuHelper IP")


def configure_device():
    section("Configure BambuHelper Device")
    print("  This sends your printer config to a BambuHelper on your network.")
    print()

    printer = prompt_printer()

    bh_ip = choose_device_ip()
    if not bh_ip:
        die("BambuHelper IP required.")

    slot = ask_choice("Configure which slot on the BambuHelper?", [
        ("Printer 1 (default)", 0),
        ("Printer 2 (only if dual-printer mode is enabled)", 1),
    ])

    suggested = printer.get("name") or ("Bambu " + printer["serial"][:4])
    friendly = ask("Friendly name for this printer", default=suggested)
    form = core.build_printer_form(slot, printer, friendly)

    section("Sending config to BambuHelper")
    print(f"  Target:  http://{bh_ip}/save/printer")
    print(f"  Slot:    {slot + 1}")
    print(f"  Mode:    {form['connmode']}")
    print(f"  Serial:  {form['serial']}")
    print(f"  Name:    {form['pname']}")
    print("\n  POSTing...")

    status, body = core.push_config_to_device(bh_ip, form)
    level, message = core.interpret_push_result(status, body)
    print(f"  HTTP {status}")
    print()
    log(message, level)
    if level == "fail":
        sys.exit(1)
    if level == "warn":
        sys.exit(2)


# ───────────────────────────────────────────────────────────────────────────
#  Mode 2: diagnostic
# ───────────────────────────────────────────────────────────────────────────
def save_dumps(diag):
    """Write the captured payloads next to wherever the tool was run."""
    written = []
    if diag.ams_extract is not None:
        with open("ams_dump.json", "w", encoding="utf-8") as f:
            json.dump(diag.ams_extract, f, indent=2, ensure_ascii=False)
        written.append("ams_dump.json")
    if diag.pushall is not None:
        with open("pushall_dump.json", "w", encoding="utf-8") as f:
            json.dump(diag.pushall, f, indent=2, ensure_ascii=False)
        written.append("pushall_dump.json")
    return written


def run_diagnostic():
    section("Bambu Lab MQTT Diagnostic")
    print("  Tests the full connection chain and saves a pushall dump.")
    print()

    if not core.MQTT_AVAILABLE:
        die("'paho-mqtt' is required for the diagnostic.\n"
            "       Install with: pip install paho-mqtt")

    mode = ask_choice("Connection mode:", [
        ("LAN (printer on local network, LAN Only Mode enabled)", "lan"),
        ("Cloud (login with Bambu account)", "cloud"),
    ])
    cfg = prompt_lan() if mode == "lan" else prompt_cloud()

    section("Starting diagnostic")
    diag = core.Diagnostic(cfg, log=log)
    diag.run()

    section("DIAGNOSTIC SUMMARY")
    for ok, label in diag.summary_rows():
        print(f"  [{'PASS' if ok else 'FAIL'}] {label}")
    level, text = diag.verdict()
    print()
    log(text, level)

    written = save_dumps(diag)
    if written:
        print()
        for name in written:
            print(f"  Saved: {os.path.abspath(name)}")
        print("\n  Email pushall_dump.json to keralots@gmail.com with a short note")
        print("  about what looks wrong. It contains your printer serial and state,")
        print("  but not your password or cloud token.")
    else:
        print("\n  No pushall captured - nothing to send.")


def main():
    section("BambuHelper Companion Tool")
    print("  Set up your BambuHelper device, or diagnose printer connection issues.")
    print()
    action = ask_choice("What would you like to do?", [
        ("Configure BambuHelper device (one-click setup)", "setup"),
        ("Run printer diagnostic (test connection + dump)", "diag"),
    ])
    if action == "setup":
        configure_device()
    else:
        run_diagnostic()
