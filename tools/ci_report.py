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
import re
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
LOG_DIR = REPO_ROOT / "out" / "ci"
#: How much trailing output to keep for the "why did it fail" decision. Generous, because
#: the line that explains a failure is often not the last one - make prints the command it
#: was running, and a compiler prints a preamble before the error.
DEFAULT_LINES = 120

#: GitHub accepts at most ten annotations per step created by workflow commands. Beyond
#: that it silently drops them, which is how the first version of this reported nine lines
#: of "TST tests/unit/..." and no error at all: the useful lines were past the cap.
ANNOTATION_LIMIT = 10

#: What a line that explains a failure looks like. Deliberately broad: compilers, linkers,
#: make, Python tracebacks and git all say "error" in their own way, and a missed line
#: costs another push-and-wait cycle to discover.
DIAGNOSTIC = re.compile(
    r"(error|fatal|cannot|can't|unable to|failed|failure|not found|no such file|"
    r"undefined reference|Traceback|Exception|make: \*\*\*|✘|\bX |exit code)",
    re.IGNORECASE)


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

    # The annotations are the point, and there are only ten of them. Spend them on the
    # lines that explain the failure, then on the end of the output: a build that dies
    # after printing its plan must still say what the plan was, but an error outranks it.
    chosen = [line for line in tail if line.strip() and DIAGNOSTIC.search(line)]
    if not chosen:
        chosen = [line for line in tail if line.strip()][-ANNOTATION_LIMIT:]
    if len(chosen) > ANNOTATION_LIMIT:
        # The first error is the one that matters (later ones are usually consequences),
        # but the last lines hold the summary - keep both ends rather than only the tail.
        chosen = chosen[:6] + chosen[-(ANNOTATION_LIMIT - 6):]
    for line in chosen:
        message = escape(line)
        if message.strip():
            print(f"::error title={escape(label)}::{message}")
    if len(tail) > ANNOTATION_LIMIT:
        print(f"  ({len(tail)} trailing lines captured; the rest are in the log)",
              file=sys.stderr)
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
    # Windows console code pages cannot encode ✔ or →; without this a tool dies while
    # reporting something else. Best effort: it never makes a working run fail.
    import utf8_console  # same directory: sys.path[0] when run as a script
    utf8_console.enable()
    sys.exit(main(sys.argv[1:]))
