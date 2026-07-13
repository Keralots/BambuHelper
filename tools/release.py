#!/usr/bin/env python3
"""
End-to-end release builder for BambuHelper.

Runs the full release pipeline for the web-flasher boards:
    1. Reads FW_VERSION from include/config.h
    2. Locates pio.exe (PATH first, then ~/.platformio/penv/Scripts/pio.exe)
    3. Builds every WEB_FLASHER_BOARDS env in one PlatformIO invocation
    4. Runs merge_bins.py for each board to generate Full.bin + ota.bin
    5. Copies only the Full.bin files into docs/firmware/latest/ (web flasher
       binaries) - ota.bin stays in firmware/v<ver>/ for the GitHub Release
    6. Writes docs/firmware/latest/VERSION

Old BambuHelper-*-Full.bin files in docs/firmware/latest/ are left in place;
clean them up manually with `git rm` when they're no longer needed.

Usage:
    python tools/release.py
    python tools/release.py --skip-build       # assume .pio/build/<env> is current
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# Boards published by the web flasher. Order matches docs/flasher.js
# (DIY builds first, then all-in-one boards, CYD-style at the end).
WEB_FLASHER_BOARDS = [
    "esp32s3",
    "esp32s3_round",
    "esp32s3_zero",
    "esp32s3_zero_320",
    "esp32c3",
    "esp32c3_round",
    "ws_lcd_200",
    "ws_lcd_154",
    "ws_lcd_280",
    "es3n28p",
    "sc05_x",
    "ws_lcd_350",
    "wt32_sc01_plus",
    "jc3248w535",
    "cyd",
    "tzt_2432",
]

REPO_ROOT = Path(__file__).resolve().parent.parent
CONFIG_H = REPO_ROOT / "include" / "config.h"
MERGE_BINS = REPO_ROOT / "merge_bins.py"
DOCS_LATEST = REPO_ROOT / "docs" / "firmware" / "latest"


def read_version() -> str:
    """Extract FW_VERSION from include/config.h."""
    if not CONFIG_H.exists():
        sys.exit(f"error: {CONFIG_H} not found")
    pat = re.compile(r'#define\s+FW_VERSION\s+"([^"]+)"')
    for line in CONFIG_H.read_text().splitlines():
        m = pat.match(line)
        if m:
            return m.group(1)
    sys.exit("error: FW_VERSION not found in include/config.h")


def locate_pio() -> str:
    """Locate the PlatformIO CLI executable."""
    for name in ("pio", "pio.exe"):
        found = shutil.which(name)
        if found:
            return found
    fallback = Path.home() / ".platformio" / "penv" / "Scripts" / "pio.exe"
    if fallback.exists():
        return str(fallback)
    # POSIX fallback for completeness
    posix_fallback = Path.home() / ".platformio" / "penv" / "bin" / "pio"
    if posix_fallback.exists():
        return str(posix_fallback)
    sys.exit(
        "error: pio executable not found.\n"
        "  Tried PATH (pio / pio.exe) and "
        f"{fallback} / {posix_fallback}.\n"
        "  Install PlatformIO Core or add it to PATH."
    )


def run(cmd, cwd=REPO_ROOT):
    """Run a subprocess, exit on failure."""
    print(f"\n$ {' '.join(str(c) for c in cmd)}")
    result = subprocess.run(cmd, cwd=cwd)
    if result.returncode != 0:
        sys.exit(f"error: command failed with exit code {result.returncode}")


def build_envs(pio_path: str):
    """Build every WEB_FLASHER_BOARDS env in a single PlatformIO invocation."""
    cmd = [pio_path, "run"]
    for board in WEB_FLASHER_BOARDS:
        cmd.extend(["-e", board])
    run(cmd)


def merge_for_board(board: str):
    """Run merge_bins.py for one board without --full (produces both Full + ota)."""
    run([sys.executable, str(MERGE_BINS), "--board", board])


def copy_full_to_docs(version: str):
    """Copy *-Full.bin for each board from firmware/v<ver>/ to docs/firmware/latest/."""
    src_dir = REPO_ROOT / "firmware" / version
    if not src_dir.exists():
        sys.exit(f"error: {src_dir} not found - did merge_bins run?")

    DOCS_LATEST.mkdir(parents=True, exist_ok=True)

    copied = []
    for board in WEB_FLASHER_BOARDS:
        src = src_dir / f"BambuHelper-{board}-{version}-Full.bin"
        if not src.exists():
            sys.exit(f"error: missing {src}")
        dst = DOCS_LATEST / src.name
        shutil.copy2(src, dst)
        copied.append(dst.name)
    return copied


def write_version_file(version: str):
    (DOCS_LATEST / "VERSION").write_text(version + "\n", encoding="utf-8")


def find_old_full_bins(version: str):
    """List Full.bin files in docs/firmware/latest/ that are not for the current version."""
    if not DOCS_LATEST.exists():
        return []
    pat = re.compile(r"^BambuHelper-(.+)-(v[^-]+)-Full\.bin$")
    old = []
    for f in DOCS_LATEST.iterdir():
        m = pat.match(f.name)
        if m and m.group(2) != version:
            old.append(f.name)
    return sorted(old)


def list_ota_assets(version: str):
    """List the ota.bin files ready for `gh release create`."""
    src_dir = REPO_ROOT / "firmware" / version
    if not src_dir.exists():
        return []
    return sorted(
        f.name for f in src_dir.iterdir()
        if f.name.endswith("-ota.bin")
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--skip-build", action="store_true",
                        help="Skip the PlatformIO build step (assume .pio/build is up to date)")
    args = parser.parse_args()

    version = read_version()
    print(f"BambuHelper release: {version}")
    print(f"Boards: {', '.join(WEB_FLASHER_BOARDS)}")

    if not args.skip_build:
        pio = locate_pio()
        print(f"PlatformIO: {pio}")
        build_envs(pio)
    else:
        print("Skipping build (--skip-build)")

    print("\n--- Merging binaries ---")
    for board in WEB_FLASHER_BOARDS:
        merge_for_board(board)

    print("\n--- Copying Full.bin to docs/firmware/latest/ ---")
    copied = copy_full_to_docs(version)
    write_version_file(version)

    print("\n" + "=" * 60)
    print(f"Release {version} ready.")
    print("=" * 60)

    print("\nCopied to docs/firmware/latest/ (for web flasher):")
    for name in copied:
        print(f"  {name}")
    print(f"  VERSION  ({version})")

    ota_assets = list_ota_assets(version)
    if ota_assets:
        print(f"\nOTA assets ready in firmware/{version}/ (for `gh release create`):")
        for name in ota_assets:
            print(f"  {name}")

    old = find_old_full_bins(version)
    if old:
        print(f"\nOlder Full.bin files still in docs/firmware/latest/ "
              f"(remove with `git rm` when no longer needed):")
        for name in old:
            print(f"  {name}")

    print("\nNext steps:")
    print("  git add docs/firmware/latest/ .gitignore include/config.h")
    print(f'  git commit -m "release: {version}"')
    print(f"  gh release create {version} firmware/{version}/*.bin --notes \"...\"")
    print("  git push")


if __name__ == "__main__":
    main()
