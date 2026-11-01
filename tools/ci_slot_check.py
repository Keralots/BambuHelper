#!/usr/bin/env python3
"""CI helper: check one env's firmware.bin against its app partition.

Reuses the partition lookup from tools/release.py. A release-set board that does
not fit fails the job; any other env only gets a warning (some DIY envs are
over their slot and are not shipped).

Usage: python tools/ci_slot_check.py <env>
"""

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import release  # noqa: E402


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: ci_slot_check.py <env>")
    env = sys.argv[1]
    image = release.REPO_ROOT / ".pio" / "build" / env / "firmware.bin"
    if not image.is_file():
        sys.exit(f"::error::{image} not found")

    shipped = env in release.WEB_FLASHER_BOARDS
    size = image.stat().st_size
    csv_name = release._partition_csv_for(env)
    slot = release._app_slot_bytes(csv_name) if csv_name else None

    if slot is None:
        line = f"{env}: {size:,} B, partition table {csv_name!r} not found - not checked"
        print(f"::warning::{line}")
        status = 0
    else:
        free = slot - size
        line = f"{env}: {size:,} / {slot:,} B ({free:+,} B)"
        if free >= 0:
            print(line)
            status = 0
        elif shipped:
            print(f"::error::{line} - over its app slot, would brick on first OTA")
            status = 1
        else:
            print(f"::warning::{line} - over its app slot (not in the release set)")
            status = 0

    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as fh:
            fh.write(f"- `{line}`{' (release set)' if shipped else ''}\n")
    sys.exit(status)


if __name__ == "__main__":
    main()
