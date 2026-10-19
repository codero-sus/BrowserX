#!/usr/bin/env python3
# BrowserX — (c) 2026 codero-sus. All rights reserved.
# Personal, non-commercial use only. No modification, no redistribution or resale.
# Source-available, not open source — see LICENSE. https://the-codero.web.app
"""BrowserX — turn a downloaded folder into a git checkout, so updates are `git pull`.

Why this exists: the project ships as a GitHub repository, but the easy way to get it is
"Download ZIP", which produces a folder that looks identical and cannot be updated. From
then on every fix means another 40 MB download, and `tools\\update.cmd` cannot help because
it is not a checkout. This tool converts that folder in place:

    tools\\adopt_clone.cmd               what would change, touching nothing
    tools\\adopt_clone.cmd --yes         do it

What it does, in order:

    git init                      a repository in the folder you already have
    git remote add origin URL
    git fetch origin BRANCH
    git reset --hard FETCH_HEAD   the repository's files replace yours
    git checkout -B BRANCH        on the branch, tracking the remote

What it deliberately keeps: everything the repository does not track. The CEF download
(`.cef-deps`, about 1.4 GB), build outputs (`build`, `out`), and your profile inside
`out/profile` are untouched — a `git reset --hard` only writes the paths the commit knows
about. That is the whole point: you should not download 200 MB of CEF again because you
moved from ZIP to git.

What it replaces, loudly: every file the repository tracks. If you edited one of those,
the edit goes. The dry run lists them by name before anything happens.

Exit codes: 0 done (or nothing to do), 1 failed, 3 cannot start (no git, already a
checkout, branch not on the remote).

    python3 tools/adopt_clone.py --self-test
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_URL = "https://github.com/codero-sus/BrowserX.git"
# The branch carrying the work. `main` is still the repository's first commit, so cloning
# or adopting without naming this branch produces a folder with almost nothing in it.
DEFAULT_BRANCH = "arena/419ef7ef-mybrowser"

EXIT_OK = 0
EXIT_FAILED = 1
EXIT_CANNOT_START = 3

# What a person is most likely to have inside the folder before it is a checkout. Listed
# so the dry run can promise, by name, that it will survive.
KEPT_BY_DEFAULT = (".cef-deps", "build", "out", ".cef-headers", "node_modules", ".git")

GIT_INSTALL_ADVICE = (
    "install Git for Windows from https://git-scm.com/download/win\n"
    "  then close this window and open a new one, so the new PATH is picked up")


class AdoptError(Exception):
    """A failure with a message meant for the person running this."""


# ---------------------------------------------------------------------------
# git, invoked plainly
# ---------------------------------------------------------------------------
def find_git() -> str:
    git = shutil.which("git")
    if git is None:
        raise AdoptError(f"git was not found on PATH.\n  {GIT_INSTALL_ADVICE}")
    return git


def git(arguments: list[str], cwd: Path | None = None, capture: bool = True,
        allow_failure: bool = False) -> tuple[int, str]:
    git_exe = find_git()
    completed = subprocess.run([git_exe, *arguments],
                               cwd=str(cwd) if cwd else None,
                               stdout=subprocess.PIPE if capture else None,
                               stderr=subprocess.STDOUT if capture else None,
                               text=True, encoding="utf-8", errors="replace")
    output = (completed.stdout or "") if capture else ""
    if completed.returncode != 0 and not allow_failure:
        raise AdoptError(f"git {' '.join(arguments)} failed:\n{output.strip()}")
    return completed.returncode, output


def git_bytes(arguments: list[str], cwd: Path | None = None) -> bytes:
    """git, returning raw bytes - for blobs, which are not text.

    Decoding the output of `git cat-file blob` would corrupt every binary file in the
    repository and make the comparison lie about it.
    """
    git_exe = find_git()
    completed = subprocess.run([git_exe, *arguments],
                               cwd=str(cwd) if cwd else None,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if completed.returncode != 0:
        raise AdoptError(f"git {' '.join(arguments)} failed:\n"
                         f"{completed.stderr.decode('utf-8', 'replace').strip()}")
    return completed.stdout


def is_checkout(directory: Path) -> bool:
    return (directory / ".git").exists()


def remote_head(url: str, branch: str) -> tuple[str, dict[str, bytes]]:
    """Fetch `branch` into a throwaway repository and return (commit, path -> content).

    The throwaway directory is what makes the dry run honest: it is a real fetch of the
    real remote, so the report describes the actual target rather than a guess, and
    nothing in the folder being adopted is touched while it is produced.
    """
    scratch = Path(tempfile.mkdtemp(prefix="browserx-adopt-"))
    try:
        git_dir = scratch / "probe.git"
        git(["init", "--quiet", "--bare", str(git_dir)])
        # --depth 1: the probe only needs the tip tree, and it also lets the probe work
        # when the folder it is comparing against came from a shallow clone (a shallow
        # source refuses to update a non-shallow ref, which reads as "branch not found").
        git([f"--git-dir={git_dir}", "fetch", "--quiet", "--depth", "1", url, branch])
        code, output = git([f"--git-dir={git_dir}", "rev-parse", "FETCH_HEAD"],
                           allow_failure=True)
        if code != 0:
            raise AdoptError(f"could not resolve {branch} at {url}:\n{output.strip()}")
        commit = output.strip()
        _, listing = git([f"--git-dir={git_dir}", "ls-tree", "-r", "-z", "FETCH_HEAD"])
        files: dict[str, bytes] = {}
        for entry in listing.split("\0"):
            if not entry.strip():
                continue
            meta, path = entry.split("\t", 1)
            _, kind, sha = meta.split()
            if kind != "blob":
                continue
            files[path] = git_bytes([f"--git-dir={git_dir}", "cat-file", "blob", sha])
        return commit, files
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


# ---------------------------------------------------------------------------
# The plan
# ---------------------------------------------------------------------------
def comparable(data: bytes) -> bytes:
    """Bytes with line endings normalised, for deciding whether two copies differ.

    A Windows checkout and a repository blob can differ only in CRLF vs LF and still be
    the same file - .gitattributes and core.autocrlf exist precisely so they are. Comparing
    raw bytes would report every text file as "will be replaced", which would make the dry
    run frightening and wrong. Normalising both sides keeps the comparison honest without
    ever hiding a real change.
    """
    return data.replace(b"\r\n", b"\n")


def make_plan(workdir: Path, url: str, branch: str) -> dict:
    commit, files = remote_head(url, branch)
    same: list[str] = []
    changed: list[str] = []
    added: list[str] = []
    for path, content in files.items():
        local = workdir / path
        if not local.is_file():
            added.append(path)
            continue
        try:
            if comparable(local.read_bytes()) == comparable(content):
                same.append(path)
            else:
                changed.append(path)
        except OSError:
            changed.append(path)          # unreadable: it will be replaced whatever it is

    tracked = set(files)
    kept: list[tuple[str, int]] = []
    for entry in sorted(workdir.iterdir()):
        if entry.name in (".git",) or entry.name in tracked:
            continue
        if any(entry.name == path or path.startswith(entry.name + "/") for path in tracked):
            continue                       # part of the repository's own tree
        size = 0
        if entry.is_dir():
            for item in entry.rglob("*"):
                try:
                    if item.is_file():
                        size += item.stat().st_size
                except OSError:
                    pass
        else:
            try:
                size = entry.stat().st_size
            except OSError:
                size = 0
        kept.append((entry.name, size))

    return {"commit": commit, "files": len(files), "same": same, "changed": changed,
            "added": added, "kept": kept, "branch": branch, "url": url}


def human_bytes(count: int) -> str:
    value = float(count)
    for unit in ("B", "KB", "MB", "GB"):
        if value < 1024 or unit == "GB":
            return f"{value:.0f} {unit}" if unit == "B" else f"{value:.1f} {unit}"
        value /= 1024
    return f"{count} B"


def report(plan: dict, verbose: bool = True) -> None:
    print(f"  branch     : {plan['branch']}")
    print(f"  remote     : {plan['url']}")
    print(f"  target     : {plan['commit'][:12]}")
    print(f"  tracked    : {plan['files']} file(s) in the repository")
    print(f"  unchanged  : {len(plan['same'])} already identical")
    print(f"  updated    : {len(plan['changed'])} will be replaced")
    print(f"  new        : {len(plan['added'])} will be added")
    if plan["changed"] and verbose:
        for path in plan["changed"][:12]:
            print(f"                 {path}")
        if len(plan["changed"]) > 12:
            print(f"                 ... and {len(plan['changed']) - 12} more")
    if plan["kept"]:
        print("  kept       : not tracked by the repository, so not touched")
        for name, size in plan["kept"]:
            print(f"                 {name:<16} {human_bytes(size)}")
    else:
        print("  kept       : nothing untracked here")


# ---------------------------------------------------------------------------
# Doing it
# ---------------------------------------------------------------------------
def adopt(workdir: Path, url: str, branch: str, plan: dict | None = None) -> dict:
    if is_checkout(workdir):
        raise AdoptError(f"{workdir} is already a git checkout.\n"
                         "  update it instead:  tools\\update.cmd")
    if plan is None:
        plan = make_plan(workdir, url, branch)

    git(["init", "--quiet"], cwd=workdir)
    _, remotes = git(["remote"], cwd=workdir)
    if "origin" in remotes.split():
        git(["remote", "set-url", "origin", url], cwd=workdir)
    else:
        git(["remote", "add", "origin", url], cwd=workdir)
    git(["fetch", "--quiet", "origin", branch], cwd=workdir)
    # --hard is deliberate: the folder's tracked paths are an extracted copy of some
    # older commit, and anything else at those paths would block the checkout.
    git(["reset", "--quiet", "--hard", "FETCH_HEAD"], cwd=workdir)
    git(["checkout", "--quiet", "-B", branch], cwd=workdir)
    git(["branch", "--quiet", "--set-upstream-to", f"origin/{branch}", branch], cwd=workdir)
    return plan


def verify(workdir: Path, branch: str) -> list[str]:
    """What still looks wrong after adopting. Empty means it worked."""
    problems: list[str] = []
    code, head = git(["rev-parse", "HEAD"], cwd=workdir, allow_failure=True)
    code2, remote = git(["rev-parse", f"origin/{branch}"], cwd=workdir, allow_failure=True)
    if code != 0 or code2 != 0:
        problems.append("git cannot resolve HEAD or the remote branch")
    elif head.strip() != remote.strip():
        problems.append(f"HEAD ({head.strip()[:12]}) is not the remote branch "
                        f"({remote.strip()[:12]})")
    code, status = git(["status", "--porcelain"], cwd=workdir, allow_failure=True)
    if code != 0:
        problems.append("git status failed")
    elif status.strip():
        problems.append("the working tree is not clean after adopting:\n"
                        + "\n".join(status.splitlines()[:8]))
    code, upstream = git(["rev-parse", "--abbrev-ref", f"{branch}@{{upstream}}"],
                         cwd=workdir, allow_failure=True)
    if code != 0:
        problems.append("the branch has no upstream, so `git pull` would not work")
    return problems


# ---------------------------------------------------------------------------
# Self-test
# ---------------------------------------------------------------------------
def self_test() -> int:
    failures = 0

    def expect(condition: bool, label: str) -> None:
        nonlocal failures
        print(f"    {'✔' if condition else '✘'} {label}")
        failures += not condition

    print("  self-test")
    if shutil.which("git") is None:
        print("    ✘ git is not installed here, so nothing can be verified")
        print("\n  1 self-test failure(s)\n")
        return EXIT_FAILED

    branch = git(["rev-parse", "--abbrev-ref", "HEAD"], cwd=REPO_ROOT)[1].strip()
    scratch = Path(tempfile.mkdtemp(prefix="browserx-adopt-test-"))
    try:
        # A fake "remote" holding this repository's files at HEAD, built from git archive
        # so it is a normal (non-shallow) repository like the one on GitHub. Cloning the
        # checkout itself would copy this sandbox's shallow state and fail for reasons that
        # have nothing to do with adopting.
        source = scratch / "source"
        source.mkdir()
        archive = subprocess.run([find_git(), "archive", "--format=tar", "HEAD"],
                                 cwd=str(REPO_ROOT), stdout=subprocess.PIPE, check=True)
        subprocess.run(["tar", "-x", "-C", str(source)], input=archive.stdout, check=True)
        git(["init", "--quiet", "-b", branch], cwd=source)
        git(["add", "--all"], cwd=source)
        git(["-c", "user.name=BrowserX self-test", "-c", "user.email=test@example.invalid",
             "commit", "--quiet", "-m", "self-test"], cwd=source)
        remote = scratch / "remote.git"
        git(["clone", "--quiet", "--bare", str(source), str(remote)])
        workdir = scratch / "Downloads" / "BrowserX"
        (workdir / "tools").mkdir(parents=True)
        (workdir / "cef.lock.json").write_text('{"version": "the-beta-we-do-not-want"}\n')
        (workdir / "BUILDING.md").write_text("an older copy of the docs\n")
        # The identical copy must come from the *commit*, not the working tree: a ZIP
        # extract matches some commit, and a developer's uncommitted edit would otherwise
        # make this test fail for a reason that has nothing to do with adopting.
        (workdir / "README.md").write_bytes(git_bytes(["show", "HEAD:README.md"], cwd=REPO_ROOT))
        sizeable = workdir / ".cef-deps" / "cef_binary_156_windows64_beta" / "libcef.dll"
        sizeable.parent.mkdir(parents=True)
        sizeable_bytes = b"pretend dll" * 1000
        sizeable.write_bytes(sizeable_bytes)
        (workdir / "build").mkdir()
        (workdir / "build" / "thing.obj").write_bytes(b"obj")

        plan = make_plan(workdir, str(remote), branch)
        expect(plan["commit"], "a real fetch of a real branch resolves a commit")
        expect("cef.lock.json" in plan["changed"],
               "the CEF pin that was overwritten before is reported as replaced")
        expect("README.md" in plan["same"], "an identical file is not reported as changed")
        expect(".gitignore" in plan["added"],
               "files the folder does not have are reported as new")
        expect(any(name == ".cef-deps" for name, _ in plan["kept"]),
               "the CEF download is listed as kept")
        expect(not is_checkout(workdir), "the dry run creates no repository")

        adopt(workdir, str(remote), branch, plan)
        expect(is_checkout(workdir), "adopting creates the repository")
        expect((workdir / "cef.lock.json").read_bytes()
               == (REPO_ROOT / "cef.lock.json").read_bytes(),
               "tracked files now match the repository (the beta pin is gone)")
        expect(sizeable.is_file() and sizeable.read_bytes() == sizeable_bytes,
               "the CEF download was not touched - no re-download needed")
        expect((workdir / "build" / "thing.obj").is_file(), "build outputs are kept")
        expect(verify(workdir, branch) == [],
               "afterwards HEAD matches the remote, the tree is clean and has an upstream")
        expect(git(["remote", "get-url", "origin"], cwd=workdir)[1].strip() == str(remote),
               "origin points at the remote that was adopted")
        expect(git(["rev-parse", "--abbrev-ref", f"{branch}@{{upstream}}"],
                   cwd=workdir)[1].strip() == f"origin/{branch}",
               "the branch tracks the remote, so `git pull` and update.cmd both work")

        # Running it twice must be a no-op with a useful message, not a mess.
        try:
            adopt(workdir, str(remote), branch)
            expect(False, "adopting an existing checkout is refused")
        except AdoptError as error:
            expect("already a git checkout" in str(error),
                   "adopting an existing checkout is refused, and says why")

        # A branch that does not exist must fail before anything is written.
        fresh = scratch / "fresh"
        fresh.mkdir()
        try:
            make_plan(fresh, str(remote), "no-such-branch")
            expect(False, "a missing branch is refused")
        except AdoptError as error:
            expect("could not resolve" in str(error) or "failed" in str(error),
                   "a branch that is not on the remote is refused with git's own words")

        # Nothing is adopted when the branch is wrong: the folder stays as it was.
        expect(not is_checkout(fresh), "a failed plan leaves the folder untouched")
    finally:
        shutil.rmtree(scratch, ignore_errors=True)

    print()
    if failures:
        print(f"  {failures} self-test failure(s)\n")
        return EXIT_FAILED
    print("  self-test passed\n")
    return EXIT_OK


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="tools/adopt_clone.py",
        description="Turn a downloaded copy of BrowserX into a git checkout, so updates "
                    "arrive with git pull instead of another download.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    parser.add_argument("--yes", action="store_true",
                        help="do it; without this, only report what would change")
    parser.add_argument("--url", default=DEFAULT_URL, help=f"repository (default: {DEFAULT_URL})")
    parser.add_argument("--branch", default=DEFAULT_BRANCH,
                        help=f"branch to adopt (default: {DEFAULT_BRANCH})")
    parser.add_argument("--dir", default="", help="the folder to adopt (default: this checkout)")
    parser.add_argument("--self-test", action="store_true", help="test this tool offline")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()

    workdir = Path(args.dir).expanduser().resolve() if args.dir else REPO_ROOT
    if not workdir.is_dir():
        print(f"  ✘ {workdir} is not a directory\n")
        return EXIT_CANNOT_START

    print()
    print("  BrowserX — adopt this folder into git")
    print("  " + "-" * 60)

    try:
        if is_checkout(workdir):
            print(f"  {workdir} is already a git checkout - nothing to do.")
            print("  update it with:  tools\\update.cmd")
            print()
            return EXIT_OK
        plan = make_plan(workdir, args.url, args.branch)
        report(plan)
        print()
        if not args.yes:
            print("  nothing has been changed. To finish:")
            print("    tools\\adopt_clone.cmd --yes")
            print()
            return EXIT_OK

        print("  adopting...")
        adopt(workdir, args.url, args.branch, plan)
        problems = verify(workdir, args.branch)
        print()
        if problems:
            print("  ✘ it ran, but the result does not look right:")
            for problem in problems:
                print(f"      {problem}")
            print()
            return EXIT_FAILED
        print("  ✔ this folder is now a git checkout")
        print(f"    branch : {args.branch}")
        print(f"    next   : tools\\update.cmd --check   (is there anything new?)")
        print("             tools\\update.cmd           (pull, rebuild, verify)")
        print()
        return EXIT_OK
    except AdoptError as error:
        print()
        print(f"  ✘ {error}")
        print()
        return EXIT_CANNOT_START


if __name__ == "__main__":
    # Windows console code pages cannot encode ✔ or →; without this a tool dies while
    # reporting something else. Best effort: it never makes a working run fail.
    import utf8_console  # same directory: sys.path[0] when run as a script
    utf8_console.enable()
    sys.exit(main(sys.argv[1:]))
