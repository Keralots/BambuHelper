"""Thread-safe state shared between the HTTP handlers and the worker threads.

The GUI is a set of short HTTP requests against long-running work: a 30-second
diagnostic, a subnet sweep, a login that pauses for a 2FA code. None of that
fits in one request/response, so the work runs on a worker thread and the
browser polls. This module is the only place those two sides meet, and every
public method takes the lock.

Log lines carry a monotonic sequence number so the UI can ask for "everything
after line N" instead of re-fetching the whole buffer each tick.
"""

import threading


class LogBuffer:
    """Append-only log with monotonic sequence numbers."""

    def __init__(self, limit=4000):
        self._lock = threading.Lock()
        self._lines = []
        self._limit = limit
        self._dropped = 0

    def append(self, text, level="info"):
        with self._lock:
            seq = self._dropped + len(self._lines)
            self._lines.append({"seq": seq, "text": text, "level": level})
            if len(self._lines) > self._limit:
                # A pushall dump on a 4-AMS printer is a few thousand lines.
                # Drop from the front rather than growing without bound; seq
                # stays monotonic so the UI's "since" cursor is unaffected.
                drop = len(self._lines) - self._limit
                del self._lines[:drop]
                self._dropped += drop
            return seq

    def since(self, cursor):
        """Lines with seq >= cursor, plus the next cursor value."""
        with self._lock:
            if not self._lines:
                return [], self._dropped
            first = self._lines[0]["seq"]
            start = max(0, cursor - first)
            out = self._lines[start:]
            nxt = out[-1]["seq"] + 1 if out else cursor
            return list(out), nxt

    def clear(self):
        with self._lock:
            self._lines = []
            self._dropped = 0

    def as_text(self):
        with self._lock:
            return "\n".join(line["text"] for line in self._lines)


class Job:
    """A single background task: running flag, log, result, cancel event."""

    def __init__(self, name):
        self.name = name
        self.log = LogBuffer()
        self._lock = threading.Lock()
        self._running = False
        self._result = None
        self._error = None
        self.stop = threading.Event()
        self._thread = None

    # ── status ─────────────────────────────────────────────────────────────
    @property
    def running(self):
        with self._lock:
            return self._running

    def snapshot(self, cursor=0):
        lines, nxt = self.log.since(cursor)
        with self._lock:
            return {
                "running": self._running,
                "result": self._result,
                "error": self._error,
                "lines": lines,
                "cursor": nxt,
            }

    # ── lifecycle ──────────────────────────────────────────────────────────
    def start(self, target):
        """Run `target(job)` on a worker thread. False if already running."""
        with self._lock:
            if self._running:
                return False
            self._running = True
            self._result = None
            self._error = None
        self.stop.clear()
        self.log.clear()

        def runner():
            try:
                result = target(self)
                with self._lock:
                    self._result = result
            except Exception as e:
                with self._lock:
                    self._error = str(e) or e.__class__.__name__
                self.log.append(str(e), "fail")
            finally:
                with self._lock:
                    self._running = False

        self._thread = threading.Thread(target=runner, daemon=True,
                                        name=f"{self.name}-worker")
        self._thread.start()
        return True

    def cancel(self):
        self.stop.set()

    def emit(self, text, level="info"):
        """The `log` callback handed to bambu_core."""
        for chunk in str(text).split("\n"):
            self.log.append(chunk, level)


class AppState:
    """Everything the GUI session holds.

    Credentials live here only for as long as the process runs, and only the
    pieces needed to finish the flow: the access token (to push to the device)
    and the resolved userId. The password is used for the login call and never
    stored - not on this object, not on disk.
    """

    def __init__(self):
        self._lock = threading.Lock()
        # Cloud login flow
        self._region = "us"
        self._tfa_key = ""
        self._email = ""
        self._token = ""
        self._user_id = ""
        self._devices = []
        # Selected printer (cloud or LAN), shaped for build_printer_form
        self._printer = None
        # Background jobs
        self.diag = Job("diag")
        self.scan = Job("scan")
        # Discovered BambuHelper devices
        self._found = []

    # ── cloud login ────────────────────────────────────────────────────────
    def begin_login(self, email, region):
        with self._lock:
            self._email = email
            self._region = region
            self._tfa_key = ""
            self._token = ""
            self._user_id = ""
            self._devices = []

    def set_tfa_key(self, key):
        with self._lock:
            self._tfa_key = key or ""

    def get_login_context(self):
        with self._lock:
            return {"email": self._email, "region": self._region,
                    "tfa_key": self._tfa_key}

    def set_session(self, token, user_id, devices):
        with self._lock:
            self._token = token
            self._user_id = user_id
            self._devices = list(devices)

    def get_devices(self):
        with self._lock:
            return list(self._devices)

    def get_session(self):
        with self._lock:
            return self._token, self._user_id, self._region

    def logged_in(self):
        with self._lock:
            return bool(self._token and self._user_id)

    # ── selected printer ───────────────────────────────────────────────────
    def set_printer(self, printer):
        with self._lock:
            self._printer = dict(printer) if printer else None

    def get_printer(self):
        with self._lock:
            return dict(self._printer) if self._printer else None

    # ── discovered devices ─────────────────────────────────────────────────
    def set_found(self, devices):
        with self._lock:
            self._found = list(devices)

    def get_found(self):
        with self._lock:
            return list(self._found)

    # ── teardown ───────────────────────────────────────────────────────────
    def forget_credentials(self):
        """Drop the token and printer list. Wired to the UI's Sign out."""
        with self._lock:
            self._token = ""
            self._user_id = ""
            self._devices = []
            self._tfa_key = ""
            self._printer = None
