#!/usr/bin/env python3
"""
End-to-end release builder for BambuHelper.

Runs the full release pipeline for the web-flasher boards:
    1. Reads FW_VERSION from include/config.h
    2. Locates pio.exe (PATH first, then ~/.platformio/penv/Scripts/pio.exe)
    3. Builds every WEB_FLASHER_BOARDS env in one PlatformIO invocation
    4. Checks each firmware.bin against its own app partition and refuses to go
       further if one does not fit - the build summary does NOT answer this
    5. Runs merge_bins.py for each board to generate Full.bin + ota.bin
    6. Copies only the Full.bin files into docs/firmware/latest/ (web flasher
       binaries) - ota.bin stays in firmware/v<ver>/ for the GitHub Release
    7. Writes docs/firmware/latest/VERSION
    8. Refreshes docs/errors/hms_en.json from Bambu's error-text feed - the
       portal page reads it from GitHub Pages for codes the device carries no
       table for

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


def _partition_csv_for(board: str):
    """Which partition table an env builds against.

    An explicit `board_build.partitions` under `[env:<board>]` wins; otherwise
    the `[env]` default in platformio.ini applies. Both live across
    platformio.ini and the boards/*.ini files pulled in by extra_configs.
    """
    default = None
    per_env = {}
    inis = [REPO_ROOT / "platformio.ini"] + sorted((REPO_ROOT / "boards").glob("*.ini"))
    for ini in inis:
        section = None
        for line in ini.read_text(encoding="utf-8").splitlines():
            stripped = line.strip()
            m = re.match(r"^\[env:?([^\]]*)\]$", stripped)
            if m:
                section = m.group(1) or None
                continue
            m = re.match(r"^board_build\.partitions\s*=\s*(\S+)", stripped)
            if not m:
                continue
            if section:
                per_env[section] = m.group(1)
            elif ini.name == "platformio.ini":
                default = m.group(1)
    return per_env.get(board, default)


def _app_slot_bytes(csv_name: str):
    """Size of the ota_0/factory app partition, or None if the table is not found.

    Tables live either in the repo or, for Arduino's stock ones like
    min_spiffs.csv, inside the framework package - which may sit under a custom
    packages dir, so search the obvious roots rather than assuming one.
    """
    candidates = [REPO_ROOT / csv_name]
    roots = [os.environ.get("PLATFORMIO_PACKAGES_DIR"),
             os.environ.get("PLATFORMIO_CORE_DIR"),
             str(Path.home() / ".platformio")]
    for root in filter(None, roots):
        candidates.extend(Path(root).glob("**/framework-arduinoespressif32*/tools/partitions/" + csv_name))
    for path in candidates:
        if not Path(path).is_file():
            continue
        for line in Path(path).read_text(encoding="utf-8").splitlines():
            if line.lstrip().startswith("#"):
                continue
            cols = [c.strip() for c in line.split(",")]
            if len(cols) < 5 or cols[1] != "app":
                continue
            if cols[2] not in ("ota_0", "factory"):
                continue
            size = cols[4]
            if size.lower().startswith("0x"):
                return int(size, 16)
            if size and size[-1] in "kKmM":
                return int(size[:-1]) * (1024 if size[-1] in "kK" else 1024 * 1024)
            return int(size)
    return None


def check_ota_slots():
    """Refuse to publish an app image that does not fit its own partition.

    The build summary cannot answer this. PlatformIO prints the linker section
    sum; what OTA uploads and what Full.bin embeds is firmware.bin, which
    carries segment padding on top - about 88 KB more on the C3. v3.8.0 shipped
    an esp32c3 image 7,408 bytes past its 1,835,008 byte slot on the strength of
    a build summary claiming 78 KB spare. Such a board boots from a fresh flash,
    because the bootloader loads segments without checking partition bounds, and
    then dies on its first OTA when app1 is written over the tail of app0.
    """
    print("\n--- Checking images against their app partitions ---")
    over, unknown = [], []
    for board in WEB_FLASHER_BOARDS:
        image = REPO_ROOT / ".pio" / "build" / board / "firmware.bin"
        if not image.is_file():
            sys.exit(f"error: {image} not found - build first, or drop --skip-build")
        csv_name = _partition_csv_for(board)
        slot = _app_slot_bytes(csv_name) if csv_name else None
        size = image.stat().st_size
        if slot is None:
            unknown.append((board, csv_name))
            print(f"  {board:<20} {size:>9,}  (partition table {csv_name!r} not found - NOT CHECKED)")
            continue
        free = slot - size
        print(f"  {board:<20} {size:>9,} / {slot:>9,}  {free:>+9,}"
              + ("   *** OVER ***" if free < 0 else ""))
        if free < 0:
            over.append((board, size, slot))
    if unknown:
        print("\nWARNING: could not verify " + ", ".join(b for b, _ in unknown))
    if over:
        print()
        for board, size, slot in over:
            print(f"error: {board} image is {size:,} B but its app partition is only {slot:,} B "
                  f"({size - slot:,} B over)")
        sys.exit("Refusing to publish - such a build bricks the board on its first OTA.")


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


def refresh_error_mirror():
    """Pull Bambu's error-text feed and rewrite the portal's mirror.

    Only the mirror. The embedded tables are source files, and regenerating them
    here would change what the firmware we just built contains - refresh those
    deliberately with a plain `python tools/gen_error_tables.py`, rebuild, and
    commit, rather than as a side effect of cutting a release. A network failure
    is not fatal: the previously published mirror stays up and merely goes stale.
    """
    script = REPO_ROOT / "tools" / "gen_error_tables.py"
    cmd = [sys.executable, str(script), "--mirror-only"]
    print(f"\n$ {' '.join(cmd)}")
    # Deliberately not run() - that exits the whole release on a non-zero code,
    # and a feed that is briefly unreachable must not sink a build that is
    # otherwise finished.
    try:
        rc = subprocess.run(cmd, cwd=REPO_ROOT).returncode
    except OSError as exc:
        rc, note = 1, str(exc)
    else:
        note = f"exit code {rc}"
    if rc != 0:
        print(f"  WARNING: mirror refresh failed ({note}); keeping the published copy")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--skip-build", action="store_true",
                        help="Skip the PlatformIO build step (assume .pio/build is up to date)")
    parser.add_argument("--skip-error-mirror", action="store_true",
                        help="Leave docs/errors/hms_en.json alone (offline builds)")
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

    # Before anything is merged or copied, so a bad build cannot reach
    # docs/firmware/latest/ or a release asset.
    check_ota_slots()

    print("\n--- Merging binaries ---")
    for board in WEB_FLASHER_BOARDS:
        merge_for_board(board)

    print("\n--- Copying Full.bin to docs/firmware/latest/ ---")
    copied = copy_full_to_docs(version)
    write_version_file(version)

    if not args.skip_error_mirror:
        print("\n--- Refreshing docs/errors/hms_en.json ---")
        refresh_error_mirror()

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
    print("  git add docs/firmware/latest/ docs/errors/ .gitignore include/config.h")
    print(f'  git commit -m "release: {version}"')
    print(f"  gh release create {version} firmware/{version}/*.bin --notes \"...\"")
    print("  git push")


if __name__ == "__main__":
    main()
