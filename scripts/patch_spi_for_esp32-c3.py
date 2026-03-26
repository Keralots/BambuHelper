"""
Pre-build script that patches vendored libraries in .pio/libdeps/.
Runs after PlatformIO installs dependencies, before compilation.
Add new entries to PATCHES to fix future library bugs.
"""
Import("env")
import os

PATCHES = [
    {
        # TFT_eSPI bug on ESP32-C3: SPI_PORT is set to SPI2_HOST (=1, host enum),
        # but REG_SPI_BASE(i) only returns the correct SPI2 base address when i==2
        # (the bus number). With i=1 it returns 0, so _spi_user = 0x00000010,
        # causing a Store access fault on the first SPI write inside tft.init().
        # There's an open PR fixing this here: https://github.com/Bodmer/TFT_eSPI/pull/3792
        # But until it is merged we need to patch the library
        "file": os.path.join("TFT_eSPI", "Processors", "TFT_eSPI_ESP32_C3.h"),
        "search": "#define SPI_PORT SPI2_HOST",
        "replace": (
            "// Must use 2 (bus number), NOT SPI2_HOST (=1, host enum).\n"
            "// REG_SPI_BASE(i) on C3 only returns DR_REG_SPI2_BASE when i==2;\n"
            "// i==1 returns 0, making _spi_user=0x10 -> Store access fault.\n"
            "#define SPI_PORT 2"
        ),
    },
]

lib_dir = os.path.join(env.get("PROJECT_LIBDEPS_DIR"), env.get("PIOENV"))

for patch in PATCHES:
    path = os.path.join(lib_dir, patch["file"])
    if not os.path.isfile(path):
        print(f"patch_libs: WARNING: {patch['file']} not found, skipping")
        continue
    with open(path, "r") as f:
        content = f.read()
    if patch["search"] in content:
        content = content.replace(patch["search"], patch["replace"])
        with open(path, "w") as f:
            f.write(content)
        print(f"patch_libs: patched {patch['file']}")
    else:
        print(f"patch_libs: {patch['file']} already patched")
