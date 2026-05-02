#!/usr/bin/env python3
"""
Create BambuHelper release firmware files.

Usage:
    python merge_bins.py                    # auto-reads version, builds esp32s3
    python merge_bins.py --board cyd        # build CYD firmware
    python merge_bins.py --board esp32c3    # build ESP32-C3 firmware
    python merge_bins.py --board esp32s3_zero  # build Waveshare ESP32-S3-Zero firmware
    python merge_bins.py --board ws_lcd_200 # build Waveshare 2.0 firmware
    python merge_bins.py --board ws_lcd_154 # build Waveshare 1.54 firmware
    python merge_bins.py --all              # build all board variants
    python merge_bins.py v2.5               # override version
    python merge_bins.py --ota              # OTA binary only
    python merge_bins.py --full             # Full (WebFlasher) binary only

Output:
    firmware/v2.5/BambuHelper-esp32s3-v2.5-Full.bin
    firmware/v2.5/BambuHelper-esp32s3-v2.5-ota.bin
    firmware/v2.5/BambuHelper-esp32s3_zero-v2.5-Full.bin
    firmware/v2.5/BambuHelper-esp32s3_zero-v2.5-ota.bin
    firmware/v2.5/BambuHelper-cyd-v2.5-Full.bin
    firmware/v2.5/BambuHelper-cyd-v2.5-ota.bin
    firmware/v2.5/BambuHelper-ws_lcd_200-v2.5-Full.bin
    firmware/v2.5/BambuHelper-ws_lcd_200-v2.5-ota.bin
    firmware/v2.5/BambuHelper-ws_lcd_154-v2.5-Full.bin
    firmware/v2.5/BambuHelper-ws_lcd_154-v2.5-ota.bin
    firmware/v2.5/BambuHelper-esp32c3-v2.5-Full.bin
    firmware/v2.5/BambuHelper-esp32c3-v2.5-ota.bin
"""

import argparse
import os
import re
import sys

# Canonical board configurations. board_id must match BOARD_VARIANT used by OTA.
BOARDS = {
    'esp32s3': {
        'build_dir': '.pio/build/esp32s3',
        'bootloader_offset': 0x0,       # ESP32-S3 starts at 0x0
        'partitions_offset': 0x8000,
        'firmware_offset': 0x10000,
        'build_env': 'esp32s3',
        'board_id': 'esp32s3',
    },
    'esp32s3_zero': {
        'build_dir': '.pio/build/esp32s3_zero',
        'bootloader_offset': 0x0,       # ESP32-S3 starts at 0x0
        'partitions_offset': 0x8000,
        'firmware_offset': 0x10000,
        'build_env': 'esp32s3_zero',
        'board_id': 'esp32s3_zero',
    },
    'cyd': {
        'build_dir': '.pio/build/cyd',
        'bootloader_offset': 0x1000,    # Standard ESP32 starts at 0x1000
        'partitions_offset': 0x8000,
        'firmware_offset': 0x10000,
        'build_env': 'cyd',
        'board_id': 'cyd',
    },
    'ws_lcd_200': {
        'build_dir': '.pio/build/ws_lcd_200',
        'bootloader_offset': 0x0,       # ESP32-S3 starts at 0x0
        'partitions_offset': 0x8000,
        'firmware_offset': 0x10000,
        'build_env': 'ws_lcd_200',
        'board_id': 'ws_lcd_200',
    },
    'ws_lcd_154': {
        'build_dir': '.pio/build/ws_lcd_154',
        'bootloader_offset': 0x0,       # ESP32-S3 starts at 0x0
        'partitions_offset': 0x8000,
        'firmware_offset': 0x10000,
        'build_env': 'ws_lcd_154',
        'board_id': 'ws_lcd_154',
    },
    'esp32c3': {
        'build_dir': '.pio/build/esp32c3',
        'bootloader_offset': 0x0,       # ESP32-C3 starts at 0x0
        'partitions_offset': 0x8000,
        'firmware_offset': 0x10000,
        'build_env': 'esp32c3',
        'board_id': 'esp32c3',
    },
}

BOARD_ALIASES = {
    's3': 'esp32s3',
    'esp32s3': 'esp32s3',
    's3zero': 'esp32s3_zero',
    's3_zero': 'esp32s3_zero',
    'esp32s3_zero': 'esp32s3_zero',
    'cyd': 'cyd',
    'ws': 'ws_lcd_200',
    'ws_lcd_200': 'ws_lcd_200',
    'ws154': 'ws_lcd_154',
    'ws_lcd_154': 'ws_lcd_154',
    'c3': 'esp32c3',
    'esp32c3': 'esp32c3',
}

CONFIG_H = os.path.join('include', 'config.h')


def read_version_from_config():
    """Extract FW_VERSION from config.h."""
    if not os.path.exists(CONFIG_H):
        return None
    with open(CONFIG_H, 'r') as f:
        for line in f:
            m = re.match(r'#define\s+FW_VERSION\s+"([^"]+)"', line)
            if m:
                return m.group(1)
    return None


def get_paths(version, board):
    """Return output directory and file paths for a given version and board."""
    cfg = BOARDS[board]
    out_dir = os.path.join('firmware', version)
    board_id = cfg['board_id']
    full = os.path.join(out_dir, f'BambuHelper-{board_id}-{version}-Full.bin')
    ota = os.path.join(out_dir, f'BambuHelper-{board_id}-{version}-ota.bin')
    return out_dir, full, ota


def resolve_board(board):
    """Map CLI alias to canonical board id used by BOARD_VARIANT and release assets."""
    return BOARD_ALIASES[board]


