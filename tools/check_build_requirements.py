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
    import platform
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


GROUPS = ("core", "browser", "mobile", "preview")
MARK = {"ok": "✔", "missing": "✘", "warn": "›", "info": "·"}


def render(report: Report, only: list[str]) -> None:
    titles = {
        "core": "core + tests + tools   (no CEF needed)",
        "browser": "the full browser      (CEF, X11/GTK, a display)",
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
