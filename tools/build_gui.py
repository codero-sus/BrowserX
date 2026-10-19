#!/usr/bin/env python3
# BrowserX — (c) 2026 codero-sus. All rights reserved.
# Personal, non-commercial use only. No modification, no redistribution or resale.
# Source-available, not open source — see LICENSE. https://the-codero.web.app
"""BrowserX — build the browser from a window instead of a command line.

A front-end, not a second build system. Every button runs a command that already exists
(tools/check_build_requirements.py, tools/fetch_cef.py, tools/build_windows.py, make,
tools/update.py, tools/disk_report.py), and the requirements shown in the window come from
the same report the command line prints. If this file and the command line ever disagreed
about what is installed, this file would be showing a lie, so it asks rather than guesses.

Tkinter is the whole toolkit on purpose: it ships with Python on Windows, needs no pip
install, and this project's rule is that its tools have no dependencies (the browser itself
has none either). On Linux the package is usually `python3-tk`.

    python tools\\build_gui.py          # the window
    python tools\\build_gui.py --check  # the same state, printed, no window (headless)
    python tools\\build_gui.py --self-test

`--check` exists because a GUI cannot be looked at from a build server, and because the
window's own logic should be testable without a display. It prints exactly what the window
would show.
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import queue
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TOOLS_DIR = REPO_ROOT / "tools"
OUT_DIR = REPO_ROOT / "out"

EXIT_OK = 0
EXIT_FAILED = 1
EXIT_NO_DISPLAY = 2

IS_WINDOWS = sys.platform.startswith("win")
EXECUTABLE = OUT_DIR / ("BrowserX.exe" if IS_WINDOWS else "BrowserX")


# ---------------------------------------------------------------------------
# Loading the sibling tools. They are scripts, not an installed package, so they are
# loaded by path - the same way tools/update.py loads the requirements checker.
# ---------------------------------------------------------------------------
def load_tool(name: str):
    path = TOOLS_DIR / f"{name}.py"
    if not path.is_file():
        return None
    spec = importlib.util.spec_from_file_location(f"browserx_{name}", str(path))
    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)
    except Exception:
        # A tool that cannot be imported must not take the window down with it: the window
        # reports the button as unavailable and says which file is at fault.
        return None
    return module


def requirements_report() -> tuple[object | None, str]:
    """(module, text) for the requirements table the window shows."""
    module = load_tool("check_build_requirements")
    if module is None:
        return None, "tools/check_build_requirements.py could not be loaded"

    report = module.Report()
    module.check_core(report)
    if IS_WINDOWS:
        module.check_windows(report)
    else:
        module.check_browser(report)
    groups = ["windows"] if IS_WINDOWS else ["browser"]
    module.check_preview(report)

    lines: list[str] = []
    for group in ["core"] + groups:
        title = {
            "core": "core + tests + tools   (no CEF needed)",
            "browser": "the full browser      (CEF, X11/GTK, a display)",
            "windows": "the full browser      (MSVC, Windows SDK, CEF)",
        }.get(group, group)
        blocking = report.blocking(group)
        lines.append(f"{title}")
        for row in report.of(group):
            mark = {"ok": "✔", "missing": "✘", "warn": "›", "info": "·"}[row["state"]]
            lines.append(f"   {mark} {row['name']:<22} {row['detail']}")
            if row["state"] in ("missing", "info") and row["how"]:
                lines.append(f"     {'':<22} → {row['how']}")
        lines.append(f"   {'':<22} {'ready' if not blocking else str(len(blocking)) + ' missing'}")
        lines.append("")
    return module, "\n".join(lines)


def cef_state() -> tuple[bool, str]:
    """(usable, description) for the CEF distribution this build links against."""
    builder = load_tool("build_windows") if IS_WINDOWS else None
    override = os.environ.get("BROWSERX_CEF_DIR")
    if override and (Path(override) / "include" / "cef_app.h").is_file():
        return True, f"BROWSERX_CEF_DIR: {override}"
    deps = REPO_ROOT / ".cef-deps"
    if deps.is_dir():
        found = sorted(p for p in deps.glob("cef_binary_*") if p.is_dir())
        wanted = [p for p in found if ("windows" in p.name) == IS_WINDOWS]
        if wanted:
            return True, wanted[-1].name
        if found:
            # The wrong platform's distribution is worse than none: it looks installed.
            wrong = "linux" if IS_WINDOWS else "windows"
            return False, (f"only {found[-1].name} — that is a {wrong} build and cannot be "
                           f"linked here")
    if builder is not None:
        return False, "not downloaded (~200 MB)"
    return False, "not downloaded"


def toolchain_state() -> tuple[bool, str]:
    """(usable, one line naming the compiler the build would use).

    The caller needs the verdict as well as the sentence: a button's state must not be
    decided by searching the status text for words, which is how a GUI ends up enabling
    something it cannot run.
    """
    if IS_WINDOWS:
        builder = load_tool("build_windows")
        if builder is None:
            return False, "tools/build_windows.py could not be loaded"
        try:
            toolchain = builder.find_msvc()
        except Exception as error:
            return False, str(error).splitlines()[0]
        sdk = toolchain.get("sdk") or {}
        line = f"MSVC {toolchain['msvc'].name}"
        if sdk:
            line += f" · Windows SDK {sdk.get('version', '?')}"
            note = builder.sdk_note(str(sdk.get("version", "")))
            if note:
                line += "\n" + note
        return True, line
    compiler = shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        return False, "no C++ compiler on PATH"
    try:
        completed = subprocess.run([compiler, "--version"], capture_output=True, text=True,
                                   timeout=10)
        first = (completed.stdout or compiler).splitlines()[0]
        return completed.returncode == 0, first
    except Exception:
        return True, compiler


# ---------------------------------------------------------------------------
# The jobs. Each is a command that already exists; the window only decides when to run it.
# ---------------------------------------------------------------------------
class Job:
    def __init__(self, key: str, label: str, description: str, command: list[str] | None,
                 needs: str = "", group: str = "build"):
        self.key = key
        self.label = label
        self.description = description
        self.command = command
        self.needs = needs      # "cef" | "toolchain" | "" - a human-readable gate
        self.group = group


def jobs(has_cef: bool, has_toolchain: bool, has_python_tk: bool = True) -> list[Job]:
    """Every action the window offers, and what each one requires.

    Pure, so the buttons can be tested without a window: `--self-test` builds this list and
    checks the gating, which is where a GUI's logic normally hides.
    """
    python = sys.executable or "python"

    def script(name: str, *arguments: str) -> list[str]:
        """A command that runs one of our Python tools.

        Spelled out so the two platforms cannot be confused: the interpreter is prefixed
        only when the thing being run is a .py file. `python make` is what happens when
        that rule lives in the caller's head instead of in a function.
        """
        return [python, str(TOOLS_DIR / name), *arguments]

    build_command = (script("build_windows.py", "--stage") if IS_WINDOWS else ["make"])
    build_label = "Build (MSVC)" if IS_WINDOWS else "Build (make)"

    return [
        Job("requirements", "Check requirements",
            "What this machine has and what is missing, from the same report `make "
            "requirements` prints.",
            script("check_build_requirements.py", "--for",
                   "windows" if IS_WINDOWS else "browser"), group="setup"),
        Job("cef", "Fetch CEF (200 MB)",
            "Downloads, SHA-1 verifies and extracts the Chromium/CEF binaries this build "
            "links against. One time; ~1 GB extracted.",
            script("fetch_cef.py"), group="setup"),
        Job("build", build_label,
            "Compiles the browser and stages the CEF runtime next to the executable. Only "
            "what changed is recompiled, so a rebuild after a small edit is quick.",
            build_command, needs="cef", group="build"),
        Job("run", "Run BrowserX",
            "Starts the browser you just built.", [str(EXECUTABLE)],
            needs="cef", group="build"),
        Job("dry-run", "Show the build commands",
            "Prints every compiler and linker command without running any of them.",
            script("build_windows.py", "--dry-run") if IS_WINDOWS else ["make", "-n"],
            group="checks"),
        Job("check-windows", "Check the Windows path",
            "Proves the Windows build compiles the same sources as the Linux build, and "
            "that no POSIX include has leaked in. Runs anywhere.",
            script("build_windows.py", "--check-sources"), group="checks"),
        Job("space", "Where the disk went",
            "Measured sizes for this checkout, and what each one costs to get back. "
            "Deletes nothing.", script("disk_report.py"), group="housekeeping"),
        Job("space-apply", "Reclaim build space",
            "Removes the rebuildable outputs. Keeps your profile: history, bookmarks and "
            "settings are never touched.", script("disk_report.py", "--apply"),
            group="housekeeping"),
        Job("update-check", "Check for updates",
            "Is there anything new to pull? Exit code 10 means yes - the window says which.",
            script("update.py", "--check"), group="housekeeping"),
        Job("update", "Update BrowserX",
            "Pulls the new commits, rebuilds what this machine can build, and verifies it.",
            script("update.py"), group="housekeeping"),
    ]


def disabled_reason(job: Job, has_cef: bool, has_toolchain: bool) -> str:
    """Why a button is greyed out, in one line - or an empty string when it is usable."""
    if job.needs == "cef" and not has_cef:
        return "needs the CEF binaries: use “Fetch CEF” first"
    if job.needs == "toolchain" and not has_toolchain:
        return "needs a C++ compiler (see Check requirements)"
    if job.key == "run" and not EXECUTABLE.exists():
        return f"not built yet: {EXECUTABLE.name} does not exist"
    return ""


# ---------------------------------------------------------------------------
# Running a job: in a thread, streaming into a queue that the window drains.
# ---------------------------------------------------------------------------
class Runner:
    """Runs one command at a time, streaming its output, and can stop it."""

    def __init__(self) -> None:
        self.process: subprocess.Popen | None = None
        self.thread: threading.Thread | None = None
        self.output: queue.Queue[tuple[str, str]] = queue.Queue()
        self.job: Job | None = None
        self.exit_code: int | None = None
        # Set when the stop button was pressed while the job was still running. The
        # terminal message comes from the worker, once, because two terminal messages is a
        # race: the killed process's exit code arriving after "stopped" made the window
        # report a failure for a build the user had deliberately cancelled.
        self.stopped = False

    @property
    def busy(self) -> bool:
        return self.process is not None

    def start(self, job: Job, env_extra: dict | None = None) -> None:
        self.job = job
        self.exit_code = None
        self.stopped = False
        self.thread = threading.Thread(target=self._run, args=(job, env_extra), daemon=True)
        self.thread.start()

    def _run(self, job: Job, env_extra: dict | None) -> None:
        env = dict(os.environ)
        # The tools print ✔ and ✘. Windows consoles default to a code page that cannot
        # encode them, and Python then falls back to ASCII with escaping - so the marks
        # arrive as mojibake in the pane. UTF-8 both ways is the fix.
        env["PYTHONIOENCODING"] = "utf-8"
        env["PYTHONUTF8"] = "1"
        if env_extra:
            env.update(env_extra)

        creationflags = 0
        if IS_WINDOWS:
            # No console window flashing up for each command: this is a GUI. The new
            # process group is what makes taskkill /T able to take the compiler with it.
            creationflags = (getattr(subprocess, "CREATE_NO_WINDOW", 0x08000000)
                             | getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0x00000200))

        try:
            self.process = subprocess.Popen(
                job.command, cwd=str(REPO_ROOT), env=env,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                stdin=subprocess.DEVNULL, text=True, errors="replace", bufsize=1,
                creationflags=creationflags,
                # Its own session, so stopping a build kills the build and not the window
                # that started it. Without this, os.getpgid() returns *our* group and the
                # Stop button would signal the GUI and its parent shell instead.
                start_new_session=not IS_WINDOWS)
        except FileNotFoundError as error:
            self.output.put(("line", f"  cannot run {job.command[0]}: {error}\n"))
            self.output.put(("done", "127"))
            self.process = None
            return
        except OSError as error:
            self.output.put(("line", f"  could not start: {error}\n"))
            self.output.put(("done", "1"))
            self.process = None
            return

        assert self.process.stdout is not None
        for line in self.process.stdout:
            self.output.put(("line", line))
        code = self.process.wait()
        self.exit_code = code
        self.process = None
        # The single terminal message for this run: "stopped" when the user asked for it,
        # otherwise the job's own exit code.
        self.output.put(("done", "stopped" if self.stopped else str(code)))

    def stop(self) -> None:
        """Stops the running job, and the whole tree of it.

        A build spawns the compiler; killing only the shell leaves the compilers running,
        which is how a "stopped" build keeps burning CPU. On Windows the tree is killed
        with taskkill, elsewhere by the process group the child was given its own session
        for.
        """
        if self.process is None or self.process.poll() is not None:
            return  # already finished: nothing to stop, and no "stopped" to report
        self.stopped = True
        pid = self.process.pid
        try:
            if IS_WINDOWS:
                subprocess.run(["taskkill", "/F", "/T", "/PID", str(pid)],
                               capture_output=True, creationflags=0x08000000)
            else:
                os.killpg(os.getpgid(pid), 15)
        except Exception:
            try:
                self.process.terminate()
            except Exception:
                pass
        # A note for the log, not a terminal message: the worker sends that, once.
        self.output.put(("line", "\n  stopping…\n"))


def exit_meaning(code: str | None, job: Job | None) -> str:
    """What the exit code means, in the window's words rather than the shell's."""
    if code is None:
        return ""
    if code == "stopped":
        return "stopped by you"
    if code == "10" and job is not None and job.key == "update-check":
        # The updater's own contract: 10 with --check means "an update is waiting".
        return "an update is available"
    return "finished" if code == "0" else f"failed (exit {code})"


# ---------------------------------------------------------------------------
# The window
# ---------------------------------------------------------------------------
def run_gui() -> int:
    try:
        import tkinter as tk
        from tkinter import font as tkfont
        from tkinter import messagebox, ttk
    except ImportError:
        print("\n  Tkinter is not available in this Python, so the window cannot open.\n"
              "  On Windows install Python from python.org (Tcl/Tk is included).\n"
              "  On Linux:  sudo apt install python3-tk\n"
              "  The command line does not need it: tools\\build_windows.cmd --stage\n")
        return EXIT_NO_DISPLAY

    module, report_text = requirements_report()
    # One dictionary, so a job that changes the machine (fetching CEF) can update what the
    # buttons are allowed to do. A copied flag here is how the Build button ends up still
    # greyed out after a successful CEF download.
    state = {"cef": False, "toolchain": False}
    state["cef"], cef_text = cef_state()
    state["toolchain"], toolchain_text = toolchain_state()
    all_jobs = jobs(state["cef"], state["toolchain"])
    by_key = {job.key: job for job in all_jobs}

    root = tk.Tk()
    root.title("BrowserX — build")
    root.geometry("980x680")
    root.minsize(760, 520)

    style = ttk.Style(root)
    for theme in ("vista", "winnative", "clam", "default"):
        if theme in style.theme_names():
            style.theme_use(theme)
            break
    mono = tkfont.nametofont("TkFixedFont").copy()
    mono.configure(size=max(9, mono.cget("size")))

    runner = Runner()

    # --- header ------------------------------------------------------------
    header = ttk.Frame(root, padding=(12, 10, 12, 4))
    header.pack(fill="x")
    ttk.Label(header, text="BrowserX", font=("", 15, "bold")).pack(side="left")
    ttk.Label(header, text="  by codero-sus · personal use only",
              foreground="#666").pack(side="left")
    state_label = ttk.Label(header, text="")
    state_label.pack(side="right")

    # --- environment -------------------------------------------------------
    env_box = ttk.LabelFrame(root, text="This machine", padding=10)
    env_box.pack(fill="x", padx=12, pady=(0, 6))
    ttk.Label(env_box, text="Compiler").grid(row=0, column=0, sticky="nw", padx=(0, 8))
    ttk.Label(env_box, text=toolchain_text, wraplength=760,
              justify="left").grid(row=0, column=1, sticky="w")
    ttk.Label(env_box, text="CEF").grid(row=1, column=0, sticky="nw", padx=(0, 8))
    ttk.Label(env_box, text=cef_text, wraplength=760, justify="left").grid(
        row=1, column=1, sticky="w")
    ttk.Label(env_box, text="Target").grid(row=2, column=0, sticky="nw", padx=(0, 8))
    ttk.Label(env_box, text=str(EXECUTABLE), wraplength=760,
              justify="left").grid(row=2, column=1, sticky="w")

    # --- buttons -----------------------------------------------------------
    buttons_box = ttk.Frame(root, padding=(12, 0))
    buttons_box.pack(fill="x")
    widgets: dict[str, ttk.Button] = {}

    def refresh_buttons() -> None:
        for key, button in widgets.items():
            reason = disabled_reason(by_key[key], state["cef"], state["toolchain"])
            if runner.busy:
                button.state(["disabled"])
            elif reason:
                button.state(["disabled"])
            else:
                button.state(["!disabled"])

    for group, title in (("setup", "Set up"), ("build", "Build"),
                         ("checks", "Checks"), ("housekeeping", "Space & updates")):
        row = ttk.Frame(buttons_box, padding=(0, 4))
        row.pack(fill="x")
        ttk.Label(row, text=title, width=15, anchor="w").pack(side="left")
        for job in [j for j in all_jobs if j.group == group]:
            button = ttk.Button(row, text=job.label, width=max(18, len(job.label) + 2),
                                command=lambda j=job: start(j))
            button.pack(side="left", padx=(0, 6))
            widgets[job.key] = button

    # --- output ------------------------------------------------------------
    out_box = ttk.LabelFrame(root, text="Output", padding=(8, 6))
    out_box.pack(fill="both", expand=True, padx=12, pady=(6, 4))
    text = tk.Text(out_box, wrap="none", font=mono, height=18, background="#101418",
                   foreground="#d8dee9", insertbackground="#d8dee9", borderwidth=0)
    scroll_y = ttk.Scrollbar(out_box, orient="vertical", command=text.yview)
    scroll_x = ttk.Scrollbar(out_box, orient="horizontal", command=text.xview)
    text.configure(yscrollcommand=scroll_y.set, xscrollcommand=scroll_x.set)
    text.grid(row=0, column=0, sticky="nsew")
    scroll_y.grid(row=0, column=1, sticky="ns")
    scroll_x.grid(row=1, column=0, sticky="ew")
    out_box.rowconfigure(0, weight=1)
    out_box.columnconfigure(0, weight=1)

    # --- footer ------------------------------------------------------------
    footer = ttk.Frame(root, padding=(12, 4, 12, 10))
    footer.pack(fill="x")
    status = ttk.Label(footer, text="Ready.")
    status.pack(side="left")
    progress = ttk.Progressbar(footer, mode="indeterminate", length=140)

    def open_output_folder() -> None:
        try:
            if IS_WINDOWS:
                os.startfile(str(OUT_DIR))  # type: ignore[attr-defined]
            elif sys.platform == "darwin":
                subprocess.Popen(["open", str(OUT_DIR)])
            else:
                subprocess.Popen(["xdg-open", str(OUT_DIR)])
        except Exception as error:
            messagebox.showwarning("BrowserX", f"Could not open {OUT_DIR}:\n{error}")

    ttk.Button(footer, text="Open output folder", command=open_output_folder).pack(
        side="right")
    stop_button = ttk.Button(footer, text="Stop", command=lambda: runner.stop())
    stop_button.pack(side="right", padx=(0, 6))
    stop_button.state(["disabled"])

    def append(line: str) -> None:
        text.insert("end", line)
        text.see("end")

    # --- the run loop ------------------------------------------------------
    def start(job: Job) -> None:
        if runner.busy:
            return
        text.delete("1.0", "end")
        append(f"  {job.label}\n  {job.description}\n\n")
        append("  $ " + " ".join(job.command) + "\n\n")
        status.configure(text=f"{job.label}…")
        for button in widgets.values():
            button.state(["disabled"])
        stop_button.state(["!disabled"])
        progress.pack(side="right", padx=(0, 8))
        progress.start(12)
        runner.start(job)

    def poll() -> None:
        try:
            while True:
                kind, payload = runner.output.get_nowait()
                if kind == "line":
                    append(payload)
                elif kind == "done":
                    finish(payload)
        except queue.Empty:
            pass
        root.after(80, poll)

    def finish(code: str) -> None:
        job = runner.job
        meaning = exit_meaning(code, job)
        status.configure(text=f"{job.label if job else 'Job'}: {meaning}.")
        progress.stop()
        progress.pack_forget()
        stop_button.state(["disabled"])
        if job is not None and code == "0" and job.key in ("cef", "build"):
            # These two change what the machine can do: a fetched CEF enables the build, a
            # finished build enables Run. Re-ask the disk instead of assuming - a partial
            # fetch would otherwise enable a build that cannot link.
            state["cef"], _ = cef_state()
            state["toolchain"], _ = toolchain_state()
        refresh_buttons()
        if code not in ("0", "10", "stopped"):
            append(f"\n  {meaning}. The messages above are the whole story.\n")

    refresh_buttons()
    append("  BrowserX — build window\n\n")
    append(report_text + "\n")
    append("  Every button below runs a command you could type yourself; nothing here is a\n"
           "  second build system. The README and BUILDING.md describe the same steps.\n")

    root.after(80, poll)
    root.mainloop()
    if runner.busy:
        runner.stop()
    return runner.exit_code or EXIT_OK


# ---------------------------------------------------------------------------
def show_state() -> int:
    """What the window would show, printed - for a machine with no display, and for tests."""
    module, report_text = requirements_report()
    has_cef, cef_text = cef_state()
    has_toolchain, toolchain_text = toolchain_state()
    print()
    print("  BrowserX — build window (headless view)")
    print("  " + "-" * 68)
    print(f"    platform   {sys.platform}")
    print(f"    compiler   {toolchain_text}")
    print(f"    CEF        {cef_text}")
    print(f"    target     {EXECUTABLE}")
    print()
    for job in jobs(has_cef, has_toolchain):
        reason = disabled_reason(job, has_cef, has_toolchain)
        state = "available" if not reason else f"disabled — {reason}"
        print(f"    [{ '·' if reason else '✔' }] {job.label:<22} {state}")
        print(f"        {' '.join(job.command)}")
    print()
    print(report_text)
    try:
        import tkinter  # noqa: F401
        print("    tkinter: available — `python tools/build_gui.py` opens the window\n")
    except ImportError:
        print("    tkinter: NOT available in this Python, so the window cannot open here.\n"
              "             Windows: install Python from python.org (Tcl/Tk included).\n"
              "             Linux:   sudo apt install python3-tk\n")
    return EXIT_OK


def self_test() -> int:
    failures = 0

    def expect(condition: bool, what: str) -> None:
        nonlocal failures
        print(f"    {'✔' if condition else '✘'} {what}")
        if not condition:
            failures += 1

    print("\n  BrowserX — build window self-test\n")

    for cef, toolchain in ((True, True), (False, True), (True, False), (False, False)):
        listed = jobs(cef, toolchain)
        build_job = next(j for j in listed if j.key == "build")
        expect(bool(disabled_reason(build_job, cef, toolchain)) != cef,
               f"build is {'available' if cef else 'disabled'} when CEF is "
               f"{'present' if cef else 'missing'}")
    # The reason has to name the thing to do, not just refuse.
    _, reason = True, disabled_reason(next(j for j in jobs(False, True) if j.key == "build"),
                                      False, True)
    expect("Fetch CEF" in reason, "the reason a button is greyed out names the fix")

    all_jobs = jobs(True, True)
    expect(all(j.command for j in all_jobs), "every job has a command")
    expect(len({j.key for j in all_jobs}) == len(all_jobs), "job keys are unique")
    expect(all(j.description for j in all_jobs), "every job explains what it does")
    # Nothing here may be a second implementation of a build step: every command must run
    # a tool that already exists in tools/, or make, or the built binary.
    for job in all_jobs:
        head = job.command[0]
        if head in (sys.executable, "make") or head.endswith("BrowserX"):
            continue
        expect(False, f"{job.key} runs something unexpected: {head}")
    # The bug this pins: the build job was spelled [python, "make"], so it tried to run
    # the interpreter on make. The rule is mechanical - a .py file gets the interpreter,
    # anything else does not - so it can be checked mechanically.
    for job in all_jobs:
        head = job.command[0]
        if head == sys.executable:
            expect(job.command[1].endswith(".py") and
                   (TOOLS_DIR / Path(job.command[1]).name).is_file(),
                   f"{job.key} runs a Python tool that exists")
        else:
            expect(not any(part.endswith(".py") for part in job.command[:1]),
                   f"{job.key} does not run a .py file without the interpreter")
            expect(head.endswith("BrowserX") or head == "make",
                   f"{job.key} runs an executable, not a script: {head}")
    expect(True, "every job runs an existing tool, make, or the built browser")

    expect(exit_meaning("0", all_jobs[0]) == "finished", "exit 0 reads as finished")
    expect("failed" in exit_meaning("1", all_jobs[0]), "exit 1 reads as failed")
    update_check = next(j for j in all_jobs if j.key == "update-check")
    expect(exit_meaning("10", update_check) == "an update is available",
           "the updater's exit 10 is explained, not called a failure")
    expect(exit_meaning("stopped", all_jobs[0]) == "stopped by you",
           "a job you stopped is not reported as a failure")

    runner = Runner()
    expect(not runner.busy, "the runner starts idle")
    expect(runner.stop() is None, "stopping an idle runner does nothing (no crash)")

    # --- the runner, for real: streaming, exit codes, and stopping ------------------
    def drain(r: Runner) -> tuple[list[str], str | None]:
        lines: list[str] = []
        code = None
        while True:
            try:
                kind, payload = r.output.get_nowait()
            except queue.Empty:
                break
            if kind == "line":
                lines.append(payload.rstrip("\n"))
            else:
                code = payload
        return lines, code

    talker = Job("talk", "talks", "prints and exits",
                 [sys.executable, "-c", "print('one'); print('two'); print('three')"])
    runner.start(talker)
    if runner.thread is not None:
        runner.thread.join(timeout=20)
    lines, code = drain(runner)
    expect(code == "0", "a job that exits 0 reports 0")
    expect(lines == ["one", "two", "three"], "the job's output arrives line by line, in order")

    failer = Job("fail", "fails", "exits 3", [sys.executable, "-c", "import sys; sys.exit(3)"])
    runner.start(failer)
    if runner.thread is not None:
        runner.thread.join(timeout=20)
    _, code = drain(runner)
    expect(code == "3", "a job's own exit code is passed through, not flattened to 1")

    # Stopping a long job must kill it, and must not kill this process: the runner puts
    # the child in its own session for exactly this reason, and this is the test that
    # would kill the test if that were wrong.
    sleeper = Job("sleep", "sleeps", "sleeps for a minute",
                  [sys.executable, "-c", "import time; time.sleep(60)"])
    runner.start(sleeper)
    time.sleep(1.0)
    expect(runner.busy, "a running job reports itself as busy")
    runner.stop()
    if runner.thread is not None:
        runner.thread.join(timeout=20)
    _, code = drain(runner)
    expect(code == "stopped", "a stopped job reports that it was stopped")
    expect(not runner.busy, "the runner is free again after a stop")
    expect(True, "and this process survived the stop, which is the point of the session")

    print()
    if failures:
        print(f"  {failures} self-test failure(s)\n")
        return EXIT_FAILED
    print("  self-test passed\n")
    return EXIT_OK


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="build_gui.py", description="BrowserX — build the browser from a window.")
    parser.add_argument("--check", action="store_true",
                        help="print what the window would show, and exit (no display needed)")
    parser.add_argument("--self-test", action="store_true",
                        help="check the window's logic without opening a window")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()
    if args.check:
        return show_state()
    return run_gui()


if __name__ == "__main__":
    # Windows console code pages cannot encode ✔ or →; without this a tool dies while
    # reporting something else. Best effort: it never makes a working run fail.
    import utf8_console  # same directory: sys.path[0] when run as a script
    utf8_console.enable()
    sys.exit(main(sys.argv[1:]))
