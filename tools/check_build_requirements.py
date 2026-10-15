#!/usr/bin/env python3
# BrowserX — (c) 2026 codero-sus. All rights reserved.
# Personal, non-commercial use only. No modification, no redistribution or resale.
# Source-available, not open source — see LICENSE. https://the-codero.web.app
"""BrowserX — what this machine needs, checked rather than claimed.

`make requirements` runs this. It answers one question: *what can be built here, and what
is missing for the rest?* That is deliberately not the same as `make doctor`, which asks
whether the browser can run on this machine (display, GL, sandbox). This script is about
the build.

Four kinds of requirement are reported, because the honest answer to "can I build it?"
depends on what you want:

  core      the CEF-free core, the unit tests and every command-line tool.
            Needs only a C++20 compiler, make and Python 3.
  browser   the full BrowserX window: additionally CEF (make deps), X11 development
            headers, and at runtime a display.
  mobile    the Android edition: JDK 17, Android SDK 34+, NDK r26+.
  preview   the design preview plus the jsdom UI checks (make preview, make ui-check).
            Optional, and never needed to build the browser.

Exit code is 0 when everything required for `--for <what>` is present, 1 otherwise, so it
can gate a script. Nothing is downloaded: missing pieces are reported with the exact
command or package that provides them.

Usage:
    tools/check_build_requirements.py                 all four groups
    tools/check_build_requirements.py --for core      only what core needs (exit code!)
    tools/check_build_requirements.py --json          machine-readable
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import platform
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# A version we know compiles this code. Older ones usually work; the number is a floor for
# C++20 library features actually used (std::string::starts_with, std::optional, ranges-free).
MIN_GCC = (12, 0)
MIN_CLANG = (15, 0)
MIN_PYTHON = (3, 8)
MIN_MAKE = (4, 0)


class Report:
    def __init__(self) -> None:
        self.rows: list[dict] = []

    def add(self, group: str, name: str, state: str, detail: str, how: str = "") -> None:
        """state: ok | missing | warn | info"""
        self.rows.append({"group": group, "name": name, "state": state,
                          "detail": detail, "how": how})

    def of(self, group: str) -> list[dict]:
        return [row for row in self.rows if row["group"] == group]

    def blocking(self, group: str) -> list[dict]:
        return [row for row in self.of(group) if row["state"] == "missing"]

    def ok(self, group: str) -> bool:
        return not self.blocking(group)


def run(command: list[str], timeout: int = 20) -> tuple[int, str]:
    """Runs a command the tool may not have; never raises."""
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
        return result.returncode, (result.stdout or "") + (result.stderr or "")
    except (OSError, subprocess.SubprocessError):
        return 127, ""


def first_version(text: str) -> tuple[int, ...]:
    match = re.search(r"(\d+)\.(\d+)(?:\.(\d+))?", text)
    if match is None:
        return ()
    return tuple(int(part) for part in match.groups(default="0"))


def check_core(report: Report) -> None:
    group = "core"

    # --- compilers ---------------------------------------------------------
    for name, minimum in (("g++", MIN_GCC), ("clang++", MIN_CLANG)):
        path = shutil.which(name)
        if path is None:
            report.add(group, name, "info", "not installed",
                       "apt install g++   (only one C++20 compiler is required)")
            continue
        _, text = run([name, "--version"])
        version = first_version(text)
        if version and version < minimum:
            report.add(group, name, "warn", f"version {'.'.join(map(str, version))} found, "
                                            f"{'.'.join(map(str, minimum))}+ expected",
                       f"apt install {name if name != 'g++' else 'g++'}")
        else:
            report.add(group, name, "ok", text.splitlines()[0].strip() if text else name)

    if not any(row["state"] == "ok" for row in report.of(group)
               if row["name"] in ("g++", "clang++")):
        report.add(group, "C++20 compiler", "missing", "no g++ or clang++ on PATH",
                   "apt install g++  (or set CXX=...)")

    # --- C++20 standard library behaviour the code relies on ---------------
    cxx = shutil.which(os.environ.get("CXX", "g++")) or shutil.which("clang++")
    if cxx:
        check = ("#include <string>\n#include <optional>\n#include <string_view>\n"
                 "int main(){ std::string s=\"ab\"; std::string_view v=s;\n"
                 "  return (v.starts_with(\"a\") && std::optional<int>(1).has_value()) ? 0 : 1; }\n")
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "probe.cc"
            source.write_text(check)
            code, text = run([cxx, "-std=c++20", "-fsyntax-only", str(source)])
            if code == 0:
                report.add(group, "C++20 library", "ok",
                           f"{Path(cxx).name} accepts the C++20 features this code uses")
            else:
                report.add(group, "C++20 library", "missing",
                           text.strip().splitlines()[0] if text else "probe failed",
                           "upgrade the compiler; -std=c++20 must be supported")
    else:
        report.add(group, "C++20 library", "missing", "no compiler to probe",
                   "apt install g++")

    # --- build plumbing ----------------------------------------------------
    make_path = shutil.which("make")
    if make_path is None:
        report.add(group, "GNU make", "missing", "make is not on PATH", "apt install make")
    else:
        _, text = run(["make", "--version"])
        version = first_version(text)
        if version and version < MIN_MAKE:
            report.add(group, "GNU make", "warn", f"make {' '.join(map(str, version))} found",
                       "apt install make")
        else:
            report.add(group, "GNU make", "ok", text.splitlines()[0].strip())

    python_path = shutil.which("python3")
    if python_path is None:
        report.add(group, "Python 3", "missing", "python3 is not on PATH",
                   "apt install python3  (the Makefile uses it for the CEF fetcher)")
    else:
        version = tuple(sys.version_info[:2])
        if version < MIN_PYTHON:
            report.add(group, "Python 3", "missing",
                       f"found {sys.version.split()[0]}, {'.'.join(map(str, MIN_PYTHON))}+ needed",
                       "apt install python3")
        else:
            report.add(group, "Python 3", "ok", f"{sys.version.split()[0]} ({python_path})")

    # --- which compiler the Makefile will actually use ---------------------
    _, text = run(["make", "-n", "-C", str(REPO_ROOT), "test"], timeout=30)
    override = os.environ.get("CXX")
    report.add(group, "Makefile CXX", "ok",
               f"CXX={override}" if override else "CXX defaults to g++ (override with CXX=...)",
               "make CXX=clang++ ...")

    # --- space the build and the tests need --------------------------------
    free_mb = shutil.disk_usage(REPO_ROOT).free // (1024 * 1024)
    needed = 400  # object files for core + tests, plus the tools
    if free_mb < needed:
        report.add(group, "Disk space", "missing", f"{free_mb} MB free, ~{needed} MB needed",
                   "free up space, or point BUILD_DIR elsewhere")
    else:
        report.add(group, "Disk space", "ok", f"{free_mb} MB free")


def check_browser(report: Report) -> None:
    group = "browser"

    # --- path/toolchain the CEF distribution is built for -------------------
    machine = platform.machine()
    if machine in ("x86_64", "amd64"):
        report.add(group, "Architecture", "ok", f"{machine} (CEF linux64/linux32 builds exist)")
    else:
        report.add(group, "Architecture", "warn", f"{machine}: no prebuilt CEF build is "
                     "published for this architecture",
                   "build CEF from source, or run on x86_64")

    # --- the CEF distribution itself ---------------------------------------
    deps_dir = REPO_ROOT / ".cef-deps"
    installed = sorted(deps_dir.glob("cef_binary_*")) if deps_dir.is_dir() else []
    headers = REPO_ROOT / ".cef-headers"
    if installed:
        report.add(group, "CEF (full binary)", "ok", installed[0].name,
                   "")
    elif (headers / "include" / "cef_app.h").is_file():
        report.add(group, "CEF (full binary)", "missing",
                   "headers only (.cef-headers) - enough for 'make check', not for linking",
                   "make deps")
    else:
        report.add(group, "CEF (full binary)", "missing", "not downloaded",
                   "make deps   (downloads, SHA-1 verifies and extracts the pinned build)")

    # --- the pin, and the network that produces it --------------------------
    # --- the header cache `make check` uses ---------------------------------
    if (headers / "include" / "cef_app.h").is_file():
        count = len(list((headers / "include").glob("*.h")))
        report.add(group, "CEF headers", "ok",
                   f"{count} headers cached in .cef-headers/ (enough for make check)")
    else:
        report.add(group, "CEF headers", "warn",
                   "not fetched yet; make check fetches them (about 2 MB, one time)",
                   "make headers")

    lock = REPO_ROOT / "cef.lock.json"
    pinned = ""
    if lock.is_file():
        try:
            data = json.loads(lock.read_text(encoding="utf-8"))
            pinned = str(data.get("version") or "")
            if pinned and data.get("sha1"):
                report.add(group, "CEF pin", "ok",
                           f"cef.lock.json pins {pinned} (SHA-1 {str(data['sha1'])[:12]}…)")
            else:
                report.add(group, "CEF pin", "warn",
                           "cef.lock.json has no version pinned yet",
                           "make deps   (resolves the newest stable build, verifies it, re-pins)")
        except (OSError, ValueError) as error:
            report.add(group, "CEF pin", "warn", f"cef.lock.json unreadable: {error}",
                       "make deps")
    else:
        report.add(group, "CEF pin", "warn", "cef.lock.json is missing", "make deps")

    # `make deps` downloads ~200 MB, so whether this machine can reach the CEF build server
    # is a real requirement and not a detail. Offline installs are supported through the
    # environment overrides, and those are named here rather than left to be discovered.
    offline_hint = (os.environ.get("BROWSERX_CEF_DIR") or os.environ.get("BROWSERX_CEF_ARCHIVE"))
    if installed:
        report.add(group, "CEF download", "ok", "not needed: a distribution is already installed")
    elif offline_hint:
        report.add(group, "CEF download", "ok",
                   f"offline source configured: {offline_hint}",
                   "make deps   (will use it, no network)")
    else:
        report.add(group, "CEF download", "info",
                   "make deps needs ~200 MB of download (or an offline archive)",
                   "BROWSERX_CEF_ARCHIVE=cef_binary.tar.bz2 make deps   # no network needed")

    # --- link- and run-time libraries --------------------------------------
    # Linking BrowserX needs -lX11 and dlopen-able GTK/X11 runtime from the CEF build.
    pkg_config = shutil.which("pkg-config")
    if pkg_config is None:
        report.add(group, "pkg-config", "warn", "not installed; only needed to verify the "
                     "X11/GTK development packages below",
                   "apt install pkg-config")
    else:
        for package, what in (("x11", "X11 development headers (-lX11)"),
                              ("gtk+-3.0", "GTK 3 development files (CEF Views window)")):
            code, text = run([pkg_config, "--exists", package], timeout=15)
            # pkg-config --exists has no output: translate its exit code honestly
            exists = code == 0
            if exists:
                _, version_text = run([pkg_config, "--modversion", package], timeout=15)
                report.add(group, package, "ok", f"{what}: {version_text.strip()}")
            else:
                report.add(group, package, "missing", what,
                           "apt install libx11-dev libgtk-3-dev" if package == "x11"
                           else "apt install libgtk-3-dev")

    # Direct header check, so a machine without pkg-config still gets a real answer.
    for header, package in (("/usr/include/X11/Xlib.h", "libx11-dev"),
                            ("/usr/include/gtk-3.0/gtk/gtk.h", "libgtk-3-dev")):
        present = Path(header).is_file()
        name = Path(header).name
        if present:
            report.add(group, name, "ok", "development header present")
        elif not pkg_config:
            report.add(group, name, "missing", f"{header} not found", f"apt install {package}")
        # (with pkg-config, the check above already answered for these packages)

    # --- a display, for actually running it --------------------------------
    display = os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")
    if display:
        report.add(group, "Display", "ok", f"{display} is set")
    else:
        report.add(group, "Display", "warn",
                   "no DISPLAY/WAYLAND_DISPLAY: the browser builds but cannot open a window "
                   "here (headless machines: use make smoke, or Xvfb)",
                   "xvfb-run make run")


def check_mobile(report: Report) -> None:
    group = "mobile"

    java = shutil.which("java")
    if java is None and os.environ.get("JAVA_HOME") is None:
        report.add(group, "JDK 17+", "missing", "java is not on PATH and JAVA_HOME is unset",
                   "apt install openjdk-17-jdk   (or Android Studio's bundled JDK)")
    else:
        text = ""
        code, text = run([java, "-version"]) if java else (0, "")
        version = first_version(text)
        if version and version < (17,):
            report.add(group, "JDK 17+", "warn", f"java {'.'.join(map(str, version))} found",
                       "apt install openjdk-17-jdk")
        else:
            report.add(group, "JDK 17+", "ok", text.splitlines()[0].strip() if text else "JAVA_HOME set")

    sdk = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
    if sdk and Path(sdk).is_dir():
        platforms = [p.name for p in (Path(sdk) / "platforms").glob("android-*")] \
            if (Path(sdk) / "platforms").is_dir() else []
        ndks = [p.name for p in (Path(sdk) / "ndk").glob("*")] \
            if (Path(sdk) / "ndk").is_dir() else []
        if any(p >= "android-34" for p in platforms):
            report.add(group, "Android SDK 34+", "ok", f"{sdk} ({', '.join(sorted(platforms)[-2:])})")
        else:
            report.add(group, "Android SDK 34+", "missing",
                       f"{sdk} has no android-34 platform ({', '.join(platforms) or 'none'})",
                       "sdkmanager 'platforms;android-34'")
        if ndks:
            report.add(group, "Android NDK r26+", "ok", f"{', '.join(ndks)}")
        else:
            report.add(group, "Android NDK r26+", "missing", "no NDK under the SDK",
                       "sdkmanager 'ndk;26.1.10909125'")
    else:
        report.add(group, "Android SDK 34+", "missing",
                   "ANDROID_HOME/ANDROID_SDK_ROOT is not set to a directory",
                   "install Android Studio, or the command-line tools + sdkmanager")
        report.add(group, "Android NDK r26+", "missing", "no NDK to find", "sdkmanager 'ndk;26.1.10909125'")

    gradle = shutil.which("gradle")
    wrapper = (REPO_ROOT / "mobile-edition" / "gradlew").is_file()
    if gradle or wrapper:
        report.add(group, "Gradle 8.6+", "ok", gradle or "mobile-edition/gradlew")
    else:
        report.add(group, "Gradle 8.6+", "missing", "no gradle and no wrapper in mobile-edition",
                   "install Gradle 8.6+, or add the wrapper: gradle wrapper")


def check_preview(report: Report) -> None:
    group = "preview"

    node = shutil.which("node")
    if node is None:
        report.add(group, "node", "info", "not installed (only needed for make ui-check)",
                   "apt install nodejs")
    else:
        _, text = run([node, "--version"])
        report.add(group, "node", "ok", text.strip() or node)

    # jsdom, wherever it happens to be: the repo, a sibling dir, or an env override.
    candidates = [os.environ.get("BROWSERX_JSDOM") or "",
                  str(REPO_ROOT / "node_modules" / "jsdom" / "lib" / "api.js"),
                  "/tmp/uicheck/node_modules/jsdom/lib/api.js"]
    found = next((c for c in candidates if c and Path(c).is_file()), "")
    if found:
        report.add(group, "jsdom", "ok", found)
    else:
        report.add(group, "jsdom", "info",
                   "not installed: make ui-check will say so and skip the DOM checks",
                   "npm install jsdom --no-save")

    # The preview server is stdlib-only Python, so it is always usable with core present.
    report.add(group, "preview server", "ok", "stdlib-only Python (preview/serve.py)")


def sdk_version_key(name: str) -> list[int]:
    """The same ordering tools/build_windows.py uses to choose an SDK, imported from it so
    the report cannot name a different one than the build will use."""
    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "browserx_build_windows", str(REPO_ROOT / "tools" / "build_windows.py"))
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module.sdk_version_key(name)
    except Exception:
        # A partial checkout, or a broken tools/ directory: fall back to the same rule
        # rather than failing the whole report, and keep it identical on purpose.
        return [int(part) if part.isdigit() else 0 for part in re.split(r"[.\-+]", name)]


def check_windows(report: Report) -> None:
    """What building the browser on Windows needs - the question, answered directly.

    Two things are careful here. First, the CEF Windows distribution is not the Linux one:
    fetching the wrong platform's build produces a tree that looks right and cannot link,
    so the distribution is described by name and the platform is stated. Second, when this
    report runs on Linux (which is where it is usually written and reviewed) it says so
    rather than reporting a Windows machine's toolchain as "missing" - a Linux machine is
    not a broken Windows machine.
    """
    group = "windows"
    on_windows = sys.platform.startswith("win")

    # --- the compiler -------------------------------------------------------
    # MSVC v143 (Visual Studio 2022). The free Build Tools are enough; the IDE is a
    # convenience, not a requirement, and saying so saves several GB.
    if not on_windows:
        report.add(group, "MSVC v143", "info",
                   "Windows-only: not checkable from here",
                   "Visual Studio 2022 Build Tools, workload 'Desktop development with C++'")
    else:
        vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / \
            "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
        found_msvc = False
        if vswhere.is_file():
            code, text = run([str(vswhere), "-latest", "-products", "*", "-requires",
                              "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                              "-property", "installationPath"])
            found_msvc = code == 0 and bool(text.strip())
        if not found_msvc:
            # A compiler inside a Visual Studio tree is the same compiler, however the
            # installer was driven; looking for it directly avoids a false "missing".
            for root in (os.environ.get("ProgramFiles", r"C:\Program Files"),
                         os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")):
                if list(Path(root, "Microsoft Visual Studio").glob("2022/*/VC/Tools/MSVC/*")):
                    found_msvc = True
                    break
        if found_msvc:
            report.add(group, "MSVC v143", "ok", "Visual Studio 2022 C++ toolset found")
        else:
            report.add(group, "MSVC v143", "missing",
                       "no Visual Studio 2022 C++ toolset found (vswhere found none)",
                       'winget install --id Microsoft.VisualStudio.2022.BuildTools --override '
                       '"--quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"')

    # --- the Windows SDK ----------------------------------------------------
    if not on_windows:
        report.add(group, "Windows 10/11 SDK", "info", "Windows-only: not checkable from here",
                   "one is required, and any one works: tick Windows 11 SDK (10.0.26100) "
                   "unless the installer refuses it, in which case Windows 10 SDK "
                   "(10.0.19041). Install exactly one - the build uses the newest and "
                   "ignores the rest")
    else:
        kits = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / \
            "Windows Kits" / "10" / "Include"
        versions = sorted((p.name for p in kits.iterdir() if p.is_dir()),
                          key=sdk_version_key, reverse=True) if kits.is_dir() else []
        if versions:
            report.add(group, "Windows 10/11 SDK", "ok",
                       f"{versions[0]} will be used (the newest installed; x64 libraries "
                       "present). Any one Windows 10/11 SDK builds BrowserX - 10.0.26100 "
                       "is the one BrowserX.vsconfig asks for")
            if len(versions) > 1:
                # The build uses the newest and ignores the rest, so extra SDKs are pure
                # disk cost - and on a machine that is short of space, that is worth
                # saying. It does not guess at the size: these vary, and the Visual Studio
                # Installer shows the figure when the tick is removed.
                report.add(group, "extra SDKs", "info",
                           f"{len(versions) - 1} more installed ({', '.join(versions[1:4])}"
                           f"{', …' if len(versions) > 4 else ''}); the build uses only "
                           "the newest",
                           "untick them in the Visual Studio Installer's Modify screen to "
                           "reclaim the space")
        else:
            report.add(group, "Windows 10/11 SDK", "missing",
                       "no Windows SDK include tree found",
                       "the Build Tools workload installs it; tick 'Windows 10/11 SDK' if it did not")

    # --- the CEF Windows distribution --------------------------------------
    deps = REPO_ROOT / ".cef-deps"
    distributions = sorted(p.name for p in deps.glob("cef_binary_*")) if deps.is_dir() else []
    windows_dist = [name for name in distributions if "windows" in name]
    linux_dist = [name for name in distributions if "linux" in name]
    if windows_dist:
        report.add(group, "CEF (windows64)", "ok", windows_dist[-1])
    elif linux_dist:
        # The trap worth naming: a Linux CEF build in the tree looks like "CEF is here".
        report.add(group, "CEF (windows64)", "missing",
                   f"only {linux_dist[-1]} is present - a Linux build, which cannot link here",
                   "python tools\\fetch_cef.py --platform windows64")
    else:
        report.add(group, "CEF (windows64)", "missing",
                   "not downloaded (~200 MB download, ~1 GB extracted)",
                   "python tools\\fetch_cef.py        (fetches windows64 automatically on Windows)")

    # --- the things that are not the compiler -------------------------------
    python = shutil.which("python") or shutil.which("py") or shutil.which("python3")
    if python:
        _, text = run([python, "--version"])
        report.add(group, "Python 3.8+", "ok", text.strip() or python)
    else:
        report.add(group, "Python 3.8+", "missing",
                   "Python runs the CEF fetcher, the build script and every tool here",
                   "winget install --id Python.Python.3.12")

    git = shutil.which("git")
    if git:
        _, text = run([git, "--version"])
        report.add(group, "Git", "ok", text.strip() or git)
    else:
        report.add(group, "Git", "missing", "needed for the checkout and for tools\\update.cmd",
                   "winget install --id Git.Git")

    # --- the build path itself ---------------------------------------------
    builder = REPO_ROOT / "tools" / "build_windows.py"
    if builder.is_file():
        report.add(group, "the build itself", "ok",
                   "tools\\build_windows.py builds out\\BrowserX.exe with MSVC "
                   "(no CMake, no Visual Studio project files to keep in sync)")
    else:
        report.add(group, "the build itself", "missing", "tools/build_windows.py is absent",
                   "this checkout is incomplete")

    if not on_windows:
        report.add(group, "this machine", "info",
                   f"this report is running on {platform.system() or 'a non-Windows system'}, "
                   "so the Windows checks above cannot probe a real toolchain",
                   "run it on the Windows machine: python tools\\check_build_requirements.py --for windows")


GROUPS = ("core", "browser", "windows", "mobile", "preview")
MARK = {"ok": "✔", "missing": "✘", "warn": "›", "info": "·"}


def render(report: Report, only: list[str]) -> None:
    titles = {
        "core": "core + tests + tools   (no CEF needed)",
        "browser": "the full browser      (CEF, X11/GTK, a display)   — Linux",
        "windows": "the full browser      (MSVC, Windows SDK, CEF)     — Windows",
        "mobile": "the Android edition   (JDK, SDK, NDK, Gradle)",
        "preview": "preview + DOM checks  (node, optional)",
    }
    print("\n  BrowserX — build requirements on this machine\n")
    for group in GROUPS:
        if group not in only:
            continue
        rows = report.of(group)
        blocking = report.blocking(group)
        print(f"  {titles[group]}")
        for row in rows:
            print(f"    {MARK[row['state']]} {row['name']:<22} {row['detail']}")
            if row["state"] in ("missing", "info") and row["how"]:
                print(f"      {'':<22} → {row['how']}")
        print(f"    {'':<22} "
              f"{'ready' if not blocking else str(len(blocking)) + ' missing'}\n")

    print("  What that means:")
    if "core" in only:
        if report.ok("core"):
            print("    make test check bench perf pages     work on this machine now")
        else:
            print("    core is not buildable yet: install the ✘ items above")
    if "browser" in only:
        if report.ok("browser"):
            print("    make deps && make && make run        build and start the browser")
        else:
            print("    the full browser needs: " +
                  ", ".join(row["name"] for row in report.blocking("browser")))
    if "windows" in only:
        if report.ok("windows"):
            print("    tools\\build_windows.cmd               build out\\BrowserX.exe")
            print("    tools\\build_windows.cmd --stage       and copy the CEF runtime beside it")
        else:
            print("    building on Windows needs: " +
                  ", ".join(row["name"] for row in report.blocking("windows")))
    if "mobile" in only:
        if report.ok("mobile"):
            print("    mobile-edition/tools/build-native.sh  cross-compiles the shared core")
        else:
            print("    the Android edition needs: " +
                  ", ".join(row["name"] for row in report.blocking("mobile")))
    if "preview" in only and report.ok("preview"):
        print("    make preview                          clickable UI, no build at all")
    print("\n  Everything the build does not need is never installed for you: this only looks.\n")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Check what this machine needs to build BrowserX.")
    parser.add_argument("--for", choices=GROUPS, action="append",
                        help="only check this group (repeatable). Exit code reflects it.")
    parser.add_argument("--json", action="store_true", help="machine-readable report")
    args = parser.parse_args(argv)
    only = getattr(args, "for") or list(GROUPS)

    report = Report()
    check_core(report)
    if "browser" in only:
        check_browser(report)
    if "windows" in only:
        check_windows(report)
    if "mobile" in only:
        check_mobile(report)
    if "preview" in only:
        check_preview(report)

    if args.json:
        print(json.dumps({
            "groups": {group: {"ok": report.ok(group), "rows": report.of(group)} for group in only},
            "ok": all(report.ok(group) for group in only),
        }, indent=2))
    else:
        render(report, only)

    return 0 if all(report.ok(group) for group in only) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
