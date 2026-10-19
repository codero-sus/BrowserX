#!/usr/bin/env python3
# BrowserX — (c) 2026 codero-sus. All rights reserved.
# Personal, non-commercial use only. No modification, no redistribution or resale.
# Source-available, not open source — see LICENSE. https://the-codero.web.app
"""Which commit is this build? Answer it from git, and stamp the answer into the binary.

A browser built from a git checkout has a fact attached to it that a browser built from a
ZIP folder does not: the exact commit it came from. This tool makes the build carry that
fact, so "is this build current?" is a question with a verifiable answer instead of a guess.

  python tools/revision.py                 where we are, in words
  python tools/revision.py --json          the same, for tools
  python tools/revision.py --defines       the compiler flags that stamp the revision
  python tools/revision.py --record FILE   write a build record (the builders call this)
  python tools/revision.py --check FILE    exit 10 when the record is for an older commit
  python tools/revision.py --self-test

Exit codes match tools/update.py so scripts compose: 0 current, 10 stale, 1 error, 3
cannot tell.

Design notes:

* No git, or a folder that is not a checkout (a ZIP extract), is not an error. The revision
  becomes "unknown" and the build still runs — refusing to compile because provenance is
  unavailable would be a worse trade than compiling without it. It is reported as
  unknown, never guessed at.
* A dirty tree is part of the revision, not a warning to be suppressed: the binary says
  "<sha>+" so a build from uncommitted edits can never be mistaken for a clean build of
  that commit.
* Only `git` is shelled out to, read-only commands only: `rev-parse`, `describe`,
  `status`, `rev-list`. Nothing here writes to the repository.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

EXIT_CURRENT = 0
EXIT_FAILED = 1
EXIT_STALE = 10
EXIT_UNKNOWN = 3

UNKNOWN = "unknown"


def say(text: str = "") -> None:
    print(text, flush=True)


# ---------------------------------------------------------------------------
# Asking git
# ---------------------------------------------------------------------------


def git(args: list[str], cwd: Path | None = None) -> str | None:
    """Run a read-only git command, or None when git is absent or says no."""
    exe = shutil.which("git")
    if not exe:
        return None
    try:
        done = subprocess.run(
            [exe, *args],
            cwd=str(cwd or REPO_ROOT),
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=30,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if done.returncode != 0:
        return None
    return done.stdout.strip()


def is_checkout(root: Path | None = None) -> bool:
    """True only when `root` is the top of a git work tree.

    A folder inside someone else's repository (a ZIP extract unpacked into a checkout)
    would otherwise borrow the outer repository's identity, and the build would be stamped
    with a commit that has nothing to do with these sources. `--show-toplevel` is compared
    for exactly that reason.
    """
    root = (root or REPO_ROOT).resolve()
    top = git(["rev-parse", "--show-toplevel"], cwd=root)
    if not top:
        return False
    try:
        return Path(top).resolve() == root
    except OSError:
        return False


def describe(root: Path | None = None) -> dict[str, object]:
    """The facts a build needs, honestly marked unknown when git cannot supply them."""
    root = Path(root or REPO_ROOT)
    facts: dict[str, object] = {
        "checkout": False,
        "commit": UNKNOWN,
        "short": UNKNOWN,
        "branch": UNKNOWN,
        "dirty": False,
        "subject": "",
        "ahead": 0,
        "behind": 0,
    }
    if not is_checkout(root):
        return facts

    facts["checkout"] = True
    full = git(["rev-parse", "HEAD"], cwd=root)
    if full:
        facts["commit"] = full
        facts["short"] = full[:7]
    branch = git(["rev-parse", "--abbrev-ref", "HEAD"], cwd=root)
    if branch:
        facts["branch"] = branch
    subject = git(["log", "-1", "--pretty=%s"], cwd=root)
    if subject:
        facts["subject"] = subject
    # --porcelain covers staged, unstaged and untracked-but-not-ignored files, which is
    # exactly the set that means "this binary will not be reproducible from that commit".
    status = git(["status", "--porcelain"], cwd=root)
    facts["dirty"] = bool(status)

    upstream = git(["rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{upstream}"], cwd=root)
    if upstream:
        counts = git(["rev-list", "--left-right", "--count", f"HEAD...{upstream}"], cwd=root)
        if counts:
            parts = counts.split()
            if len(parts) == 2 and all(p.isdigit() for p in parts):
                facts["ahead"], facts["behind"] = int(parts[0]), int(parts[1])
    facts["upstream"] = upstream or ""
    return facts


def revision_label(facts: dict[str, object]) -> str:
    """`409d710` or `409d710+`; `unknown` when there is no checkout.

    The `+` is the whole point: it says the working tree had changes when this was built,
    so the commit alone does not describe the binary.
    """
    short = str(facts.get("short") or UNKNOWN)
    if short == UNKNOWN:
        return UNKNOWN
    return short + ("+" if facts.get("dirty") else "")


# ---------------------------------------------------------------------------
# Stamping a build
# ---------------------------------------------------------------------------


def define_values(facts: dict[str, object] | None = None) -> dict[str, str]:
    """The values a build stamps into the binary, as plain strings.

    Callers that hand flags to a compiler directly (subprocess, no shell) want these
    verbatim. A Makefile is different: its recipe line goes through /bin/sh, so the quotes
    around a string value have to be escaped there. Keeping the values here and rendering
    twice is what stops the two paths from drifting apart.
    """
    facts = facts or describe()
    return {
        "BROWSERX_GIT": revision_label(facts),
        "BROWSERX_GIT_BRANCH": str(facts.get("branch") or UNKNOWN),
        "BROWSERX_GIT_DIRTY": "1" if facts.get("dirty") else "0",
    }


#: Values that are numbers, not text. `BROWSERX_GIT_DIRTY` is tested with `#if`, and
#: `#if "1"` is a compile error, so these must never be quoted.
NUMERIC_DEFINES = ("BROWSERX_GIT_DIRTY",)


#: Where the generated header declares the revision. A header rather than -D flags is a
#: deliberate choice: make's $(shell) strips backslashes, so `-DNAME=\"value\"` arrives at
#: the compiler with no quotes and becomes a C++ expression (`arena/419ef7ef-mybrowser` ->
#: division). A header has no quoting layer to lose, and it works the same for MSVC.
REVISION_HEADER = "revision.h"

HEADER_TEMPLATE = """\
// Generated by tools/revision.py — do not edit, do not commit.
//
// This file is *rewritten only when its content changes*, so a build that produces the
// same revision does not invalidate every object file and force a full rebuild.
#pragma once

