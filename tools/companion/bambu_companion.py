#!/usr/bin/env python3
"""
BambuHelper Companion Tool - entry point.

Default is the graphical wizard. The terminal wizard is still there behind
--cli, so the diagnostics guide and any existing instructions keep working.

    python bambu_companion.py                # GUI (native window, or browser)
    python bambu_companion.py --browser      # GUI, forced into the browser
    python bambu_companion.py --cli          # the old terminal wizard

Requirements:
    pip install paho-mqtt              # diagnostic mode
    pip install -U curl_cffi           # cloud login (Cloudflare bypass);
                                       # keep it updated, Cloudflare retires old
                                       # browser fingerprints over time
    pip install pywebview              # optional: native window instead of a
                                       # browser tab
"""

import atexit
import os
import sys

# Frozen onefile builds unpack next to this module; a plain checkout runs from
# the source folder. Either way the sibling modules have to be importable.
sys.path.insert(0, getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__))))

USAGE = __doc__


def _pause_on_exit():
    """Keep the console up after a double-clicked run finishes.

    Only for the terminal frontend of a frozen build: the GUI has no console
    to hold open, and a source checkout is already being run from one.
    """
    try:
        input("\nPress Enter to exit...")
    except (EOFError, KeyboardInterrupt):
        pass


def _attach_console():
    """Give --cli a console inside a --windowed build.

    The exe is built windowed so the GUI does not flash a black console box
    behind it, which also means a frozen run starts with no stdio at all -
    input() would raise before asking anything. Allocate a console on demand
    and rebind the standard streams to it. Windows-only; every other platform
    runs this tool from source, where the console is already there.
    """
    if os.name != "nt" or not getattr(sys, "frozen", False):
        return
    try:
        import ctypes
        kernel32 = ctypes.windll.kernel32
        if kernel32.GetConsoleWindow() == 0 and not kernel32.AllocConsole():
            return
        sys.stdout = open("CONOUT$", "w", encoding="utf-8", buffering=1)
        sys.stderr = open("CONOUT$", "w", encoding="utf-8", buffering=1)
        sys.stdin = open("CONIN$", "r", encoding="utf-8")
    except Exception:
        pass


def main(argv):
    args = set(argv[1:])
    if args & {"-h", "--help", "/?"}:
        # Needs the same console the CLI does - a windowed build has none, so
        # without this --help would print into the void.
        _attach_console()
        print(USAGE)
        if getattr(sys, "frozen", False):
            _pause_on_exit()
        return 0

    if "--cli" in args:
        _attach_console()
        import cli
        if getattr(sys, "frozen", False):
            atexit.register(_pause_on_exit)
        try:
            cli.main()
        except KeyboardInterrupt:
            print("\n\nAborted by user.")
            return 130
        return 0

    import app_window
    try:
        app_window.run(force_browser="--browser" in args)
    except KeyboardInterrupt:
        return 130
    except Exception as e:
        # A GUI failure with no console is a silent no-op from the user's side.
        # Say what happened and point at the frontend that does not need a
        # window backend at all.
        message = (f"BambuHelper Companion could not start the interface:\n\n{e}\n\n"
                   f"Try running it with --cli for the terminal version.")
        print(message, file=sys.stderr)
        try:
            import tkinter
            from tkinter import messagebox
            root = tkinter.Tk()
            root.withdraw()
            messagebox.showerror("BambuHelper Companion", message)
            root.destroy()
        except Exception:
            pass
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
