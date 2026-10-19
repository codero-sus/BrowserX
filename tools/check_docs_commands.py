#!/usr/bin/env python3
# BrowserX — (c) 2026 codero-sus. All rights reserved.
# Personal, non-commercial use only. No modification, no redistribution or resale.
# Source-available, not open source — see LICENSE. https://the-codero.web.app
"""BrowserX — check that the commands in the documentation actually work.

Why this exists: a real paste of the Windows instructions failed with

    usage: tools/fetch_cef.py [-h] ...
    error: unrecognized arguments: rem downloads the CEF runtime (once, ~200 MB)

In `cmd.exe` a comment is only a comment when it is the whole line. `command :: note`
and `command rem note` are handed to the command as extra arguments, so a note written
after a command turns a correct line into a broken one. The documentation is the first
thing a person runs, so the documentation is checked like everything else:

  * no command in a `bat`/`cmd` block carries a trailing `::`/`rem` comment;
  * every `tools\\...` script a command names exists;
  * every `--flag` a command passes to a `tools\\*.py` is one that script accepts;
  * every `make <target>` names a target this Makefile defines.

Only the flags are verified against the tools themselves (by running them with `--help`,
which is safe and touches nothing); nothing here executes a documented command.

    python3 tools/check_docs_commands.py            # check the docs
    python3 tools/check_docs_commands.py --self-test
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
EXIT_OK = 0
EXIT_FAILED = 1

DOC_FILES = ["README.md", "BUILDING.md", "CONTRIBUTING.md", "docs/*.md"]
SHELL_LANGS = {"bat", "cmd", "batch", "dos", "shell", "sh", "bash", "console", "zsh"}
WIN_LANGS = {"bat", "cmd", "batch", "dos"}

# A command, then at least one space, then a comment marker. Anchored so a comment on
# its own line (which cmd.exe *does* honour) is never reported.
TRAILING_COMMENT = re.compile(r"^\s*\S.*?\s+(::|rem\b|REM\b)", re.IGNORECASE)
PROMPT = re.compile(r"^\s*(>|>>|\$|#|PS\s*[^>]*>)\s*")
TOOL_WORD = re.compile(r"^[\w./\\-]*tools[/\\][\w.-]+\.(py|cmd|bat|sh)$", re.IGNORECASE)
FLAG = re.compile(r"^--[A-Za-z][\w-]*$")
SH_COMMENT = re.compile(r"\s+#.*$")                # in sh, ` #` starts a real comment


def fences(text: str):
    """Yield (language, [(line number, line)]) for every fenced block in a document."""
    lang, lines, inside, start = "", [], False, 0
    for number, line in enumerate(text.splitlines(), start=1):
        if line.startswith("```"):
            if inside:
                yield lang, lines
                inside, lines = False, []
            else:
                inside, lang, start = True, line[3:].strip().lower(), number
            continue
        if inside:
            lines.append((number, line))
    if inside:                                    # an unclosed fence: report what we saw
        yield lang, lines


def makefile_targets(makefile: Path) -> set[str]:
    if not makefile.exists():
        return set()
    targets = set()
    for line in makefile.read_text(errors="replace").splitlines():
        match = re.match(r"^([A-Za-z0-9_][\w.-]*)\s*:(?!=)", line)
        if match:
            targets.add(match.group(1))
    return targets


# The targets whose prerequisites are audited. `make check` is the one a person runs
# before trusting a change, and the other three are the entry points the docs promise.
AUDITED_TARGETS = ("check", "test", "all", "gui")


def missing_prerequisites(text: str) -> list[str]:
    """Prerequisites that name no target at all.

    This is not hypothetical: `check` listed `check-gui` while the target was really
    called `gui-check`, so `make check` died with "No rule to make target 'check-gui'"
    and every gate after it silently never ran. Only phony-looking names are considered
    - no dot, no slash, no `$(...)` - so a real file prerequisite is left alone, and a
    prerequisite list split over several lines with a backslash is not followed.
    """
    targets = {m.group(1) for m in re.finditer(r"^([A-Za-z0-9_][\w.-]*)\s*:(?!=)", text, re.M)}
    problems: list[str] = []
    for line in text.splitlines():
        match = re.match(r"^([A-Za-z0-9_][\w.-]*)\s*:(?!=)\s*(.*)$", line)
        if not match or match.group(1) not in AUDITED_TARGETS:
            continue
        for token in match.group(2).split():
            if not re.fullmatch(r"[a-z][\w-]*", token) or token in targets:
                continue
            problems.append(
                f"Makefile: `{match.group(1)}` needs `{token}`, and nothing defines it")
    return problems


def tool_accepts_flag(tool: Path, flag: str) -> bool:
    """True when the flag appears in the tool's own --help. `--help` never builds."""
    try:
        result = subprocess.run([sys.executable, str(tool), "--help"],
                                capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.SubprocessError):
        return True                               # cannot ask: do not invent a failure
    return flag in (result.stdout + result.stderr)


