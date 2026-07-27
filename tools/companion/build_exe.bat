@echo off
REM ---------------------------------------------------------------------------
REM  Build the standalone BambuHelper Companion Tool with PyInstaller.
REM
REM  One-time setup (installs runtime deps + PyInstaller):
REM    python -m pip install -r requirements.txt
REM    python -m pip install pyinstaller
REM
REM  Then double-click this file, or run it from a terminal in this folder.
REM  Output lands in:
REM    dist\BambuHelper-CompanionTool.exe
REM  and is copied over tools\BambuHelper-CompanionTool.exe, which is the one
REM  the diagnostics guide links to.
REM
REM  Notes:
REM   * --windowed -> no console box behind the GUI. The --cli frontend
REM     allocates its own console at runtime (see _attach_console), so the
REM     terminal wizard still works from this same exe.
REM   * --add-data "webui;webui" bundles the web UI. At runtime
REM     server.webui_dir() resolves it from sys._MEIPASS.
REM   * --collect-all webview / pythonnet pulls in the pywebview EdgeChromium
REM     (WebView2) backend, which PyInstaller can't see (loaded via clr).
REM   * --collect-all curl_cffi is required: the impersonation targets ship as
REM     data files and a bundled libcurl DLL that a plain import scan misses.
REM   * WebView2 runtime is required for the native window (ships with
REM     Windows 11; Windows 10 falls back to the default browser).
REM ---------------------------------------------------------------------------

setlocal
cd /d "%~dp0"

REM PyInstaller's wrapper exe is often on a per-user Scripts dir that's not on
REM PATH. Invoke it as a module instead. Try "python" first, then the "py" launcher.
set "PYI="
python -m PyInstaller --version >nul 2>&1 && set "PYI=python -m PyInstaller"
if not defined PYI py -m PyInstaller --version >nul 2>&1 && set "PYI=py -m PyInstaller"
if not defined PYI (
  echo ERROR: PyInstaller is not importable from "python" or "py".
  echo Install build dependencies first:
  echo     python -m pip install -r requirements.txt
  echo     python -m pip install pyinstaller
  pause
  exit /b 1
)
echo Using: %PYI%

REM Clean previous build artifacts so we don't ship stale bundles.
if exist build rmdir /s /q build
if exist dist  rmdir /s /q dist

%PYI% ^
  --onefile ^
  --windowed ^
  --name BambuHelper-CompanionTool ^
  --add-data "webui;webui" ^
  --hidden-import server ^
  --hidden-import app_window ^
  --hidden-import app_state ^
  --hidden-import bambu_core ^
  --hidden-import cli ^
  --collect-all webview ^
  --collect-all pythonnet ^
  --collect-all curl_cffi ^
  --hidden-import clr ^
  --hidden-import paho.mqtt.client ^
  bambu_companion.py

if errorlevel 1 (
  echo.
  echo BUILD FAILED.
  pause
  exit /b 1
)

echo.
echo Copying to tools\ (the path DIAGNOSTICS-HOWTO.md links to)...
copy /y "dist\BambuHelper-CompanionTool.exe" "..\BambuHelper-CompanionTool.exe" >nul
if errorlevel 1 (
  echo WARNING: could not copy into tools\ - do it by hand.
)

echo.
echo ---------------------------------------------------------------
echo  BUILD OK
echo  Output: %CD%\dist\BambuHelper-CompanionTool.exe
echo ---------------------------------------------------------------
echo.
echo Test it by double-clicking the .exe in dist\ .
pause
