# How to Send Diagnostics

If something in BambuHelper looks wrong (filament colors, AMS slots, temperatures,
anything), this short guide tells you how to capture a diagnostic dump and email
it for analysis.

## Windows users (recommended path)

**1.** Download [`BambuHelper-CompanionTool.exe`](BambuHelper-CompanionTool.exe)
   from this folder to your Desktop or any folder you can find.

   First time only: Windows may show "Windows protected your PC". Click **More info**,
   then **Run anyway**. This is normal for unsigned tools.

**2.** Double-click the exe. A terminal window opens.

**3.** When asked **"What would you like to do?"** pick **option 2 - Run printer diagnostic**.

**4.** Pick **LAN** or **Cloud** (whichever your BambuHelper uses) and follow the prompts:

  - **LAN mode** needs your printer IP, Access Code, and Serial Number
    (all visible on the printer LCD under Settings)
  - **Cloud mode** needs your Bambu Lab email + password, plus a 2FA code if your account uses it

**5.** Wait about 30 seconds for the test to finish.

**6.** Email the file `pushall_dump.json` (created next to the exe) to
   **keralots@gmail.com**, with a short note about what looks wrong.

That's it.

## Mac / Linux users (manual path)

Windows exe is not available on these platforms. You'll need Python instead.

```
pip install -U paho-mqtt curl_cffi
python tools/bambu_diag.py
```

Then follow steps 3-6 from the Windows section above.

## Cloud login fails with "HTTP 403"

If it says `CSRF error`, you are on an old build. Bambu added a CSRF check to
the 2FA step in mid-2026 and it broke every version before that; current
versions handle it. Update the tool.

Otherwise it is Cloudflare, which fronts Bambu's login servers and turns away
anything that doesn't look like a real browser. The tool imitates one, but
Cloudflare retires those signatures over time, so an old `curl_cffi` eventually
stops working:

```
pip install -U curl_cffi
```

On the Windows exe that means grabbing the current
`BambuHelper-CompanionTool.exe` - the library is baked into the exe, so
updating it locally does nothing.

Still blocked? Use LAN mode instead, or configure the device by hand with a
token from the Bambu Handy app.

## Privacy note

- Your password is never saved or sent anywhere except to Bambu Lab's own login server.
- `pushall_dump.json` contains your printer's serial number and current state. It does
  **not** contain your password or cloud token. You can open it in any text editor to
  inspect it before sending.

## Troubleshooting

- **"Windows protected your PC":** click **More info** then **Run anyway**. The exe is
  unsigned because code signing certificates cost money; this is expected.
- **Antivirus blocks the exe:** add an exception, or use the Mac/Linux Python path above
  on a different machine.
- **LAN mode "TCP fail":** printer and computer must be on the same WiFi/network, and
  LAN Only Mode must be enabled on the printer (Settings > LAN Only Mode).
- **Cloud login fails:** double-check email/password by logging into bambulab.com in a
  browser. If you use 2FA, have your authenticator app or email ready.

## What the Companion Tool also does

The same exe has a second mode (**option 1 - Configure BambuHelper device**) that
configures your BambuHelper over WiFi without typing serials and tokens by hand.
That mode is for first-time setup, not diagnostics.
