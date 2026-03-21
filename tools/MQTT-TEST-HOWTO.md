# Bambu MQTT Diagnostic Tool

Tests direct MQTT connection to your Bambu Lab printer in LAN mode.
Helps diagnose BambuHelper connection issues (State: UNKNOWN, no data, etc.)

## Setup (one-time)

1. Install Python from [python.org/downloads](https://www.python.org/downloads/) — during install, check **"Add Python to PATH"**
2. Open Command Prompt (or Terminal on Mac) and run:
   ```
   pip install paho-mqtt
   ```

## Run the test

1. Open `mqtt_test.py` in Notepad and edit these 3 lines at the top with your printer info:
   ```python
   PRINTER_IP   = "YOUR_PRINTER_IP"       # e.g. "192.168.1.100"
   ACCESS_CODE  = "YOUR_ACCESS_CODE"      # 8 chars from printer LCD
   SERIAL       = "YOUR_SERIAL_NUMBER"    # MUST be UPPERCASE
   ```

   Where to find these values:
   - **IP address**: Printer LCD → Settings → Network
   - **Access Code**: Printer LCD → Settings → LAN Only Mode
   - **Serial Number**: Printer LCD → Settings → Device → Serial Number

2. Save the file, then in Command Prompt navigate to the folder and run:
   ```
   python mqtt_test.py
   ```

3. After 30 seconds it will show a summary:
   ```
   DIAGNOSTIC SUMMARY
   [PASS/FAIL] Serial number format
   [PASS/FAIL] TCP reachable
   [PASS/FAIL] TLS handshake
   [PASS/FAIL] MQTT auth
   [PASS/FAIL] Messages received
   ```

## What the results mean

| Result | Meaning |
|--------|---------|
| **TCP FAIL** | Printer not reachable — check IP, same network, printer powered on |
| **TLS FAIL** | TLS handshake failed — firewall or printer firmware issue |
| **MQTT auth FAIL (rc=4/5)** | Wrong Access Code — re-check on printer LCD |
| **No messages** | Serial number mismatch — MQTT topics are case-sensitive, serial MUST be uppercase |
| **All PASS** | Printer works fine — issue is in BambuHelper config on the ESP |

## Sharing results

When reporting issues, please share:
- A screenshot of the full output (you can redact serial/access code)
- The `pushall_dump.json` file created in the same folder (if the test got data)
