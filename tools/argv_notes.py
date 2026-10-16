#!/usr/bin/env python3
# BrowserX — (c) 2026 codero-sus. All rights reserved.
# Personal, non-commercial use only. No modification, no redistribution or resale.
# Source-available, not open source — see LICENSE. https://the-codero.web.app
"""BrowserX — one small kindness for text pasted out of the documentation.

In `cmd.exe` a comment is only a comment when it is the whole line. `command :: note`
and `command rem note` are handed to the command as extra arguments, so a note written
after a command turns a correct line into a broken one — which is exactly what happened
to a real person following the Windows instructions:

    >python tools\\fetch_cef.py           rem downloads the CEF runtime (once, ~200 MB)
    tools/fetch_cef.py: error: unrecognized arguments: rem downloads the CEF runtime

The documentation no longer contains lines like that (tools/check_docs_commands.py keeps
it that way), but a person can still write one. When that happens the tool should say
what is wrong instead of printing argparse's "unrecognized arguments", so this module
recognises a pasted comment, drops it, and explains once.

    python3 tools/argv_notes.py --self-test
"""

from __future__ import annotations

import re
import sys

# `::`, `::text` (cmd.exe allows no space), and `rem`/`REM` as a word of its own. A
# command's own argument is never one of these: no BrowserX tool takes "rem" or "::".
COMMENT_TOKEN = re.compile(r"^(::|(?i:rem)$)")


def strip_trailing_comment(argv: list[str]) -> tuple[list[str], str]:
    """Return (arguments, note). The note is empty unless something was dropped.

    Only what comes *after* the command is considered: a leading `rem`/`::` is a
    whole-line comment that cmd.exe would have removed before the command ever ran, and
    an empty result is left alone so the tool still prints its own usage.
    """
    for index, token in enumerate(argv):
        if index and COMMENT_TOKEN.match(token):
            dropped = " ".join(argv[index:])
            kept = argv[:index]
            note = (f'ignoring the note after the command: "{dropped}"\n'
                    f'         in cmd.exe that is not a comment, it is another argument - '
                    f'put it on its own line above.')
            return kept, note
    return argv, ""


def self_test() -> int:
    failures = 0

    def expect(condition: bool, label: str) -> None:
        nonlocal failures
        print(f"    {'✔' if condition else '✘'} {label}")
        failures += not condition

    print("  self-test")

    kept, note = strip_trailing_comment(["--stage", "rem", "the", "runtime"])
    expect(kept == ["--stage"] and "modifier" not in note,
           "a pasted `rem` note is dropped, the real arguments are kept")
    kept, note = strip_trailing_comment(["--verbose", "::", "say", "why"])
    expect(kept == ["--verbose"] and note, "a pasted `::` note is dropped")
    kept, note = strip_trailing_comment(["--stage", "::note-with-no-space"])
    expect(kept == ["--stage"] and note, "a `::note` glued together is dropped")
    kept, note = strip_trailing_comment(["::", "note"])
    expect(kept == ["::", "note"],
           "a comment in the *first* position is left for the tool to report")

    kept, note = strip_trailing_comment(["--force"])
    expect(kept == ["--force"] and not note, "a clean command is untouched")
    kept, note = strip_trailing_comment(["--url", "https://example.com/cef.tar.bz2"])
    expect(kept == ["--url", "https://example.com/cef.tar.bz2"] and not note,
           "a URL with a colon is never mistaken for a comment")
    kept, note = strip_trailing_comment(["rem", "--stage"])
    expect(kept == ["rem", "--stage"],
           "a leading rem is left for the tool to report")
    kept, note = strip_trailing_comment(["--ref", "remotes/upstream"])
    expect(kept == ["--ref", "remotes/upstream"] and not note,
           "an argument that merely starts with the word rem is kept")

    print()
    if failures:
        print(f"  {failures} self-test failure(s)\n")
        return 1
    print("  self-test passed\n")
    return 0


def main(argv: list[str]) -> int:
    if argv and argv[0] == "--self-test":
        return self_test()
    kept, note = strip_trailing_comment(argv)
    print(f"kept: {kept}")
    if note:
        print(note)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
