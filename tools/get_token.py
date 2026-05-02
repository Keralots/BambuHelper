#!/usr/bin/env python3
"""
BambuHelper Cloud Token Helper

Gets a Bambu Lab cloud access token for use with BambuHelper.
Paste the token into BambuHelper's web interface (Paste Token field).

Usage:
    python get_token.py

Requirements:
    pip install curl_cffi
"""

import getpass
import json
import sys

try:
    from curl_cffi import requests
    print("Using curl_cffi (browser TLS impersonation)")
except ImportError:
    print("Error: 'curl_cffi' package required for Cloudflare bypass.")
    print("Install with: pip install curl_cffi")
    sys.exit(1)

API_BASE = "https://api.bambulab.com"
LOGIN_URL = f"{API_BASE}/v1/user-service/user/login"
TFA_URL = "https://bambulab.com/api/sign-in/tfa"
DEVICES_URL = f"{API_BASE}/v1/iot-service/api/user/bind"

# Impersonate Chrome to bypass Cloudflare TLS fingerprinting
IMPERSONATE = "chrome"


def login(email: str, password: str) -> dict:
    """Login with email + password. Returns API response dict."""
    resp = requests.post(LOGIN_URL,
                         json={"account": email, "password": password},
                         impersonate=IMPERSONATE, timeout=15)
    resp.raise_for_status()
    return resp.json()


def verify_totp(tfa_code: str, tfa_key: str) -> str:
    """Submit TOTP authenticator code. Returns token from cookie."""
    resp = requests.post(TFA_URL,
                         json={"tfaKey": tfa_key, "tfaCode": tfa_code},
                         impersonate=IMPERSONATE, timeout=15)
    resp.raise_for_status()

    # Token comes in Set-Cookie header
    token = resp.cookies.get("token")
    if not token:
        # Fallback: check JSON body
        try:
            data = resp.json()
            token = data.get("accessToken") or data.get("token")
        except Exception:
            pass
    return token


def verify_email_code(email: str, code: str) -> dict:
    """Submit email verification code. Returns API response dict."""
    resp = requests.post(LOGIN_URL,
                         json={"account": email, "code": code},
                         impersonate=IMPERSONATE, timeout=15)
    resp.raise_for_status()
    return resp.json()


def fetch_devices(token: str) -> list:
    """Fetch printer list from cloud."""
    resp = requests.get(DEVICES_URL,
                        headers={"Authorization": f"Bearer {token}"},
                        impersonate=IMPERSONATE, timeout=15)
    resp.raise_for_status()
    data = resp.json()
    devices = data.get("data", [])
    if not devices and isinstance(data.get("devices"), list):
        devices = data["devices"]
    return devices


def fetch_profile(token: str) -> dict:
    """Fetch user profile from cloud."""
    url = f"{API_BASE}/v1/user-service/my/profile"
    resp = requests.get(url,
                        headers={"Authorization": f"Bearer {token}"},
                        impersonate=IMPERSONATE, timeout=15)
    resp.raise_for_status()
    return resp.json()


def extract_token(data: dict) -> str | None:
    """Extract accessToken from login/verify response."""
    token = data.get("accessToken")
    if not token and isinstance(data.get("data"), dict):
        token = data["data"].get("accessToken")
    return token if token else None


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
    except Exception as e:
        print(f"Login failed: {e}")
        sys.exit(1)

    token = extract_token(data)

    # Check if 2FA is needed
    if not token:
        login_type = data.get("loginType", "")
        tfa_key = data.get("tfaKey", "")

        if login_type == "tfa":
            # TOTP authenticator app
            print("TOTP 2FA required. Enter code from your authenticator app.")
            code = input("Authenticator code: ").strip()
            print("Verifying...")
            try:
                token = verify_totp(code, tfa_key)
            except Exception as e:
                print(f"Verification failed: {e}")
                sys.exit(1)
        elif login_type == "verifyCode":
            # Email verification code
            print("Email verification required. Check your email for the code.")
            code = input("Verification code: ").strip()
            print("Verifying...")
            try:
                data = verify_email_code(email, code)
            except Exception as e:
                print(f"Verification failed: {e}")
                sys.exit(1)
            token = extract_token(data)
        else:
            print(f"Unknown loginType: {login_type}")
            print(f"Response: {json.dumps(data, indent=2)[:500]}")
            sys.exit(1)

    if not token:
        print("Error: Could not get access token.")
        sys.exit(1)

    print("\n" + "=" * 50)
    print("  SUCCESS - Your access token:")
    print("=" * 50)
    print(f"\n{token}\n")

    # Fetch profile to get userId
    print("Fetching user profile...")
    try:
        profile = fetch_profile(token)
        # Try common uid field locations
        uid = (profile.get("uid")
               or (profile.get("data", {}) or {}).get("uid")
               or profile.get("userId")
               or (profile.get("data", {}) or {}).get("userId"))
        if uid:
            print(f"  Your userId: u_{uid}")
    except Exception as e:
        print(f"  Could not fetch profile: {e}")

    # Fetch devices
    print("\nFetching your printers...")
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
