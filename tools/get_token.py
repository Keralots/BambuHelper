#!/usr/bin/env python3
"""
BambuHelper Cloud Token Helper

Gets a Bambu Lab cloud access token for use with BambuHelper.
Paste the token into BambuHelper's web interface (Paste Token field).

Usage:
    python get_token.py

Requirements:
    pip install requests
"""

import getpass
import json
import sys

try:
    import requests
except ImportError:
    print("Error: 'requests' package required. Install with: pip install requests")
    sys.exit(1)

API_BASE = "https://api.bambulab.com"
LOGIN_URL = f"{API_BASE}/v1/user-service/user/login"
DEVICES_URL = f"{API_BASE}/v1/iot-service/api/user/bind"

HEADERS = {
    "Content-Type": "application/json",
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                  "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36",
    "Accept": "application/json, text/plain, */*",
}


def login(email: str, password: str) -> dict:
    """Login with email + password. Returns API response dict."""
    resp = requests.post(LOGIN_URL, json={"account": email, "password": password},
                         headers=HEADERS, timeout=15)
    resp.raise_for_status()
    return resp.json()


def verify(email: str, code: str) -> dict:
    """Submit 2FA verification code. Returns API response dict."""
    resp = requests.post(LOGIN_URL, json={"account": email, "code": code},
                         headers=HEADERS, timeout=15)
    resp.raise_for_status()
    return resp.json()


def fetch_devices(token: str) -> list:
    """Fetch printer list from cloud."""
    h = {**HEADERS, "Authorization": f"Bearer {token}"}
    resp = requests.get(DEVICES_URL, headers=h, timeout=15)
    resp.raise_for_status()
    data = resp.json()
    return data.get("data", [])


def extract_token(data: dict) -> str | None:
    """Extract accessToken from login/verify response."""
    token = data.get("accessToken")
    if not token and isinstance(data.get("data"), dict):
        token = data["data"].get("accessToken")
    return token


def main():
    print("=" * 50)
    print("  BambuHelper Cloud Token Helper")
    print("=" * 50)
    print()

    email = input("Bambu Lab email: ").strip()
    password = getpass.getpass("Password: ")

    print("\nLogging in...")
    try:
        data = login(email, password)
    except requests.exceptions.HTTPError as e:
        print(f"Login failed: {e}")
        sys.exit(1)

    token = extract_token(data)

    # Check if 2FA is needed
    if not token:
        login_type = data.get("loginType", "")
        if login_type in ("verifyCode", "tfa") or not token:
            print("2FA verification required. Check your email for the code.")
            code = input("Verification code: ").strip()
            print("Verifying...")
            try:
                data = verify(email, code)
            except requests.exceptions.HTTPError as e:
                print(f"Verification failed: {e}")
                sys.exit(1)
            token = extract_token(data)

    if not token:
        print("Error: Could not get access token from response.")
        print(f"Response: {json.dumps(data, indent=2)[:500]}")
        sys.exit(1)

    print("\n" + "=" * 50)
    print("  SUCCESS - Your access token:")
    print("=" * 50)
    print(f"\n{token}\n")

    # Fetch devices
    print("Fetching your printers...")
    try:
        devices = fetch_devices(token)
        if devices:
            print(f"\nFound {len(devices)} printer(s):")
            for i, dev in enumerate(devices):
                print(f"  {i+1}. {dev.get('name', '?')} "
                      f"({dev.get('dev_product_name', '?')}) "
                      f"- Serial: {dev.get('dev_id', '?')}")
        else:
            print("No printers found on this account.")
    except Exception as e:
        print(f"Could not fetch printers: {e}")

    print("\n" + "-" * 50)
    print("Copy the token above and paste it into")
    print("BambuHelper's web interface (Paste Token field).")
    print("-" * 50)


if __name__ == "__main__":
    main()
