@echo off
rem BrowserX — (c) 2026 codero-sus. All rights reserved.
rem Personal, non-commercial use only. No modification, no redistribution or resale.
rem Source-available, not open source — see LICENSE. https://the-codero.web.app
rem
rem BrowserX — build the browser on Windows.
rem
rem   tools\build_windows.cmd                  build out\BrowserX.exe
rem   tools\build_windows.cmd --dry-run        print every compiler command, run none
rem   tools\build_windows.cmd --stage          build and copy the CEF runtime next to the exe
rem   tools\build_windows.cmd --check-sources  prove it compiles the same files as make
rem   tools\build_windows.cmd --jobs 8         parallel compiles
rem
rem This is a launcher: all the work is tools\build_windows.py, so there is one build
rem description rather than a batch file that drifts away from the Python one. What the
rem batch file adds is finding Python the way Windows actually installs it - including the
rem Microsoft Store stub named python.exe that opens the Store instead of running.
rem
rem Before the first build:
rem   python tools\fetch_cef.py     (downloads the CEF Windows build, about 200 MB)
rem
rem Run this from any directory: it uses its own location to find the repository.

setlocal EnableExtensions EnableDelayedExpansion
set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "REPO_ROOT=%%~fI"

chcp 65001 >nul 2>nul
set "PYTHONIOENCODING=utf-8"
set "PYTHONUTF8=1"

rem Windows ships a Store stub named python.exe: `where python` finds it, and running it
rem opens the Microsoft Store rather than Python. Asking for a version is the only test
rem that tells a real interpreter from the stub.
set "PY_EXE="
set "PY_FLAG="
python -c "import sys" >nul 2>nul
if !ERRORLEVEL! EQU 0 set "PY_EXE=python"
if not defined PY_EXE (
  py -3 -c "import sys" >nul 2>nul
  if !ERRORLEVEL! EQU 0 (
    set "PY_EXE=py"
    set "PY_FLAG=-3"
  )
)
if not defined PY_EXE (
  echo.
  echo   Python 3 was not found. BrowserX's build needs it - it fetches CEF and runs
  echo   this build - and so does every other tool in tools\.
  echo.
  echo     winget install --id Python.Python.3.12
  echo.
  echo   Then open a new window ^(the PATH change only applies to new ones^).
  exit /b 1
)

echo.
echo   BrowserX — Windows build
!PY_EXE! !PY_FLAG! "%SCRIPT_DIR%build_windows.py" %*
set "RC=!ERRORLEVEL!"

if !RC! EQU 0 (
  echo   Built. Run it with:  out\BrowserX.exe
  echo   After pulling new commits:  tools\update.cmd
)
endlocal & exit /b %RC%