def build_outputs(version, board, ota_only=False, full_only=False):
    """Build requested output files for one board."""
    out_dir, merged_path, ota_path = get_paths(version, board)
    print(f"Version: {version}  Board: {board}\n")

    if ota_only:
        return create_ota_binary(out_dir, ota_path, board)
    if full_only:
        return merge_binaries(out_dir, merged_path, board)

    s1 = merge_binaries(out_dir, merged_path, board)
    s2 = create_ota_binary(out_dir, ota_path, board)
    success = s1 and s2

    if success:
        board_id = BOARDS[board]['board_id']
        print(f"\n{'='*60}")
        print(f"Release {version} ({board_id}) ready in {out_dir}/")
        print(f"{'='*60}")
        print(f"\nGitHub Release - attach both files:")
        print(f"  BambuHelper-{board_id}-{version}-Full.bin  - new users (ESP Web Flasher)")
        print(f"  BambuHelper-{board_id}-{version}-ota.bin   - existing users (Web UI update)")

    return success


def create_ota_binary(out_dir, out_path, board):
    """Copy firmware.bin as OTA update file."""
    cfg = BOARDS[board]
    firmware = os.path.join(cfg['build_dir'], 'firmware.bin')
    if not os.path.exists(firmware):
        print(f"Error: {firmware} not found. Run 'pio run -e {cfg['build_env']}' first.")
        return False

    os.makedirs(out_dir, exist_ok=True)

    with open(firmware, 'rb') as src, open(out_path, 'wb') as dst:
        dst.write(src.read())

    size = os.path.getsize(out_path)
    print(f"  OTA:  {out_path} ({size / 1024:.1f} KB)")
    return True


def merge_binaries(out_dir, out_path, board):
    """Merge bootloader + partitions + boot_app0 + firmware into a single flashable binary."""
    cfg = BOARDS[board]
    build_dir = cfg['build_dir']
    bootloader = os.path.join(build_dir, 'bootloader.bin')
    partitions = os.path.join(build_dir, 'partitions.bin')
    firmware = os.path.join(build_dir, 'firmware.bin')

    # boot_app0.bin initialises the OTA-data partition so the bootloader
    # knows which app slot to run.  PlatformIO flashes it automatically
    # during USB upload, but the merged Full binary must include it too.
    boot_app0 = None
    for candidate in [
        os.path.join(build_dir, 'boot_app0.bin'),
        os.path.expanduser(
            '~/.platformio/packages/framework-arduinoespressif32/'
            'tools/partitions/boot_app0.bin'),
        os.path.join(
            'C:/', '.platformio', 'packages',
            'framework-arduinoespressif32',
            'tools', 'partitions', 'boot_app0.bin'),
    ]:
        if os.path.exists(candidate):
            boot_app0 = candidate
            break
    if boot_app0 is None:
        print("Error: boot_app0.bin not found.")
        return False

    for path in [bootloader, partitions, firmware]:
        if not os.path.exists(path):
            print(f"Error: {path} not found. Run 'pio run -e {cfg['build_env']}' first.")
            return False

    os.makedirs(out_dir, exist_ok=True)

    bl_offset = cfg['bootloader_offset']
    pt_offset = cfg['partitions_offset']
    otadata_offset = 0xE000
    fw_offset = cfg['firmware_offset']

    with open(out_path, 'wb') as out:
        # Pad to bootloader offset
        if bl_offset > 0:
            out.write(b'\xFF' * bl_offset)

        with open(bootloader, 'rb') as f:
            bl = f.read()
            out.write(bl)

        # Pad to partitions offset
        current = bl_offset + len(bl)
        out.write(b'\xFF' * (pt_offset - current))

        with open(partitions, 'rb') as f:
            pt = f.read()
            out.write(pt)

        # Pad to OTA data offset and write boot_app0.bin
        current = pt_offset + len(pt)
        out.write(b'\xFF' * (otadata_offset - current))

        with open(boot_app0, 'rb') as f:
            ota = f.read()
            out.write(ota)

        # Pad to firmware offset
        current = otadata_offset + len(ota)
        out.write(b'\xFF' * (fw_offset - current))

        with open(firmware, 'rb') as f:
            fw = f.read()
            out.write(fw)

    total = os.path.getsize(out_path)
    print(f"  Full: {out_path} ({total / 1024:.1f} KB)")
    return True


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Create BambuHelper release firmware')
    parser.add_argument('version', nargs='?', default=None, help='Version (default: read from config.h)')
    parser.add_argument('--board', choices=list(BOARD_ALIASES.keys()), default='esp32s3',
                        help='Board target (aliases supported: s3, c3, ws)')
    parser.add_argument('--all', action='store_true', help='Generate binaries for all supported boards')
    parser.add_argument('--ota', action='store_true', help='OTA binary only')
    parser.add_argument('--full', action='store_true', help='Full (WebFlasher) binary only')
    args = parser.parse_args()

    version = args.version or read_version_from_config()
    if not version:
        print("Error: could not read FW_VERSION from config.h. Pass version as argument.")
        sys.exit(1)

    if args.all:
        success = True
        for idx, board in enumerate(BOARDS.keys()):
            if idx > 0:
                print(f"\n{'-'*60}\n")
            success = build_outputs(version, board, ota_only=args.ota, full_only=args.full) and success
    else:
        board = resolve_board(args.board)
        success = build_outputs(version, board, ota_only=args.ota, full_only=args.full)

    sys.exit(0 if success else 1)
