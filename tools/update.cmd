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
rem The desktop build is Linux (Chromium/CEF, X11/GTK), so this does the real work in WSL
rem when WSL is available: it runs the very same tools/update.py, so the Windows and Linux
rem updaters cannot drift into two implementations of "update". Without WSL the source is
rem still updated, and the script says plainly that building needs Linux or WSL. It never
rem pretends to have built something it could not.
rem
rem Why not the obvious `git pull && make` one-liner: in cmd.exe a single '&' runs the next
rem command whether or not the previous one succeeded, so a failed build looks like a
rem successful update and the exit code is the last command's, not the worst one. This
rem script checks every step, passes Python's exit code through unchanged, and prints what
rem happened.
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

rem --- 1. WSL: where the real browser build lives --------------------------------
where wsl >nul 2>nul
if !ERRORLEVEL! EQU 0 (
  rem wslpath is the correct translation for every layout, including a checkout on a
  rem different drive or under \\wsl$. If it fails, fall through to the Windows paths.
  set "WSL_REPO="
  for /f "delims=" %%p in ('wsl.exe -e wslpath -u "%REPO_ROOT%" 2^>nul') do set "WSL_REPO=%%p"
  if defined WSL_REPO (
    wsl.exe -e sh -lc "test -f '!WSL_REPO!/tools/update.py'" >nul 2>nul
    if !ERRORLEVEL! EQU 0 (
      echo.
      echo   BrowserX updater - running in WSL: !WSL_REPO!
      rem %* is passed through unchanged; flags have no spaces, by design.
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

rem --- 2. Native Python: source update only, and honest about the build ---------
rem `where python` is not proof that Python works: Windows ships a Store stub named
rem python.exe that opens the Microsoft Store and exits without running anything. So each
rem candidate is asked to print a version, and only one that answers is used.
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
if not defined PY_EXE goto :no_python

echo.
echo   BrowserX updater - no WSL, updating the source only
!PY_EXE! !PY_FLAG! "%SCRIPT_DIR%update.py" %* --no-build
set "RC=!ERRORLEVEL!"
if !RC! EQU 0 (
  echo   Source updated. The browser itself builds on Linux or in WSL - see BUILDING.md
)
goto :done

:no_python
rem --- 3. Last resort: the two commands, with a real error check ----------------
echo.
echo   No WSL and no Python 3 found: updating the source with git alone.
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
echo   Source updated. To build: install WSL ^(wsl --install^) or use a Linux machine.
echo   Full requirements: BUILDING.md
set "RC=0"

:done
rem %RC% is expanded before endlocal runs: the exit code survives leaving the scope.
endlocal & exit /b %RC%
