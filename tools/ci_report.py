#!/usr/bin/env python3
# BrowserX — (c) 2026 codero-sus. All rights reserved.
# Personal, non-commercial use only. No modification, no redistribution or resale.
# Source-available, not open source — see LICENSE. https://the-codero.web.app
"""Run a command and, when it fails, make the reason readable where nobody can read logs.

Why this exists: a GitHub Actions job that fails is a red cross and a log you have to open
in a browser. When the log is not reachable — a proxy, a locked-down network, an API that
only returns a redirect — the failure becomes unfalsifiable, and the only way to debug it
is to guess and push again. Annotations are different: they are returned by the checks
API (`GET /repos/{owner}/{repo}/check-runs/{id}/annotations`), so they survive exactly the
situation where the log does not.

  python tools/ci_report.py --label "make test" -- make test
  python tools/ci_report.py --label "build" --python "tools/build_windows.py --stage"

Behaviour:

* stdout and stderr are passed through unchanged, so a human reading the log sees the
  original output, not a reconstruction.
* the last `--lines` lines are kept, and on failure each becomes an `::error::…` workflow
  command: GitHub attaches them to the failing step as annotations.
* the full output is also written under out/ci/ so a later step can upload it, and the
  printed path is in the summary line.
* the exit code is the command's own. A wrapper that swallows the code is worse than none.

The escaping rules for workflow commands are not optional: `%` starts an escape, a raw
newline ends the command, and an unescaped `:::` inside the message can end it early.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
LOG_DIR = REPO_ROOT / "out" / "ci"
#: Long enough to hold a compiler's error list, short enough that 50 annotations cover
#: what matters; GitHub caps how many it shows per step.
DEFAULT_LINES = 40


def escape(text: str) -> str:
    """Escape a workflow-command message (not a property)."""
    return (text.replace("%", "%25")
                .replace("\r", "%0D")
                .replace("\n", "%0A"))


def safe_name(label: str) -> str:
    keep = "".join(c if c.isalnum() or c in "-_." else "-" for c in label)
    return keep.strip("-") or "step"


def run(label: str, command: list[str], keep: int) -> int:
    started = time.time()
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    log_path = LOG_DIR / f"{safe_name(label)}.log"
    tail: list[str] = []
    with log_path.open("w", encoding="utf-8", errors="replace") as log:
        # No shell: the command is a list, so quoting cannot be mangled by a shell the
        # author did not choose. Callers that need shell syntax pass it to --shell.
        process = subprocess.Popen(command, cwd=str(REPO_ROOT), stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, bufsize=1,
                                   encoding="utf-8", errors="replace")
        assert process.stdout is not None
        for line in process.stdout:
            line = line.rstrip("\n")
            sys.stdout.write(line + "\n")
            sys.stdout.flush()
            log.write(line + "\n")
            tail.append(line)
            if len(tail) > keep:
                tail.pop(0)
        code = process.wait()

    elapsed = time.time() - started
    if code == 0:
        print(f"\n  {label}: ok ({elapsed:.1f}s, log: {log_path.relative_to(REPO_ROOT)})")
        return 0

    print(f"\n  {label}: FAILED with exit code {code} ({elapsed:.1f}s)",
          file=sys.stderr)
    print(f"  full output: {log_path}", file=sys.stderr)
    # The annotations are the point. Emit the tail in order, so the last line - usually
    # the one that says what to fix - is the most recent annotation.
    for line in tail:
        message = escape(line)
        if message.strip():
            print(f"::error title={escape(label)}::{message}")
    return code


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Run a command; on failure, make its output readable as annotations.")
    parser.add_argument("--label", required=True, help="what to call this step in messages")
    parser.add_argument("--lines", type=int, default=DEFAULT_LINES,
                        help=f"how many trailing lines to report (default {DEFAULT_LINES})")
    parser.add_argument("--shell", action="store_true",
                        help="pass the command through the shell (use for pipelines)")
    parser.add_argument("command", nargs=argparse.REMAINDER,
                        help="the command, after --")
    args = parser.parse_args(argv)

    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("no command given (write it after --)")

    if args.shell:
        # Joined back into one string on purpose: the caller asked for shell semantics.
        rendered = " ".join(command)
        if sys.platform.startswith("win"):
            return run(args.label, ["cmd", "/c", rendered], args.lines)
        return run(args.label, ["/bin/sh", "-c", rendered], args.lines)
    return run(args.label, command, args.lines)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
