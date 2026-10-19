#!/usr/bin/env python3
# BrowserX — (c) 2026 codero-sus. All rights reserved.
# Personal, non-commercial use only. No modification, no redistribution or resale.
# Source-available, not open source — see LICENSE. https://the-codero.web.app
"""BrowserX — the updater: pull, rebuild, verify, and say what happened.

One command per platform, both landing here:

    tools/update.sh              Linux (also works anywhere with bash)
    tools\\update.cmd            Windows (runs this inside WSL; see below)
    make update                  same thing through the Makefile

Why a script and not two shell one-liners:

  * **`git pull --ff-only` is the easy half.** The hard half is knowing whether the
    machine can rebuild what it just pulled, and refusing to pretend it did. This asks
    `tools/check_build_requirements.py` what this machine supports and builds exactly
    that: the full browser when CEF and X11/GTK are present, the core, tests and tools
    when they are not.
  * **A failed pull should not cost anyone an afternoon.** The tree is checked first: a
    dirty working tree is refused with an explanation (`--stash` to be explicit), the
    incoming commits are listed before they are applied, and the previous commit is
    recorded so `--rollback` works.
  * **The profile lives in the repository directory.** `git clean -fdx` - the reflex of
    every "update" script - would delete browsing history and bookmarks. Nothing here
    ever runs git clean, and `--rollback` is opt-in and says what it does.

Exit codes: 0 updated or already up to date; 10 an update is available (`--check`);
1 anything failed. `--json` writes the machine-readable summary to stdout and puts the
build log on stderr, so scripts can read one and humans the other.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TOOLS_DIR = REPO_ROOT / "tools"
STATE_FILE = REPO_ROOT / "out" / "update" / "state.json"

# What --check exits with when there is something to fetch. Distinct from 0 (nothing to
# do) and 1 (the check itself failed), so a cron job or a wrapper can tell them apart.
EXIT_UPDATE_AVAILABLE = 10


# ---------------------------------------------------------------------------
# Output: readable, and correct on a console that cannot print ✔
# ---------------------------------------------------------------------------
class Out:
    """Human output that degrades honestly.

    Windows consoles default to a legacy code page where '✔' becomes mojibake, so the
    marks fall back to ASCII when the stream cannot encode them. The text never changes.
    """

    def __init__(self, quiet: bool = False, json_mode: bool = False) -> None:
        self.quiet = quiet
        self.json_mode = json_mode
        self.stream = sys.stderr if json_mode else sys.stdout
        try:
            self.marks = {"ok": "✔", "info": "·", "warn": "›", "fail": "✘", "step": "→"}
            "".join(self.marks.values()).encode(self.stream.encoding or "utf-8")
        except (UnicodeEncodeError, LookupError, TypeError):
            self.marks = {"ok": "OK", "info": "-", "warn": ">", "fail": "X", "step": "->"}

    def say(self, text: str = "") -> None:
        if not self.quiet:
            print(text, file=self.stream, flush=True)

    def step(self, text: str) -> None:
        print(f"  {self.marks['step']} {text}", file=self.stream, flush=True)

    def mark(self, kind: str, name: str, detail: str = "") -> None:
        line = f"    {self.marks[kind]} {name}"
        if detail:
            line += f"  {detail}"
        print(line, file=self.stream, flush=True)

    def warn(self, text: str) -> None:
        print(f"    {self.marks['warn']} {text}", file=self.stream, flush=True)


def run(args: list[str], cwd: Path = REPO_ROOT, capture: bool = True,
        stream: bool = False, timeout: int | None = None) -> tuple[int, str]:
    """Runs a command; never raises. `stream=True` lets a build show its own output."""
    try:
        if stream:
            target = sys.stderr if _JSON_MODE else None
            result = subprocess.run(args, cwd=str(cwd), stdout=target, stderr=target,
                                    text=True, timeout=timeout)
            return result.returncode, ""
        result = subprocess.run(args, cwd=str(cwd), capture_output=True, text=True,
                                timeout=timeout)
        return result.returncode, (result.stdout or "") + (result.stderr or "")
    except FileNotFoundError:
        return 127, f"{args[0]}: not found"
    except (OSError, subprocess.SubprocessError) as error:
        return 1, str(error)


_JSON_MODE = False


# ---------------------------------------------------------------------------
# Pure decision logic - every function here is exercised by --self-test
# ---------------------------------------------------------------------------
def decide_plan(core_ok: bool, browser_ok: bool, cef_missing: bool, args) -> list[str]:
    """The build steps to run, in order. Kept pure so it can be tested without a machine.

    * core unusable            -> nothing to build; the update stops after the pull
    * CEF missing (and no --deps) -> core targets only, with the reason printed
    * otherwise                -> the full browser build
    """
    if args.no_build:
        return []
    if not core_ok:
        return []
    if browser_ok:
        return ["browser"]
    if cef_missing and not args.deps:
        return ["core"]
    if cef_missing and args.deps:
        return ["deps", "browser"]
    return ["core"]


def decide_verification(core_ok: bool, headers_present: bool, args) -> list[str]:
    """What to run afterwards to prove the update is not broken."""
    if args.no_verify or args.no_build:
        return []
    checks = []
    if args.full and core_ok:
        checks.append("check-compile")
    if core_ok:
        checks.append("test")
    return checks


def git_error(text: str, max_lines: int = 3) -> str:
    """The part of git's output worth showing.

    Taking 'the last line' produced "and the repository exists." for a failed pull - a
    fragment of "Please make sure you have the correct access rights and the repository
    exists", which tells the reader nothing. Prefer the lines that say what went wrong.
    """
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    interesting = [line for line in lines if line.startswith(("fatal:", "error:", "hint:"))
                   or "refusing" in line.lower()]
    chosen = interesting or lines[-max_lines:]
    return "\n      ".join(chosen[:max_lines])


def tree_state(porcelain: str) -> dict:
    """Reads `git status --porcelain` output. Untracked files are reported separately:
    they do not block a fast-forward pull, and treating them as dirt would make the
    updater refuse on a tree that is perfectly fine to update."""
    modified, untracked = [], []
    for line in porcelain.splitlines():
        if not line.strip():
            continue
        code, path = line[:2], line[3:].strip()
        (untracked if code.startswith("??") else modified).append(path)
    return {"modified": modified, "untracked": untracked, "dirty": bool(modified)}


def summarize_incoming(log_output: str, max_lines: int = 8) -> list[str]:
    """`git log --oneline` output -> the list to print. Empty when nothing is coming."""
    lines = [line.strip() for line in log_output.splitlines() if line.strip()]
    return lines[:max_lines]


def load_state(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def save_state(path: Path, state: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(".tmp")
    temp.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
    temp.replace(path)


# ---------------------------------------------------------------------------
# Git facts
# ---------------------------------------------------------------------------
class Repo:
    def __init__(self, out: Out) -> None:
        self.out = out

    def git(self, *args: str, capture: bool = True) -> tuple[int, str]:
        return run(["git", *args], capture=capture)

    def is_repo(self) -> bool:
        code, _ = self.git("rev-parse", "--git-dir")
        return code == 0

    def branch(self) -> str:
        code, text = self.git("rev-parse", "--abbrev-ref", "HEAD")
        return text.strip() if code == 0 else ""

    def head(self) -> str:
        code, text = self.git("rev-parse", "HEAD")
        return text.strip() if code == 0 else ""

    def upstream(self, branch: str) -> str:
        code, text = self.git("rev-parse", "--abbrev-ref", f"{branch}@{{u}}")
        return text.strip() if code == 0 else ""

    def remote_ref(self, branch: str) -> str:
        """Where a pull would come from when the branch has no upstream yet."""
        code, text = self.git("config", f"branch.{branch}.remote")
        remote = text.strip() if code == 0 and text.strip() else "origin"
        return f"{remote}/{branch}"

    def status(self) -> dict:
        _, text = self.git("status", "--porcelain")
        return tree_state(text)

    def log_range(self, since: str, until: str) -> list[str]:
        code, text = self.git("log", "--oneline", "--no-decorate", f"{since}..{until}")
        return summarize_incoming(text) if code == 0 else []


# ---------------------------------------------------------------------------
# Requirements: reuse the checker rather than re-deciding what a machine can do
# ---------------------------------------------------------------------------
def load_requirements_module():
    path = REPO_ROOT / "tools" / "check_build_requirements.py"
    if not path.is_file():
        return None
    spec = importlib.util.spec_from_file_location("browserx_requirements", path)
    if spec is None or spec.loader is None:
        return None
    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)
    except Exception:  # pragma: no cover - a broken checker must not break updating
        return None
    return module


def machine_capability(out: Out) -> dict:
    """{core_ok, browser_ok, cef_missing, headers_present} from the requirements checker.

    When the checker cannot be loaded the answer is deliberately pessimistic: the updater
    then builds nothing and says why, instead of guessing and reporting success.
    """
    module = load_requirements_module()
    if module is None:
        out.warn("tools/check_build_requirements.py is missing: cannot decide what to build")
        return {"core_ok": False, "browser_ok": False, "cef_missing": True,
                "headers_present": False, "known": False}
    report = module.Report()
    module.check_core(report)
    if sys.platform.startswith("win"):
        # Windows has no `make`, no X11 and no GTK: the browser is built with MSVC by
        # tools/build_windows.py. Asking the Linux browser group about this machine would
        # answer "missing X11/GTK/display" and the updater would then refuse to build
        # anything, which is exactly the wrong conclusion here.
        module.check_windows(report)
    else:
        module.check_browser(report)
    group = "windows" if sys.platform.startswith("win") else "browser"
    headers_present = any(row["name"] == "CEF headers" and row["state"] == "ok"
                          for row in report.of("browser"))
    # The CEF row is named differently in the two groups: "windows64" is what a Windows
    # build needs, "full binary" is what a Linux one needs, and both mean the same thing
    # to the decision below.
    cef_missing = any(row["state"] == "missing" and row["name"].startswith("CEF")
                      for row in report.of(group))
    return {"core_ok": report.ok("core"), "browser_ok": report.ok(group),
            "cef_missing": cef_missing, "headers_present": headers_present, "known": True}


# ---------------------------------------------------------------------------
# The steps
# ---------------------------------------------------------------------------
def do_fetch(repo: Repo, branch: str, upstream: str, out: Out) -> tuple[bool, list[str]]:
    """Fetches and reports what is coming. Returns (ok, incoming commit lines)."""
    remote = upstream.split("/", 1)[0] if "/" in upstream else "origin"
    out.step(f"fetching {remote}")
    code, text = repo.git("fetch", "--prune", remote)
    if code != 0:
        out.warn("git fetch failed:")
        out.warn("  " + git_error(text))
        out.warn("offline? the update is being done from what is already fetched")
        return False, []
    local = repo.head()
    code, remote_head = repo.git("rev-parse", upstream)
    if code != 0:
        out.warn(f"cannot resolve {upstream}")
        return False, []
    incoming = repo.log_range(local, upstream)
    return True, incoming


def do_pull(repo: Repo, branch: str, upstream: str, args, out: Out,
            stashed_out: dict | None = None) -> tuple[bool, str]:
    """Fast-forward only. A pull that would merge or rewrite history is refused: an
    updater is not the right place to resolve a conflict."""
    if args.no_pull:
        out.step("skipping the pull (--no-pull)")
        return True, repo.head()

    if stashed_out is not None:
        stashed_out["stashed"] = False
    dirty = repo.status()
    if dirty["dirty"]:
        if not args.stash:
            out.warn(f"{len(dirty['modified'])} modified file(s) in the working tree:")
            for path in dirty["modified"][:6]:
                out.warn(f"  {path}")
            out.warn("refusing to update: a pull over local changes can lose them.")
            out.warn("commit or stash them yourself, or re-run with --stash to let the updater")
            out.warn("stash them (and print the command that brings them back).")
            return False, repo.head()
        out.step("stashing local changes (--stash)")
        code, text = repo.git("stash", "push", "--include-untracked", "--message",
                              "browserx-update: local changes before the pull")
        if code == 0 and stashed_out is not None:
            stashed_out["stashed"] = True
        else:
            out.warn("git stash failed:")
            out.warn("  " + git_error(text))
            return False, repo.head()

    out.step(f"pulling {upstream} (fast-forward only)")
    # `git pull --ff-only origin/foo/bar` is NOT a pull of that ref: git reads the first
    # argument as a *remote name* and fails with "does not appear to be a git repository".
    # With an upstream configured, the no-argument form is the right one; without one, name
    # the remote and the branch it holds.
    if repo.upstream(repo.branch()):
        code, text = repo.git("pull", "--ff-only", "--quiet")
    else:
        remote, _, remote_branch = upstream.partition("/")
        code, text = repo.git("pull", "--ff-only", "--quiet", remote, remote_branch)
    if code != 0:
        out.warn("the pull did not happen:")
        out.warn("  " + git_error(text))
        out.warn("a fast-forward fails when this branch has local commits, or when the remote")
        out.warn("was rewritten. Look at it, then choose:  git log --oneline --graph --all")
        return False, repo.head()
    return True, repo.head()


def is_windows() -> bool:
    return sys.platform.startswith("win")


def build_command(step: str, args, windows: bool | None = None) -> tuple[str, list[str]]:
    """The command that performs one build step, and the label to print for it.

    Kept pure and separate from running it: the Windows choice has to be testable on
    whatever machine happens to be running the tests, and it is the kind of decision that
    is otherwise only exercised on the platform you cannot easily check.
    """
    if windows is None:
        windows = is_windows()
    if windows:
        # One command on Windows: build_windows.py finds MSVC itself, so there is no
        # toolchain to set up first. --stage puts libcef.dll next to the exe, because an
        # updated exe beside a missing DLL is not an update.
        command = [sys.executable, str(TOOLS_DIR / "build_windows.py"), "--stage",
                   "--jobs", str(args.jobs or (os.cpu_count() or 2))]
        return "build_windows.py --stage", command
    jobs = ["-j", str(args.jobs)] if args.jobs else []
    targets = ["all"] if step == "browser" else ["pages"]
    return "make " + " ".join(targets), ["make", *jobs, *targets]


def do_build(plan: list[str], args, out: Out) -> list[dict]:
    """Runs the planned build steps, streaming their output. Stops at the first failure."""
    results = []
    jobs = ["-j", str(args.jobs)] if args.jobs else []
    for step in plan:
        if step == "deps":
            out.step("downloading and verifying CEF (about 200 MB, one time)")
            code, _ = run([sys.executable, str(REPO_ROOT / "tools" / "fetch_cef.py")],
                          stream=True, timeout=None)
            results.append({"step": "make deps", "ok": code == 0})
            if code != 0:
                break
            continue
        label, command = build_command(step, args, windows=is_windows())
        out.step("building: " + label)
        code, _ = run(command, stream=True, timeout=None)
        results.append({"step": label, "ok": code == 0})
        if code != 0:
            break
    return results


def do_verify(checks: list[str], args, out: Out) -> list[dict]:
    results = []
    for check in checks:
        if sys.platform.startswith("win"):
            # `make test` has no Windows equivalent here: the unit tests compile without CEF
            # and run as a separate step only on the POSIX side, so on Windows the honest
            # verification is that the build linked and the exe exists - reported as such
            # rather than as a test run that did not happen.
            exe = REPO_ROOT / "out" / "BrowserX.exe"
            ok = exe.is_file() and exe.stat().st_size > 0
            out.step(f"verifying: {exe.relative_to(REPO_ROOT)} exists")
            results.append({"step": "out/BrowserX.exe exists", "ok": ok})
            results.append(verify_revision(out))
            continue
        out.step(f"verifying: make {check}")
        code, _ = run(["make", check], stream=True, timeout=None)
        results.append({"step": f"make {check}", "ok": code == 0})
        if code != 0:
            break
    results.append(verify_revision(out))
    return results


def verify_revision(out: "Out", record: "Path | None" = None) -> dict:
    """Did the thing we just built come from the commit we just pulled?

    An update that pulls code and then reports success while the binary on disk is still
    the old one is the failure this catches. It reads out/build-info.json, which the build
    writes only after a successful link, and asks tools/revision.py to compare it with HEAD.
    Cannot-tell (no record yet, or no checkout) is reported as its own outcome rather than
    as a pass: an update should not claim provenance it cannot check.
    """
    record = record or REPO_ROOT / "out" / "build-info.json"
    sys.path.insert(0, str(REPO_ROOT / "tools"))
    try:
        import revision
    except ImportError:
        out.warn("could not load tools/revision.py, so the build record was not checked")
        return {"step": "build matches the pulled commit", "ok": True, "skipped": True}
    code, text = revision.staleness(record, revision.describe())
    if code == revision.EXIT_CURRENT:
        out.mark("ok", "revision", text)
        return {"step": "build matches the pulled commit", "ok": True}
    if code == revision.EXIT_STALE:
        out.mark("fail", "revision", text)
        return {"step": "build matches the pulled commit", "ok": False}
    out.mark("info", "revision", text)
    return {"step": "build matches the pulled commit", "ok": True, "skipped": True}


def do_rollback(repo: Repo, args, out: Out) -> int:
    """Returns to the commit recorded before the last update, then rebuilds."""
    state = load_state(STATE_FILE)
    target = state.get("previous") or state.get("last_good")
    if not target:
        out.mark("fail", "rollback", "no previous commit recorded in out/update/state.json")
        out.warn("nothing to roll back to: the updater records that on its first successful run.")
        return 1
    if repo.status()["dirty"]:
        if not args.stash:
            out.mark("fail", "rollback",
                     "the working tree has local changes; commit or stash them first")
            out.warn("a rollback does 'git reset --hard', which would discard them.")
            return 1
        # --stash means "keep my changes", so they are stashed, not reset away: passing
        # --stash and then losing them to reset --hard would be worse than refusing.
        out.step("stashing local changes before the rollback (--stash)")
        code, text = repo.git("stash", "push", "--include-untracked", "--message",
                              "browserx-update: local changes before a rollback")
        if code != 0:
            out.mark("fail", "rollback", "git stash failed:")
            out.warn("  " + git_error(text))
            return 1
        out.warn("your changes are in a stash:  git stash list  then  git stash pop")

    current = repo.head()
    out.step(f"rolling back {current[:12]} -> {target[:12]} (git reset --hard)")
    code, text = repo.git("reset", "--hard", target)
    if code != 0:
        out.mark("fail", "rollback", git_error(text))
        return 1
    # Record the direction of the move, so a second --rollback is not a surprise, and keep
    # the commit that was left so it can be restored from the reflog by hand.
    save_state(STATE_FILE, {**state, "previous": current, "now": repo.head(),
                            "when": now_iso(), "action": "rollback"})
    out.mark("ok", "rollback", f"now at {repo.head()[:12]}")
    out.say("")
    out.warn("the commits you rolled away from are still in the reflog; to go forward again:")
    out.warn(f"  git reset --hard {current[:12]}")
    capability = machine_capability(out)
    build_plan = decide_plan(capability["core_ok"], capability["browser_ok"],
                             capability["cef_missing"], args)
    results = do_build(build_plan, args, out)
    verify_results = do_verify(decide_verification(capability["core_ok"],
                                                   capability["headers_present"], args),
                               args, out) if all(item["ok"] for item in results) else []
    ok = all(item["ok"] for item in results + verify_results)
    out.say("")
    out.mark("ok" if ok else "fail", "rebuild after rollback",
             "rebuilt and verified" if ok else
             "the rolled-back commit does not build here either")
    return 0 if ok else 1


def now_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


# ---------------------------------------------------------------------------
# Self-test: the decision logic, without touching a repository or a compiler
# ---------------------------------------------------------------------------
def self_test() -> int:
    failures = []

    def expect(condition: bool, label: str) -> None:
        if condition:
            print(f"    ✔ {label}")
        else:
            failures.append(label)
            print(f"    ✘ {label}")

    class Args:
        no_build = False
        no_verify = False
        full = False
        deps = False

    print("\n  BrowserX — updater self-test (decision logic only, no git, no build)\n")

    args = Args()
    expect(decide_plan(True, True, False, args) == ["browser"],
           "a machine that can build everything builds the browser")
    expect(decide_plan(True, False, True, args) == ["core"],
           "with CEF missing it builds the core, tests and tools instead of failing")
    args.deps = True
    expect(decide_plan(True, False, True, args) == ["deps", "browser"],
           "--deps allows the CEF download first")
    args.deps = False
    expect(decide_plan(False, False, True, args) == [],
           "a machine that cannot build the core builds nothing")
    args.no_build = True
    expect(decide_plan(True, True, False, args) == [], "--no-build builds nothing")
    args.no_build = False
    args.no_verify = True
    expect(decide_verification(True, True, args) == [], "--no-verify verifies nothing")
    args.no_verify = False
    expect(decide_verification(True, True, args) == ["test"],
           "verification runs the unit tests, which need no CEF")
    args.full = True
    expect(decide_verification(True, True, args) == ["check-compile", "test"],
           "--full also compiles every file against the CEF headers")
    args.full = False

    # The step that stops an update from claiming success while the binary is untouched:
    # it must fail on a stale record, pass on a current one, and report "not checked"
    # (neither pass nor failure) when there is nothing to compare.
    import tempfile
    with tempfile.TemporaryDirectory(prefix="browserx-verify-") as scratch:
        class Quiet:
            def __getattr__(self, name):
                return lambda *a, **k: None

        verdict = verify_revision(Quiet(), REPO_ROOT / "out" / "no-such-record.json")
        expect(verdict.get("skipped") is True,
               "a build with no record is reported as not checked, never as a pass")

        record = Path(scratch) / "build-info.json"
        record.write_text('{"commit": "unknown", "short": "unknown", "dirty": false}')
        verdict = verify_revision(Quiet(), record)
        expect(verdict.get("skipped") is True,
               "a record with no commit (an outside-a-checkout build) is not checked either")

    state = tree_state(" M src/core/format.cc\n?? scratch.txt\nM  Makefile\n")
    expect(state["dirty"] and len(state["modified"]) == 2,
           "modified files block a pull")
    expect(state["untracked"] == ["scratch.txt"],
           "untracked files are reported but do not block one")
    expect(not tree_state("").get("dirty"), "a clean tree is clean")

    lines = summarize_incoming("abc123 one\ndef456 two\n")
    expect(lines == ["abc123 one", "def456 two"], "incoming commits are listed")
    expect(summarize_incoming("") == [], "nothing incoming prints nothing")

    import tempfile
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / "state.json"
        save_state(path, {"previous": "abc", "now": "def"})
        expect(load_state(path).get("previous") == "abc", "state round-trips")
        expect(load_state(Path(folder) / "missing.json") == {},
               "a missing state file is not an error")

    # The wrappers are part of the deliverable: their absence would make the platform
    # entry points fail on the user's machine, which is exactly what this checks.
    expect((REPO_ROOT / "tools" / "update.sh").is_file(), "tools/update.sh exists")
    expect((REPO_ROOT / "tools" / "update.cmd").is_file(), "tools/update.cmd exists")
    # The Windows launchers cannot be executed here, so the rules that make batch scripts
    # fail are checked statically instead of being taken on trust - for every .cmd in the
    # repository, because a rule that is only enforced on one file is enforced by luck.
    for cmd_name in ("update.cmd", "build_windows.cmd"):
        cmd = REPO_ROOT / "tools" / cmd_name
        if not cmd.is_file():
            continue
        raw = cmd.read_bytes()
        text = raw.decode("utf-8", "replace")
        expect(b"\r\n" in raw and b"\n\n" not in raw.replace(b"\r\n", b""),
               f"{cmd_name} uses CRLF line endings (cmd.exe reads a bare LF as part of the line)")
        # Comments are not code: they explain the traps, and some of them quote the very
        # patterns that must not appear in real commands. Check the commands, not the prose.
        code = "\n".join(line for line in text.splitlines()
                         if not line.strip().lower().startswith("rem"))
        expect("%ERRORLEVEL%" not in code,
               f"no command in {cmd_name} uses %ERRORLEVEL% (inside a block it is a stale value)")
        expect("!ERRORLEVEL!" in code,
               f"{cmd_name} checks exit codes with !ERRORLEVEL! (delayed expansion)")
        expect("EnableDelayedExpansion" in code,
               f"{cmd_name} enables delayed expansion, which !ERRORLEVEL! needs")
        # The single '&' is allowed only in `endlocal & exit /b %RC%`: that idiom has to stay
        # on one line, and `exit /b` cannot fail, so there is nothing for '&' to hide there.
        stray = code.replace("endlocal & exit /b", "")
        expect(" & " not in stray,
               f"{cmd_name} never chains real commands with a single '&' (it would run on failure too)")
        expect("chcp 65001" in text,
               f"{cmd_name} sets a console code page that can print the marks")
        expect("endlocal & exit /b %RC%" in text,
               f"{cmd_name} leaves its scope and returns a real exit code")
        # The Store-stub trap: `where python` finds a python.exe that is not Python.
        expect("import sys" in code,
               f"{cmd_name} proves Python works instead of trusting that it exists")

    print()
    if failures:
        print(f"  {len(failures)} self-test failure(s)\n")
        return 1
    # The Windows build command, decided here rather than on Windows: this is the one
    # branch of the updater that cannot be exercised on a Linux machine any other way.
    class _Args:
        def __init__(self, jobs=None):
            self.jobs = jobs
            self.no_build = False
            self.no_verify = False
            self.full = False
            self.deps = False
            self.dry_run = False

    win_label, win_command = build_command("browser", _Args(4), windows=True)
    expect("build_windows.py --stage" in win_label,
           "on Windows the updater builds with the MSVC script, not make")
    expect(win_command[1].endswith("build_windows.py") and "--stage" in win_command,
           "the Windows build command stages the CEF runtime next to the exe")
    expect("make" not in win_command, "the Windows build never calls make")
    linux_label, linux_command = build_command("browser", _Args(4), windows=False)
    expect(linux_label == "make all" and linux_command[:2] == ["make", "-j"],
           "on POSIX the updater still builds with make")
    core_label, _ = build_command("core", _Args(), windows=False)
    expect(core_label == "make pages", "the POSIX core build is make pages, not make test")

    print("  self-test passed\n")
    return 0


# ---------------------------------------------------------------------------
def main(argv: list[str]) -> int:
    global _JSON_MODE

    parser = argparse.ArgumentParser(
        description="Update BrowserX: pull, rebuild what this machine can build, verify.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""exit codes:  0 updated or already current   10 --check found an update   1 failed

examples:
  tools/update.sh                     pull, rebuild, verify
  tools/update.sh --check             is there anything new? (exit 10 when yes)
  tools/update.sh --dry-run           print the plan, touch nothing
  tools/update.sh --no-pull           rebuild and verify the tree as it is
  tools/update.sh --deps              allow the one-time CEF download
  tools/update.sh --rollback          go back to the commit before the last update
  tools/update.sh --branch main       update a different branch""")
    parser.add_argument("--check", action="store_true",
                        help="report whether an update is available, then stop")
    parser.add_argument("--dry-run", action="store_true", help="print the plan and stop")
    parser.add_argument("--branch", default="", help="branch to update (default: the current one)")
    parser.add_argument("--no-pull", action="store_true", help="do not touch git; build only")
    parser.add_argument("--no-build", action="store_true", help="pull only, no build")
    parser.add_argument("--no-verify", action="store_true", help="skip the tests after building")
    parser.add_argument("--full", action="store_true",
                        help="also run the compile check over every source file")
    parser.add_argument("--deps", action="store_true",
                        help="allow downloading CEF (about 200 MB) when it is missing")
    parser.add_argument("--stash", action="store_true",
                        help="stash local changes instead of refusing")
    parser.add_argument("--rollback", action="store_true",
                        help="return to the commit recorded before the last update")
    parser.add_argument("--jobs", type=int, default=0, help="parallel build jobs (make -jN)")
    parser.add_argument("--quiet", action="store_true", help="only the important lines")
    parser.add_argument("--json", action="store_true",
                        help="machine-readable summary on stdout, build log on stderr")
    parser.add_argument("--self-test", action="store_true", help="check the decision logic")
    args = parser.parse_args(argv)
    _JSON_MODE = args.json

    if args.self_test:
        return self_test()

    out = Out(quiet=args.quiet, json_mode=args.json)
    started = time.time()
    result = {
        "ok": False, "branch": "", "before": "", "after": "", "incoming": [],
        "pulled": False, "build": [], "verify": [], "update_available": False,
        "action": "update", "notes": [],
    }

    out.say("\n  BrowserX — updater\n")

    repo = Repo(out)
    if not repo.is_repo():
        out.mark("fail", "repository", f"{REPO_ROOT} is not a git checkout")
        result["notes"].append("not a git checkout")
        return finish(result, out, started, 1)

    if args.rollback:
        result["action"] = "rollback"
        code = do_rollback(repo, args, out)
        result["ok"] = code == 0
        result["after"] = repo.head()
        return finish(result, out, started, code)

    branch = args.branch or repo.branch()
    if not branch or branch == "HEAD":
        out.mark("fail", "branch", "detached HEAD: check out a branch first (git switch -c ...)")
        result["notes"].append("detached HEAD")
        return finish(result, out, started, 1)
    result["branch"] = branch
    result["before"] = repo.head()

    upstream = repo.upstream(branch) or repo.remote_ref(branch)
    if branch != repo.branch():
        code, text = repo.git("switch", branch)
        if code != 0:
            # Older git: switch may not exist.
            code, text = repo.git("checkout", branch)
        if code != 0:
            out.mark("fail", "branch", f"cannot check out {branch}: {text.strip()}")
            result["notes"].append("checkout failed")
            return finish(result, out, started, 1)
        upstream = repo.upstream(branch) or repo.remote_ref(branch)

    out.mark("ok", "repository", f"{REPO_ROOT}")
    out.mark("ok", "branch", f"{branch}  (upstream {upstream})")
    out.mark("ok", "commit", repo.head()[:12])

    # --- what is coming ----------------------------------------------------
    fetched, incoming = do_fetch(repo, branch, upstream, out)
    result["incoming"] = incoming
    if incoming:
        result["update_available"] = True
        out.mark("ok", "incoming", f"{len(incoming)} commit(s)")
        for line in incoming:
            out.say(f"      {line}")
    elif fetched:
        out.mark("ok", "incoming", "nothing new: the branch is current")
    if args.check:
        out.say("")
        code = EXIT_UPDATE_AVAILABLE if incoming else 0
        out.mark("info", "check", "an update is available (exit 10)" if incoming
                 else "already up to date (exit 0)")
        result["ok"] = True
        return finish(result, out, started, code)

    # --- the machine -------------------------------------------------------
    capability = machine_capability(out)
    build_plan = decide_plan(capability["core_ok"], capability["browser_ok"],
                             capability["cef_missing"], args)
    verify_plan = decide_verification(capability["core_ok"], capability["headers_present"], args)
    if capability["known"]:
        if capability["browser_ok"]:
            out.mark("ok", "this machine", "can build the full browser")
        elif capability["core_ok"]:
            out.mark("ok", "this machine", "can build the core, tests and tools")
            if capability["cef_missing"]:
                out.warn("CEF is not installed here: the browser build is skipped. "
                         "Add --deps to fetch it (about 200 MB), or run make deps.")
        else:
            out.mark("fail", "this machine", "cannot build anything yet")
            out.warn("run:  make requirements      for the full list of what is missing")

    if args.dry_run:
        out.say("")
        out.mark("info", "dry run", "nothing was changed. The plan:")
        out.say(f"      pull:   {'no (--no-pull)' if args.no_pull else upstream + ' --ff-only'}")
        out.say(f"      build:  {', '.join(build_plan) if build_plan else 'nothing'}")
        out.say(f"      verify: {', '.join(verify_plan) if verify_plan else 'nothing'}")
        result["ok"] = True
        result["notes"].append("dry run")
        return finish(result, out, started, 0)

    # --- pull --------------------------------------------------------------
    stash_state = {"stashed": False}
    pulled, after_pull = do_pull(repo, branch, upstream, args, out, stash_state)
    result["pulled"] = pulled
    if not pulled:
        result["notes"].append("the pull did not happen")
        return finish(result, out, started, 1)
    result["after"] = after_pull
    if after_pull != result["before"]:
        out.mark("ok", "updated", f"{result['before'][:12]} -> {after_pull[:12]}")

    # --- build and verify ---------------------------------------------------
    build_results = do_build(build_plan, args, out)
    verify_results = do_verify(verify_plan, args, out) if all(
        item["ok"] for item in build_results) else []
    result["build"] = build_results
    result["verify"] = verify_results
    ok = all(item["ok"] for item in build_results + verify_results)
    result["ok"] = ok

    # The state file is what makes --rollback possible, so it is written whenever HEAD
    # actually moved - including when the build then failed. `previous` means "the commit
    # this update started from", which is exactly what a rollback should return to; the
    # earlier version of this code wrote the file only on success, so the failure message
    # told the user to run --rollback against a file that did not exist.
    if result["after"] and result["after"] != result["before"]:
        save_state(STATE_FILE, {
            "previous": result["before"], "now": result["after"], "branch": branch,
            "when": now_iso(), "action": "update",
            "build_ok": ok, "verified": bool(verify_results) and all(
                item["ok"] for item in verify_results),
        })

    if stash_state["stashed"]:
        out.say("")
        out.warn("your local changes are in a stash, not gone:")
        out.warn("  git stash list          # what is waiting")
        out.warn("  git stash pop           # put it back (may report a conflict if the")
        out.warn("                          #  update touched the same lines - resolve it there)")

    out.say("")
    if not build_results and not verify_results:
        out.mark("ok", "done", "source updated, nothing was built (this machine or the flags)")
    elif ok:
        what = "the browser" if "browser" in build_plan else "the core, tests and tools"
        out.mark("ok", "done", f"{what} rebuilt and verified")
        if verify_results:
            out.mark("ok", "verified", ", ".join(item["step"] for item in verify_results))
        if "browser" in build_plan:
            out.warn("a running BrowserX keeps the old binary until you restart it.")
    else:
        out.mark("fail", "done", "the update was applied but something did not build")
        out.warn("the source is updated; the binary is not. To see what failed, read above.")
        if result["before"] and result["before"] != result["after"]:
            out.warn(f"to go back:  tools/update.sh --rollback     (returns to {result['before'][:12]})")
            out.warn("that commit built and verified here before this update, which is why it is "
                     "the recorded one.")
    return finish(result, out, started, 0 if ok else 1)


def finish(result: dict, out: Out, started: float, code: int) -> int:
    result["exit_code"] = code
    result["seconds"] = round(time.time() - started, 1)
    if _JSON_MODE:
        print(json.dumps(result, indent=2))
    else:
        out.say(f"\n  finished in {result['seconds']}s  (exit {code})\n")
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
