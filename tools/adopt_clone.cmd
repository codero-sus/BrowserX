@echo off
rem BrowserX — (c) 2026 codero-sus. All rights reserved.
rem Personal, non-commercial use only. No modification, no redistribution or resale.
rem Source-available, not open source — see LICENSE. https://the-codero.web.app
rem
rem BrowserX — turn this downloaded folder into a git checkout.
rem
rem   adopt_clone.cmd             what would change; touches nothing
rem   adopt_clone.cmd --yes       do it
rem
rem Why: "Download ZIP" is the easy way to get the repository, and the result cannot be
rem updated. From then on every fix means another full download, and the updater refuses
rem to run because this is not a checkout. This converts the folder in place and keeps
rem everything git does not track — the CEF download (.cef-deps, about 1.4 GB), build
rem outputs, and your profile — so nothing has to be downloaded again.
rem
rem All the work is tools\adopt_clone.py; this file only finds Python and returns the
rem exit code unchanged (0 done, 1 failed, 3 cannot start).
rem
rem Note on `setlocal EnableDelayedExpansion`: inside a parenthesised block, cmd.exe
rem expands %VAR% when the block is *parsed*, not when each line runs, so `if %ERRORLEVEL%
rem NEQ 0` inside a block reads a stale value. Every in-block check here uses !ERRORLEVEL!
rem instead, which is why delayed expansion is required.

setlocal EnableExtensions EnableDelayedExpansion
set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "REPO_ROOT=%%~fI"
set "RC=1"

rem Python writes the ✔/✘ marks; UTF-8 keeps them readable and stops Python from falling
rem back to ASCII on a code page that cannot encode them.
chcp 65001 >nul 2>nul
set "PYTHONIOENCODING=utf-8"
set "PYTHONUTF8=1"

rem --- 1. git has to exist: this tool is git, with the thinking around it ----------
where git >nul 2>nul
if !ERRORLEVEL! NEQ 0 (
  echo.
  echo   git was not found. It is needed from here on, for this once and for updates.
  echo.
  echo     winget install --id Git.Git -e
  echo.
  echo   or the installer: https://git-scm.com/download/win
  echo   Then close this window and open a new one, so the new PATH is found.
  echo.
  goto :done
)

rem --- 2. Native Python: the real path ---------------------------------------------
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
  !PY_EXE! !PY_FLAG! "%SCRIPT_DIR%adopt_clone.py" %*
  set "RC=!ERRORLEVEL!"
  if !RC! EQU 0 goto :done
  echo.
  echo   That did not finish ^(exit !RC!^). The message above says why.
  goto :done
)

rem --- 3. No Python: the repository is only fetched and checked out ----------------
echo.
echo   Python 3 was not found, so the check that nothing is lost cannot run here.
echo   Refusing to touch your files without it: install Python and run this again.
echo.
echo     winget install --id Python.Python.3.12
echo.
echo   Full requirements: BUILDING.md

:done
rem %RC% is expanded before endlocal runs: the exit code survives leaving the scope.
endlocal & exit /b %RC%
