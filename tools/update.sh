#!/bin/sh
# BrowserX — (c) 2026 codero-sus. All rights reserved.
# Personal, non-commercial use only. No modification, no redistribution or resale.
# Source-available, not open source — see LICENSE. https://the-codero.web.app
#
# BrowserX — the Linux (and macOS/WSL) updater.
#
#   tools/update.sh                 pull, rebuild, verify
#   tools/update.sh --check         is there anything new? (exit 10 when yes)
#   tools/update.sh --dry-run       print the plan, touch nothing
#   tools/update.sh --rollback      return to the commit recorded before the last update
#
# This is a launcher, not a second implementation: all of the work is tools/update.py, so
# the Windows wrapper (tools\update.cmd, which runs the same file inside WSL) cannot drift
# away from this one. Python 3 is already a documented build requirement (the Makefile uses
# it for the CEF fetcher), which is why the logic lives there and not in shell.
#
# Exit codes are passed through unchanged: 0 updated, 10 update available (--check),
# 1 failed. POSIX sh, so it also runs under dash, busybox ash and macOS's sh.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)

if command -v python3 >/dev/null 2>&1; then
  python=python3
elif command -v python >/dev/null 2>&1; then
  python=python
else
  echo "" >&2
  echo "  BrowserX updater needs Python 3 (already required for the build)." >&2
  case "$(uname -s 2>/dev/null || echo unknown)" in
    Darwin) echo "    install it with:  brew install python3" >&2 ;;
    *)      echo "    install it with:  sudo apt install python3     (or your package manager)" >&2 ;;
  esac
  echo "    or update by hand:  git pull --ff-only && make" >&2
  echo "" >&2
  exit 1
fi

if [ ! -f "$repo_root/tools/update.py" ]; then
  echo "  BrowserX updater: $repo_root/tools/update.py is missing - is this a full checkout?" >&2
  exit 1
fi

# exec: the exit code is Python's own, with no shell in between to mask it.
exec "$python" "$repo_root/tools/update.py" "$@"
