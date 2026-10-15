@echo off
rem BrowserX — (c) 2026 codero-sus. All rights reserved.
rem Personal, non-commercial use only. No modification, no redistribution or resale.
rem Source-available, not open source — see LICENSE. https://the-codero.web.app
rem
rem BrowserX — the Windows updater.
rem
rem   update.cmd                  pull, rebuild, verify
rem   update.cmd --check          is there anything new? (exit 10 when yes)
rem   update.cmd --dry-run        print the plan, touch nothing
rem   update.cmd --rollback       return to the commit recorded before the last update
rem
rem All the work is tools\update.py, and on Windows that script builds with MSVC through
rem tools\build_windows.py - the same updater that runs on Linux, with the platform's own
rem compiler. This file only finds Python and passes the exit code back.
rem
rem Why not the obvious `git pull && make` one-liner: in cmd.exe a single '&' runs the next
rem command whether or not the previous one succeeded, so a failed build looks like a
rem successful update. Every step is checked here, and Python's exit code is returned
rem unchanged: 0 updated, 10 update available, 1 failed.
rem
rem Note on `setlocal EnableDelayedExpansion` below: inside a parenthesised block, cmd.exe
rem expands %VAR% when the block is *parsed*, not when each line runs, so a check like
rem `if %ERRORLEVEL% NEQ 0` inside a block reads a stale value. Every in-block check here
rem uses !ERRORLEVEL! instead, which is why delayed expansion is required.

setlocal EnableExtensions EnableDelayedExpansion
set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "REPO_ROOT=%%~fI"
set "RC=1"

rem Python writes the ✔/✘ marks; UTF-8 keeps them readable in a modern console and stops
rem Python from falling back to ASCII on a code page that cannot encode them.
chcp 65001 >nul 2>nul
set "PYTHONIOENCODING=utf-8"
set "PYTHONUTF8=1"

rem --- 1. Native Python: the real path, and it builds with MSVC ------------------
rem Windows ships a Store stub named python.exe that opens the Microsoft Store instead of
rem running anything, so `where python` proves nothing. Asking for a version does.
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
if defined PY_EXE (
  rem %* is passed through unchanged; flags have no spaces, by design.
  !PY_EXE! !PY_FLAG! "%SCRIPT_DIR%update.py" %*
  set "RC=!ERRORLEVEL!"
  if !RC! EQU 0 goto :done
  if !RC! EQU 10 goto :done
  echo.
  echo   The update failed ^(exit !RC!^). The log above says where.
  goto :done
)

rem --- 2. No Python here: hand the job to WSL from the Linux side of the tree -----
where wsl >nul 2>nul
if !ERRORLEVEL! EQU 0 (
  set "WSL_REPO="
  for /f "delims=" %%p in ('wsl.exe -e wslpath -u "%REPO_ROOT%" 2^>nul') do set "WSL_REPO=%%p"
  if defined WSL_REPO (
    wsl.exe -e sh -lc "test -f '!WSL_REPO!/tools/update.py'" >nul 2>nul
    if !ERRORLEVEL! EQU 0 (
      echo.
      echo   BrowserX updater - no Windows Python, running in WSL: !WSL_REPO!
      wsl.exe -e sh -lc "cd '!WSL_REPO!' && exec python3 tools/update.py %*"
      set "RC=!ERRORLEVEL!"
      if !RC! EQU 0 goto :done
      if !RC! EQU 10 goto :done
      echo.
      echo   The update failed inside WSL ^(exit !RC!^). The log above says where.
      goto :done
    )
    echo   WSL is present, but !WSL_REPO! has no tools/update.py - is this a BrowserX checkout?
  ) else (
    echo   WSL is present, but this folder could not be resolved inside it.
  )
  echo   Falling back to the Windows-side update, which updates the source only.
)

rem --- 3. Last resort: the two commands, with a real error check ----------------
echo.
echo   Neither Python 3 nor a usable WSL was found: updating the source with git alone.
where git >nul 2>nul
if !ERRORLEVEL! NEQ 0 (
  echo   git is not installed either. Install Git, then run this again.
  goto :done
)
pushd "%REPO_ROOT%"
git pull --ff-only
if !ERRORLEVEL! NEQ 0 (
  echo   git pull failed - the working tree may have local changes. Nothing was overwritten.
  popd
  goto :done
)
popd
echo   Source updated. To build it as well, install Python 3 and run this again:
echo     winget install --id Python.Python.3.12
echo   Full requirements: BUILDING.md
set "RC=0"

:done
rem %RC% is expanded before endlocal runs: the exit code survives leaving the scope.
endlocal & exit /b %RC%
