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
import queue
import re
import subprocess
import sys
import threading
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


def run(label: str, command: list[str], keep: int, timeout: float = 0.0) -> int:
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

        # Read on a thread and consume from a queue rather than iterating the pipe
        # directly. Why: iterating `for line in process.stdout` ends only when every writer
        # has closed the pipe, and a grandchild that inherited the handle keeps it open
        # after the child exits - the step then hangs forever with no output, which is
        # exactly what happened to a Windows runner. With a queue, the parent's exit is
        # what ends the loop, and a stuck child can be killed on timeout.
        lines: "queue.Queue[str | None]" = queue.Queue()

        def pump(stream) -> None:
            try:
                for text in stream:
                    lines.put(text)
            finally:
                lines.put(None)

        reader = threading.Thread(target=pump, args=(process.stdout,), daemon=True)
        reader.start()

        seen_end = False
        quiet_since: float | None = None
        while True:
            try:
                item = lines.get(timeout=0.5)
            except queue.Empty:
                item = "__empty__"
            if item is None:
                seen_end = True
                break
            if item != "__empty__":
                line = str(item).rstrip("\n")
                sys.stdout.write(line + "\n")
                sys.stdout.flush()
                log.write(line + "\n")
                tail.append(line)
                if len(tail) > keep:
                    tail.pop(0)
            if process.poll() is not None:
                # The child has exited. Its reader may still be blocked because something
                # it started inherited the pipe, so wait a moment for output already in
                # flight and then stop waiting for a pipe nobody is going to close.
                if quiet_since is None:
                    quiet_since = time.time()
                elif (time.time() - quiet_since) > 1.5:
                    break
            else:
                quiet_since = None
            if timeout and (time.time() - started) > timeout:
                process.kill()
                print(f"\n  {label}: killed after {timeout:.0f}s", file=sys.stderr)
                break
        if not seen_end:
            # Drain whatever the reader still had, without waiting for the pipe to close.
            while True:
                try:
                    item = lines.get_nowait()
                except queue.Empty:
                    break
                if item is None:
                    break
                line = str(item).rstrip("\n")
                sys.stdout.write(line + "\n")
                log.write(line + "\n")
                tail.append(line)
                if len(tail) > keep:
                    tail.pop(0)
        code = process.wait()

    elapsed = time.time() - started
    if code == 0:
        shown = log_path.relative_to(REPO_ROOT) if log_path.is_relative_to(REPO_ROOT) else log_path
        print(f"\n  {label}: ok ({elapsed:.1f}s, log: {shown})")
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


def self_test() -> int:
    """The three properties this wrapper exists for, checked without a CI run."""
    import contextlib
    import io
    import tempfile

    checks: list[tuple[bool, str]] = []

    def expect(ok: bool, what: str) -> None:
        checks.append((bool(ok), what))

    with tempfile.TemporaryDirectory(prefix="browserx-ci-") as scratch:
        global LOG_DIR
        real_dir = LOG_DIR
        LOG_DIR = Path(scratch)
        try:
            quiet = io.StringIO()
            with contextlib.redirect_stdout(quiet), contextlib.redirect_stderr(quiet):
                code = run("ok", [sys.executable, "-c", "print('fine')"], 40)
            expect(code == 0, "a successful command keeps exit code 0")
            expect("fine" in quiet.getvalue(), "its output is passed through, not swallowed")

            quiet = io.StringIO()
            with contextlib.redirect_stdout(quiet), contextlib.redirect_stderr(quiet):
                code = run("bad", [sys.executable, "-c",
                                   "print('the reason'); raise SystemExit(7)"], 40)
            expect(code == 7, "a failing command keeps its own exit code")
            expect("::error title=bad::the reason" in quiet.getvalue(),
                   "the reason becomes an annotation")

            # The hang this wrapper caused once: a grandchild inherits the pipe and stays
            # alive, so waiting for the pipe to close waits for the grandchild's lifetime.
            started = time.time()
            quiet = io.StringIO()
            with contextlib.redirect_stdout(quiet), contextlib.redirect_stderr(quiet):
                code = run("leaky", [sys.executable, "-c",
                                     "import subprocess, sys\n"
                                     "subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'])\n"
                                     "print('child done')"], 40)
            elapsed = time.time() - started
            expect(code == 0, "a child that leaves a grandchild behind still reports success")
            expect(elapsed < 10, f"it does not wait for a pipe the child left open ({elapsed:.1f}s)")
        finally:
            LOG_DIR = real_dir

    for ok, what in checks:
        print(f"  {'✔' if ok else '✘'} {what}")
    passed = sum(1 for ok, _ in checks if ok)
    print("")
    if passed == len(checks):
        print(f"  self-test passed ({passed} checks)")
        return 0
    print(f"  self-test FAILED ({len(checks) - passed} of {len(checks)})")
    return 1


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Run a command; on failure, make its output readable as annotations.")
    parser.add_argument("--self-test", action="store_true", help="check the wrapper itself")
    parser.add_argument("--label", help="what to call this step in messages")
    parser.add_argument("--lines", type=int, default=DEFAULT_LINES,
                        help=f"how many trailing lines to report (default {DEFAULT_LINES})")
    parser.add_argument("--timeout", type=float, default=0.0,
                        help="kill the command after this many seconds (0 = no limit)")
    parser.add_argument("--shell", action="store_true",
                        help="pass the command through the shell (use for pipelines)")
    parser.add_argument("command", nargs=argparse.REMAINDER,
                        help="the command, after --")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()
    if not args.label:
        parser.error("--label is required")

    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("no command given (write it after --)")

    if args.shell:
        # Joined back into one string on purpose: the caller asked for shell semantics.
        rendered = " ".join(command)
        if sys.platform.startswith("win"):
            return run(args.label, ["cmd", "/c", rendered], args.lines, args.timeout)
        return run(args.label, ["/bin/sh", "-c", rendered], args.lines, args.timeout)
    return run(args.label, command, args.lines, args.timeout)


if __name__ == "__main__":
    # Windows console code pages cannot encode ✔ or →; without this a tool dies while
    # reporting something else. Best effort: it never makes a working run fail.
    import utf8_console  # same directory: sys.path[0] when run as a script
    utf8_console.enable()
    sys.exit(main(sys.argv[1:]))
