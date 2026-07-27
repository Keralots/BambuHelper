# How to Send Diagnostics

If something in BambuHelper looks wrong (filament colors, AMS slots, temperatures,
anything), this short guide tells you how to capture a diagnostic dump and email
it for analysis.

## Windows users (recommended path)

**1.** Download [`BambuHelper-CompanionTool.exe`](BambuHelper-CompanionTool.exe)
   from this folder to your Desktop or any folder you can find.

   First time only: Windows may show "Windows protected your PC". Click **More info**,
   then **Run anyway**. This is normal for unsigned tools.

**2.** Double-click the exe. An app window opens - no terminal, no commands.

**3.** Go to the **Diagnostic** page in the left sidebar.

**4.** First pick your printer on the **Set up a device** page:

  - **Cloud** - sign in with your Bambu Lab email and password (plus a 2FA code
    if your account uses one), then choose your printer from the list
  - **LAN** - enter the printer's IP, Access Code, and Serial Number
    (all visible on the printer LCD under Settings)

**5.** Back on **Diagnostic**, click **Run diagnostic** and wait about 30
   seconds. The output appears live as it runs.

**6.** Click **Save pushall dump**, pick a folder you can find, and email the
   file to **keralots@gmail.com** with a short note about what looks wrong.

That's it.

> Prefer the old terminal version? It is still in the same exe - run
> `BambuHelper-CompanionTool.exe --cli` from a command prompt.

## Mac / Linux users (manual path)

No exe is published for these platforms. Use Python instead - you get the same
interface, in your browser.

```
pip install -U paho-mqtt curl_cffi pywebview
python tools/companion/bambu_companion.py
```

Then follow steps 3-6 from the Windows section above.

`pywebview` is optional. Without it the app opens in your default browser
instead of its own window; everything else is identical. For the terminal
version, run `python tools/bambu_diag.py`.

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
- **The window is blank or never appears:** on Windows 10 the app needs Microsoft's
  Edge WebView2 runtime. Install it, or run the exe with `--browser` to use your
  normal browser instead.
- **LAN mode "TCP fail":** printer and computer must be on the same WiFi/network, and
  LAN Only Mode must be enabled on the printer (Settings > LAN Only Mode).
- **Cloud login fails:** double-check email/password by logging into bambulab.com in a
  browser. If you use 2FA, have your authenticator app or email ready.

## What the Companion Tool also does

The **Set up a device** page configures your BambuHelper over WiFi without typing
serials and tokens by hand. It can scan your network to find the device, so you
don't need to read its IP off the screen. That page is for first-time setup,
not diagnostics.