def check_document(path: Path, targets: set[str]) -> list[str]:
    problems: list[str] = []
    seen: dict[str, bool] = {}

    for lang, lines in fences(path.read_text(errors="replace")):
        if lang not in SHELL_LANGS:
            continue
        for number, line in lines:
            body = PROMPT.sub("", line)
            stripped = body.strip()
            if not stripped or stripped.startswith(("rem ", "::", "#", "@")):
                continue                          # a whole-line comment is fine everywhere
            if lang in WIN_LANGS and TRAILING_COMMENT.match(body):
                marker = TRAILING_COMMENT.search(body).group(1)     # type: ignore[union-attr]
                problems.append(
                    f"{path.name}:{number}: cmd.exe does not treat '{marker}' as a comment "
                    f"after a command - it is passed as an argument. Put the note on its "
                    f"own line above the command.")
                continue

            command = body if lang in WIN_LANGS else SH_COMMENT.sub("", body)
            words = command.split()
            if not words:
                continue
            tool = next((w for w in words if TOOL_WORD.match(w)), None)
            if tool:
                resolved = REPO_ROOT / tool.replace("\\", "/")
                if not resolved.exists():
                    problems.append(f"{path.name}:{number}: {tool} does not exist")
                    continue
                if resolved.suffix.lower() == ".py":
                    for word in words[words.index(tool) + 1:]:
                        if word.startswith("-") and FLAG.match(word):
                            if word not in seen:
                                seen[word] = tool_accepts_flag(resolved, word)
                            if not seen[word]:
                                problems.append(
                                    f"{path.name}:{number}: {tool} does not accept {word}")
            if words[0] == "make":
                for word in words[1:]:
                    if word.startswith("-") or "=" in word or word in {"deps", "check"}:
                        continue
                    if word not in targets:
                        problems.append(
                            f"{path.name}:{number}: `make {word}` - no such target in the Makefile")
    return problems


def self_test() -> int:
    failures = 0

    def expect(condition: bool, label: str) -> None:
        nonlocal failures
        print(f"    {'✔' if condition else '✘'} {label}")
        failures += not condition

    print("  self-test")
    bad = '```bat\npython tools\\fetch_cef.py  :: the binaries\n```'
    hits = [line for lang, lines in fences(bad) for _, line in lines
            if lang in WIN_LANGS and TRAILING_COMMENT.match(PROMPT.sub("", line))]
    expect(len(hits) == 1, "a trailing :: after a command is caught")

    bad = '```bat\npython tools\\fetch_cef.py  rem the binaries\n```'
    hits = [line for lang, lines in fences(bad) for _, line in lines
            if lang in WIN_LANGS and TRAILING_COMMENT.match(PROMPT.sub("", line))]
    expect(len(hits) == 1, "a trailing rem after a command is caught")

    good = '```bat\nrem the binaries\npython tools\\fetch_cef.py\n:: also fine\n```'
    hits = [line for lang, lines in fences(good) for _, line in lines
            if lang in WIN_LANGS and TRAILING_COMMENT.match(PROMPT.sub("", line))]
    expect(not hits, "a comment on its own line is never reported")

    good = '```sh\nmake run         # launch it\n```'
    hits = [line for lang, lines in fences(good) for _, line in lines
            if lang in WIN_LANGS and TRAILING_COMMENT.match(PROMPT.sub("", line))]
    expect(not hits, "a shell block is left alone (there # really is a comment)")

    expect(bool(makefile_targets(REPO_ROOT / "Makefile")), "the Makefile targets are read")
    expect(missing_prerequisites(
        "check: check-compile check-gui\ncheck-compile:\ncheck-gui:\n\techo hi\n") == [],
        "a prerequisite that exists is accepted")
    caught = missing_prerequisites("check: check-compile check-gui\ncheck-compile:\n")
    expect(len(caught) == 1 and "check-gui" in caught[0],
           "the exact bug - a prerequisite whose name is wrong - is caught")
    expect(missing_prerequisites("all: out/browserx $(OBJS)\n") == [],
           "file and variable prerequisites are left alone")
    expect(tool_accepts_flag(REPO_ROOT / "tools/fetch_cef.py", "--headers-only"),
           "a real flag is accepted by the tool's own help")
    expect(not tool_accepts_flag(REPO_ROOT / "tools/fetch_cef.py", "--definitely-not-a-flag"),
           "an invented flag is rejected")

    print()
    if failures:
        print(f"  {failures} self-test failure(s)\n")
        return EXIT_FAILED
    print("  self-test passed\n")
    return EXIT_OK


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Check that commands in the documentation run as written.")
    parser.add_argument("--self-test", action="store_true", help="test the checker itself")
    parser.add_argument("--verbose", action="store_true", help="list every document checked")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()

    documents: list[Path] = []
    for pattern in DOC_FILES:
        documents.extend(sorted(REPO_ROOT.glob(pattern)))
    documents = [d for d in documents if d.is_file()]
    if not documents:
        print("  no documents found to check\n")
        return EXIT_OK

    targets = makefile_targets(REPO_ROOT / "Makefile")
    problems: list[str] = missing_prerequisites((REPO_ROOT / "Makefile").read_text(errors="replace"))
    for document in documents:
        where = str(document.relative_to(REPO_ROOT))
        found = check_document(document, targets)
        if args.verbose:
            print(f"    {where}: {'✘' if found else '✔'}")
        problems.extend(found)

    print()
    if problems:
        print("  ✘ the documentation contains commands that will not run:")
        for problem in problems:
            print(f"      {problem}")
        print()
        return EXIT_FAILED
    print(f"  ✔ {len(documents)} document(s): every documented command is well formed, "
          f"every tool and target it names exists, and {', '.join(AUDITED_TARGETS)} only "
          f"need targets that are defined\n")
    return EXIT_OK


if __name__ == "__main__":
    # Windows console code pages cannot encode ✔ or →; without this a tool dies while
    # reporting something else. Best effort: it never makes a working run fail.
    import utf8_console  # same directory: sys.path[0] when run as a script
    utf8_console.enable()
    sys.exit(main(sys.argv[1:]))
