"""Native window for the Companion GUI, with a plain-browser fallback.

pywebview owns the main thread; the HTTP server and every job run on daemon
threads behind it. Closing the window quits - this is a wizard, not a resident
service, so there is no tray and no hide-to-background.

When pywebview or its platform backend is unavailable the same local URL opens
in the default browser instead. Two situations need that path:
  * Windows 10 without the Edge WebView2 runtime (it ships with Windows 11).
  * Linux without the GTK/Qt bindings pywebview needs.
Both get the identical UI; only the frame around it differs.
"""

import sys
import threading
import time
import webbrowser

import server as srv
from app_state import AppState

try:
    import webview
    WEBVIEW_AVAILABLE = True
except Exception:
    webview = None
    WEBVIEW_AVAILABLE = False

_window = None
_stop = threading.Event()

WINDOW_TITLE = "BambuHelper Companion"
_FILE_TYPES = ("JSON files (*.json)", "Text files (*.txt)", "All files (*.*)")


class NativeBridge:
    """Native-only capabilities the web layer cannot do for itself.

    server.py holds one of these when a native window is up and calls into it
    for the Save dialog. In browser mode the bridge is None and the UI falls
    back to an ordinary download link.
    """

    def save_text(self, text, name):
        try:
            res = _window.create_file_dialog(
                webview.SAVE_DIALOG, save_filename=name, file_types=_FILE_TYPES)
            if not res:
                return {"ok": False, "cancelled": True}
            path = res if isinstance(res, str) else res[0]
            with open(path, "w", encoding="utf-8") as f:
                f.write(text or "")
            return {"ok": True, "path": path}
        except Exception as e:
            return {"ok": False, "error": str(e)}

    def open_url(self, url):
        try:
            if not str(url).startswith(("http://", "https://")):
                raise ValueError("Only web links can be opened.")
            return {"ok": bool(webbrowser.open(str(url)))}
        except Exception as e:
            return {"ok": False, "error": str(e)}

    def quit(self):
        _stop.set()
        if _window is not None:
            try:
                # Give the HTTP response that triggered this a moment to flush.
                time.sleep(0.2)
                _window.destroy()
            except Exception:
                pass


def run(force_browser=False):
    """Start the server and show the UI. Returns when the user quits."""
    global _window

    state = AppState()
    use_native = WEBVIEW_AVAILABLE and not force_browser
    bridge = NativeBridge() if use_native else None

    httpd, port = srv.make_server(state, bridge=bridge)
    srv.serve(httpd)
    url = f"http://127.0.0.1:{port}/"

    if not use_native:
        reason = ("--browser was requested" if force_browser
                  else "no native window backend is available")
        print(f"BambuHelper Companion - {reason}.")
        print(f"Open this address in your browser:  {url}")
        if not force_browser:
            print("(On Windows 10 the Edge WebView2 runtime gives you a real "
                  "app window instead.)")
        try:
            webbrowser.open(url)
        except Exception:
            pass
        print("\nLeave this window open while you use the app. Ctrl+C to quit.")
        try:
            while not _stop.wait(0.5):
                pass
        except KeyboardInterrupt:
            pass
        finally:
            _shutdown(httpd)
        return

    _window = webview.create_window(
        WINDOW_TITLE, url,
        width=1080, height=800, min_size=(820, 620))
    try:
        webview.start()
    finally:
        # webview.start() returns once the window is destroyed.
        _stop.set()
        _shutdown(httpd)


def _shutdown(httpd):
    state = getattr(httpd, "state", None)
    if state is not None:
        # Stop any in-flight scan/diagnostic so their threads don't outlive
        # the window and keep the process alive.
        state.diag.cancel()
        state.scan.cancel()
        state.forget_credentials()
    try:
        httpd.shutdown()
    except Exception:
        pass
    try:
        httpd.server_close()
    except Exception:
        pass
