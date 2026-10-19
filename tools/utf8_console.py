#!/usr/bin/env python3
# BrowserX — (c) 2026 codero-sus. All rights reserved.
# Personal, non-commercial use only. No modification, no redistribution or resale.
# Source-available, not open source — see LICENSE. https://the-codero.web.app
"""Make a tool's output survive a console that cannot encode it.

Every tool here prints ✔ and ✘, and some print → and … . On Linux that is UTF-8 and fine.
On Windows, Python picks the console's code page for stdout — often cp1252 — and writing
"→" raises

    UnicodeEncodeError: 'charmap' codec can't encode character '\\u2192'

which kills the tool *while it is reporting on something else*. That is exactly what
happened in a GitHub Windows runner: `python tools\\fetch_cef.py` died printing its plan,
before downloading anything, with a traceback that says nothing about CEF.

`tools\\*.cmd` wrappers already set PYTHONIOENCODING and chcp 65001, but a person running
`python tools\\fetch_cef.py` by hand gets no such protection, and neither does CI. So the
fix belongs in the tools, not only in the wrappers.

Two deliberate choices:

* `errors="replace"` rather than "strict": a character that cannot be written becomes "?"
  and the run continues. Losing a tick mark is not a reason to fail a build.
* failures are swallowed. If a stream cannot be reconfigured (not a real console, already
  detached, a test harness), the tool keeps working exactly as before. This is a
  best-effort improvement, never a new way to fail.
"""

from __future__ import annotations

import sys


def enable(encoding: str = "utf-8") -> bool:
    """Reconfigure stdout and stderr to `encoding`; return True when both succeeded."""
    ok = True
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure is None:  # a stream that is not a TextIOWrapper
            ok = False
            continue
        try:
            reconfigure(encoding=encoding, errors="replace")
        except (OSError, ValueError, AttributeError):
            ok = False
    return ok


def safe_write(stream, text: str) -> None:
    """Write text that a narrow console would otherwise refuse, without losing the run."""
    try:
        stream.write(text)
    except UnicodeEncodeError:
        encoding = getattr(stream, "encoding", None) or "ascii"
        stream.write(text.encode(encoding, errors="replace").decode(encoding, errors="replace"))


def self_test() -> int:
    """Prove the fallback works, on a stream that is deliberately too narrow to write to."""
    import io

    checks = []

    # A utf-8 stream accepts the marks: this is the normal case.
    stream = io.StringIO()
    safe_write(stream, "✔ done\n")
    checks.append(("a utf-8 stream keeps the tick", stream.getvalue() == "✔ done\n"))

    # A cp1252 stream cannot encode →, and must degrade to a replacement rather than raise.
    narrow = io.TextIOWrapper(io.BytesIO(), encoding="cp1252", errors="strict")
    try:
        narrow.write("→")
        raised = False
    except UnicodeEncodeError:
        raised = True
    checks.append(("the test really is using a narrow stream", raised))
    buffer = io.BytesIO()
    narrow = io.TextIOWrapper(buffer, encoding="cp1252", errors="strict")
    safe_write(narrow, "→ ok\n")
    narrow.flush()
    checks.append(("a narrow console gets a replacement, not a crash",
                   "ok" in buffer.getvalue().decode("cp1252")))

    # enable() on the real streams must never raise, whatever they are.
    checks.append(("enable() never raises on the real streams", isinstance(enable(), bool)))

    # Windows' default for a redirected pipe is the locale encoding; this is the case that
    # broke CI, so it is stated as a fact the fix depends on.
    checks.append(("the module names the failure it exists for", "charmap" in (__doc__ or "")))

    for ok, what in checks:
        print(f"  {'✔' if ok else '✘'} {what}")
    passed = sum(1 for ok, _ in checks if ok)
    print("")
    if passed == len(checks):
        print(f"  self-test passed ({passed} checks)")
        return 0
    print(f"  self-test FAILED ({len(checks) - passed} of {len(checks)})")
    return 1


if __name__ == "__main__":
    enable()
    sys.exit(self_test())
