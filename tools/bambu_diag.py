#!/usr/bin/env python3
"""
BambuHelper Companion Tool - terminal wizard (compatibility entry point).

The tool grew a graphical frontend and moved into tools/companion/. This file
stays because DIAGNOSTICS-HOWTO.md, older forum posts and existing bug reports
all tell people to run `python tools/bambu_diag.py`, and that should keep
working. It forwards straight to the terminal wizard.

    python tools/bambu_diag.py                  # terminal wizard (as before)
    python tools/companion/bambu_companion.py    # graphical wizard

Requirements:
    pip install paho-mqtt              # always required
    pip install -U curl_cffi           # only for Cloud mode (Cloudflare bypass);
                                       # keep it updated, Cloudflare retires old
                                       # browser fingerprints over time
"""

import atexit
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "companion"))


def _pause_on_exit():
    # Keep the console open after a double-clicked run finishes.
    if getattr(sys, "frozen", False):
        try:
            input("\nPress Enter to exit...")
        except (EOFError, KeyboardInterrupt):
            pass


atexit.register(_pause_on_exit)

if __name__ == "__main__":
    try:
        import cli
    except ImportError as e:
        sys.exit(f"error: could not load tools/companion/ - {e}")
    try:
        cli.main()
    except KeyboardInterrupt:
        print("\n\nAborted by user.")
        sys.exit(130)
