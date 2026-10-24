"""Apply after the framework has selected its compiler and linker settings."""
Import("env")  # noqa: F821 - injected by PlatformIO/SCons

env.Replace(AR=env.subst("$AR").replace("-ar", "-gcc-ar"))
env.Append(LINKFLAGS=["-flto", "-fuse-linker-plugin"])

# LovyanGFX deliberately uses include-order-dependent extension base classes.
# Keep its original ABI boundary instead of letting LTO merge those types.
for library in env.GetLibBuilders():
    if library.name == "LovyanGFX":
        library.env.ProcessUnFlags("-flto")