#define BROWSERX_GIT "{git}"
#define BROWSERX_GIT_BRANCH "{branch}"
#define BROWSERX_GIT_COMMIT "{commit}"
#define BROWSERX_GIT_DIRTY {dirty}
#define BROWSERX_GIT_SUBJECT "{subject}"
"""


def header_text(facts: dict[str, object] | None = None) -> str:
    """The revision as C++ the compiler can include, with every string escaped."""
    facts = facts or describe()

    def quote(text: object) -> str:
        # A branch name cannot contain a quote, but a commit subject can, and the subject
        # ends up in a C string literal: escape it rather than emit code that will not
        # compile because someone's commit message had a backslash in it.
        return str(text).replace("\\", "\\\\").replace('"', '\\"')

    return HEADER_TEMPLATE.format(
        git=quote(revision_label(facts)),
        branch=quote(facts.get("branch") or UNKNOWN),
        commit=quote(facts.get("commit") or UNKNOWN),
        dirty=1 if facts.get("dirty") else 0,
        subject=quote(facts.get("subject") or ""),
    )


def write_header(path: Path, facts: dict[str, object] | None = None) -> bool:
    """Write the header; return True when the file actually changed.

    Content-stable on purpose: `make` decides what to rebuild from timestamps, so touching
    this file on every build would recompile the whole project every time. Returning
    whether anything changed lets a caller say so.
    """
    path = Path(path)
    text = header_text(facts)
    try:
        if path.read_text(encoding="utf-8") == text:
            return False
    except OSError:
        pass
    path.parent.mkdir(parents=True, exist_ok=True)
    # Write-then-replace so a killed build cannot leave a half-written header behind for
    # the next one to compile.
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(text, encoding="utf-8")
    os.replace(temp, path)
    return True


def build_record(facts: dict[str, object] | None = None, extra: dict[str, object] | None = None) -> dict:
    facts = facts or describe()
    record = {
        "commit": facts["commit"],
        "short": facts["short"],
        "branch": facts["branch"],
        "dirty": facts["dirty"],
        "label": revision_label(facts),
        "subject": facts["subject"],
    }
    if extra:
        record.update(extra)
    return record


def write_record(path: Path, facts: dict[str, object] | None = None, **extra: object) -> Path:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    record = build_record(facts, dict(extra) or None)
    path.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    return path


def read_record(path: Path) -> dict | None:
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    return data if isinstance(data, dict) else None


def verify_header(path: Path, facts: dict[str, object] | None = None) -> tuple[int, str]:
    """Does the generated header name the revision that is here now?

    The header is a build input, not a build output, so it can go stale in exactly the way
    a cached value does: present, plausible, and describing a commit that has moved on.
    This is the assertion a CI job can make about a build that has not happened yet.
    """
    facts = facts or describe()
    expected = f'#define BROWSERX_GIT "{revision_label(facts)}"'
    try:
        text = Path(path).read_text(encoding="utf-8")
    except OSError:
        return EXIT_UNKNOWN, f"{path} does not exist — the revision was never generated"
    if expected not in text:
        found = [line for line in text.splitlines() if line.startswith("#define BROWSERX_GIT ")]
        return EXIT_STALE, (
            f"{path} says {found[0] if found else 'nothing'}, and this tree is "
            f"{revision_label(facts)} — rebuild")
    return EXIT_CURRENT, f"the revision header names {revision_label(facts)}"


def staleness(path: Path, facts: dict[str, object] | None = None) -> tuple[int, str]:
    """Is the thing described by `path` built from the code that is here now?

    Deliberately compares commits, not timestamps: a rebuild that changes nothing produces
    a new timestamp and is still current, and an edit that is never committed makes any
    comparison of times meaningless — so a dirty tree is reported as its own state instead
    of pretending to be current or stale.
    """
    facts = facts or describe()
    record = read_record(path)
    if record is None:
        if not Path(path).exists():
            return EXIT_UNKNOWN, f"no build record at {path} — build first, or pass the right path"
        return EXIT_UNKNOWN, f"{path} is not readable as a build record"
    built = str(record.get("commit") or UNKNOWN)
    here = str(facts.get("commit") or UNKNOWN)
    if here == UNKNOWN:
        return EXIT_UNKNOWN, "this folder is not a git checkout, so there is nothing to compare against"
    if built == UNKNOWN:
        return EXIT_UNKNOWN, "the build record has no commit (built outside a checkout)"
    if built != here:
        return EXIT_STALE, (
            f"built from {str(record.get('label') or built[:7])}, "
            f"the code here is {revision_label(facts)} — rebuild"
        )
    if record.get("dirty") or facts.get("dirty"):
        return EXIT_CURRENT, (
            f"same commit as the build ({revision_label(facts)}); "
            "the working tree has uncommitted changes, so rebuild if they matter"
        )
    return EXIT_CURRENT, f"current: {revision_label(facts)}"


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------


def render(facts: dict[str, object]) -> str:
    if not facts.get("checkout"):
        return (
            "  not a git checkout, so this build has no revision\n"
            "  (a build still works; it is simply stamped 'unknown')\n"
            "\n"
            "  to turn this folder into a checkout, keep what is already downloaded:\n"
            "    tools\\adopt_clone.cmd --yes"
        )
    lines = [
        f"  branch   : {facts.get('branch')}",
        f"  commit   : {facts.get('short')}  {facts.get('subject')}",
        f"  revision : {revision_label(facts)}",
        f"  tree     : {'dirty — uncommitted changes are in this build' if facts.get('dirty') else 'clean'}",
    ]
    upstream = facts.get("upstream") or ""
    if upstream:
        ahead, behind = facts.get("ahead") or 0, facts.get("behind") or 0
        if ahead or behind:
            lines.append(f"  vs {upstream}: {ahead} ahead, {behind} behind")
        else:
            lines.append(f"  vs {upstream}: up to date")
    else:
        lines.append("  upstream : none — git pull has nowhere to go")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Self-test
# ---------------------------------------------------------------------------


def self_test() -> int:
    checks: list[tuple[bool, str]] = []

    def expect(ok: bool, what: str) -> None:
        checks.append((bool(ok), what))

    with tempfile.TemporaryDirectory(prefix="browserx-rev-") as tmp:
        work = Path(tmp)
        # A folder that is not a checkout: the answer must be "unknown", not a guess, and
        # must not borrow an enclosing repository's identity either.
        facts = describe(work)
        expect(facts["checkout"] is False, "a plain folder is reported as not a checkout")
        expect(revision_label(facts) == UNKNOWN, "a plain folder's revision is unknown, never guessed")
        expect(facts["commit"] == UNKNOWN, "no commit is invented for a plain folder")

        # A real repository: two commits, so "stale" means something.
        def run(*args: str) -> None:
            subprocess.run(["git", *args], cwd=work, capture_output=True, check=True)

        run("init", "--quiet")
        run("config", "user.email", "self@test.invalid")
        run("config", "user.name", "self test")
        run("checkout", "--quiet", "-b", "work")
        (work / "generated").mkdir()
        (work / ".gitignore").write_text("generated/\n")
        (work / "a.txt").write_text("one\n")
        run("add", "a.txt", ".gitignore")
        run("commit", "--quiet", "-m", "first")
        first = describe(work)
        expect(first["checkout"] is True, "a real repository is recognised")
        expect(first["dirty"] is False, "a freshly committed tree is clean")
        expect(len(str(first["commit"])) == 40, "the full commit hash is recorded")
        expect(revision_label(first) == str(first["short"]), "a clean build is labelled with its short hash")

        record_path = work / "generated" / "build-info.json"
        write_record(record_path, first, exe="BrowserX.exe", size=1234)
        expect(record_path.is_file(), "the build record is written")
        saved = read_record(record_path)
        expect(saved is not None and saved.get("exe") == "BrowserX.exe", "extra build facts are kept in the record")
        code, text = staleness(record_path, first)
        expect(code == EXIT_CURRENT and "current" in text, "the same commit is reported current")

        # Uncommitted edit: the revision must say so, and the flags must carry it.
        (work / "a.txt").write_text("two\n")
        dirty = describe(work)
        expect(dirty["dirty"] is True, "an uncommitted edit marks the tree dirty")
        expect(revision_label(dirty) == str(dirty["short"]) + "+", "a dirty build is labelled with a trailing +")
        header = work / "generated" / "revision.h"
        expect(write_header(header, dirty) is True, "the revision header is written, creating its directory")
        text = header.read_text()
        expect(f'#define BROWSERX_GIT "{dirty["short"]}+"' in text, "the + reaches the compiler, not just the tool")
        expect("#define BROWSERX_GIT_DIRTY 1" in text, "a dirty tree is marked dirty in the header")
        expect(write_header(header, dirty) is False, "rewriting the same revision reports no change (no needless rebuild)")
        expect(describe(work)["dirty"] is True, "the fixture is dirty for a real reason, not because of the header")
        # The counter-check: an *ignored* build directory must not be mistaken for an edit.
        clean_check = work / "generated" / "only-ignored.txt"
        clean_check.write_text("build output\n")
        expect(describe(work)["dirty"] is True, "one ignored file does not clean a dirty tree (the edit is still there)")
        (work / "a.txt").write_text("one\n")  # undo the edit, leaving only build output
        expect(describe(work)["dirty"] is False,
               "build output inside an ignored directory does not make the tree dirty")
        stamp = header.stat().st_mtime_ns
        write_header(header, dirty)
        expect(header.stat().st_mtime_ns == stamp, "an unchanged header keeps its timestamp, so objects stay valid")
        code, text = staleness(record_path, dirty)
        expect(code == EXIT_CURRENT and "same commit" in text, "a dirty tree on the same commit is not called stale")

        # The header is an input: it has to agree with the tree, and CI asserts that.
        now = describe(work)
        write_header(header, now)
        code, text = verify_header(header, now)
        expect(code == EXIT_CURRENT, "the header written for this tree is accepted")
        code, text = verify_header(work / "not-there.h", describe(work))
        expect(code == EXIT_UNKNOWN, "a missing header is cannot-tell, not stale")

        # A newer commit: now the old record really is stale.
        (work / "b.txt").write_text("new work\n")
        run("add", "b.txt")
        run("commit", "--quiet", "-m", "second")
        newer = describe(work)
        expect(str(newer["commit"]) != str(first["commit"]), "the second commit is a different commit")
        code, text = staleness(record_path, newer)
        expect(code == EXIT_STALE and "rebuild" in text, "a record from an older commit is reported stale and says rebuild")
        code, text = verify_header(header, newer)
        expect(code == EXIT_STALE and "rebuild" in text,
               "a header left over from an older commit is caught before anything is built")
        write_header(header, newer)
        expect(f'#define BROWSERX_GIT "{newer["short"]}"' in header.read_text(), "a new commit rewrites the header")
        # A commit subject is arbitrary text: it must land in the header as a valid C string.
        nasty = dict(newer, subject='fix "quotes" and \\ backslash')
        write_header(header, nasty)
        line = [l for l in header.read_text().splitlines() if "SUBJECT" in l][0]
        expect('\\"' in line and "\\\\" in line, "quotes and backslashes in a commit subject are escaped")

        # Missing and malformed records are "cannot tell", which is not the same as stale.
        code, _ = staleness(work / "nope.json", newer)
        expect(code == EXIT_UNKNOWN, "a missing record is cannot-tell (3), not stale (10)")
        (work / "junk.json").write_text("{not json")
        code, _ = staleness(work / "junk.json", newer)
        expect(code == EXIT_UNKNOWN, "a malformed record is cannot-tell (3), not a crash")

        # A checkout nested inside another must not steal the outer one's identity.
        inner = work / "nested"
        inner.mkdir()
        expect(describe(inner)["checkout"] is False, "a subfolder of a checkout is not treated as its own checkout")
        expect(describe(work)["checkout"] is True, "the enclosing checkout is still recognised as itself")

        # Submodule-style gitlink, i.e. a folder that has a .git *file*: rev-parse resolves it,
        # so the check above must be about the top level, not the presence of a directory.
        expect(is_checkout(work) and not is_checkout(inner), "is_checkout discriminates by top level")

        # JSON is what other tools consume; it must be valid and stable.
        payload = json.loads(json.dumps(build_record(newer)))
        expect(set(payload) >= {"commit", "short", "branch", "dirty", "label"}, "the record has the fields tools rely on")

        # A branch with no upstream must not report a bogus ahead/behind.
        empty = work / "empty"
        empty.mkdir()
        subprocess.run(["git", "init", "--quiet"], cwd=empty, capture_output=True)
        bare = describe(empty)
        expect(bare["ahead"] == 0 and bare["behind"] == 0, "a repository with no commits reports 0/0, not an error")

    passed = sum(1 for ok, _ in checks if ok)
    for ok, what in checks:
        say(f"  {'✔' if ok else '✘'} {what}")
    say("")
    if passed == len(checks):
        say(f"  self-test passed ({passed} checks)")
        return EXIT_CURRENT
    say(f"  self-test FAILED ({len(checks) - passed} of {len(checks)})")
    return EXIT_FAILED


# ---------------------------------------------------------------------------


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(add_help=True, description="Which commit is this build?")
    parser.add_argument("--json", action="store_true", help="machine-readable facts")
    parser.add_argument("--header", metavar="FILE", help="write the C++ header that stamps the revision")
    parser.add_argument("--record", metavar="FILE", help="write a build record to FILE")
    parser.add_argument("--check", metavar="FILE", help="exit 10 when FILE describes an older commit")
    parser.add_argument("--verify-header", metavar="FILE",
                        help="exit 10 when the generated revision header is stale")
    parser.add_argument("--label", action="store_true", help="print just the revision label")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()

    facts = describe()

    if args.label:
        say(revision_label(facts))
        return EXIT_CURRENT
    if args.json:
        say(json.dumps(facts, indent=2))
        return EXIT_CURRENT
    if args.header:
        path = Path(args.header)
        changed = write_header(path, facts)
        # Quiet by default: this runs from a build recipe, once per compile, and a line of
        # output per file would bury the compiler's own messages.
        if changed:
            say(f"  revision {revision_label(facts)}")
        return EXIT_CURRENT
    if args.record:
        path = write_record(Path(args.record), facts)
        say(f"  build recorded: {path.relative_to(REPO_ROOT) if path.is_relative_to(REPO_ROOT) else path} "
            f"({revision_label(facts)})")
        return EXIT_CURRENT
    if args.verify_header:
        code, text = verify_header(Path(args.verify_header), facts)
        say(f"  {'✔' if code == EXIT_CURRENT else '✘'} {text}")
        return code
    if args.check:
        code, text = staleness(Path(args.check), facts)
        say(f"  {'✔' if code == EXIT_CURRENT else '✘' if code == EXIT_STALE else '?'} {text}")
        return code

    say("")
    say(render(facts))
    say("")
    return EXIT_CURRENT


if __name__ == "__main__":
    # Windows console code pages cannot encode ✔ or →; without this a tool dies while
    # reporting something else. Best effort: it never makes a working run fail.
    import utf8_console  # same directory: sys.path[0] when run as a script
    utf8_console.enable()
    sys.exit(main(sys.argv[1:]))
