#!/usr/bin/env python3
# BrowserX — (c) 2026 codero-sus. All rights reserved.
# Personal, non-commercial use only. No modification, no redistribution or resale.
# Source-available, not open source — see LICENSE. https://the-codero.web.app
"""Builds the BrowserX browser on Windows, with MSVC.

The rest of this project builds with `make`, which is a POSIX tool: the Makefile's
`find`, `rm`, `ln -sfn` and `-lX11` are not Windows. Rather than ship a second build
system that can disagree with the first, this script uses the *same source rule* as the
Makefile - every .cc under src/, and CEF's own libcef_dll wrapper - and only swaps the
compiler and the linker inputs. `--check-sources` proves that the two builds compile the
same files, which is the part a second build system usually gets wrong.

What it needs on the machine (see `python tools/check_build_requirements.py FOR=windows`):

  * Visual Studio 2022 Build Tools with "Desktop development with C++" (MSVC v143)
    and a Windows 10/11 SDK. The free Build Tools are enough; the full IDE is not
    needed unless you want to debug in it.
  * Python 3.8+ (already required: it fetches CEF and runs the other tools).
  * Git for Windows.
  * The CEF Windows binary distribution:  .cef-deps\\cef_binary_<version>_windows64\\
    fetched by tools/fetch_cef.py. There is no building Chromium here - CEF is a
    prebuilt binary distribution of Chromium, which is the entire point of CEF.

Usage (from the repository root, in cmd.exe or PowerShell):

  python tools\\build_windows.py                    # build out\\BrowserX.exe
  python tools\\build_windows.py --dry-run          # print every command, run none
  python tools\\build_windows.py --jobs 8           # parallel compiles
  python tools\\build_windows.py --check-sources    # prove it matches the POSIX build
  python tools\\build_windows.py --stage            # copy the CEF runtime next to the exe

Exit codes: 0 built, 1 failed, 3 nothing to do (--check-sources with no Makefile to read).
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import hashlib
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "tools"))       # so argv_notes is importable however we are called
import argv_notes                                  # noqa: E402  (path is set up just above)
SRC_DIR = REPO_ROOT / "src"
OUT_DIR = REPO_ROOT / "out"
OBJ_DIR = REPO_ROOT / "build" / "obj-win"
DEPS_DIR = REPO_ROOT / ".cef-deps"

EXIT_OK = 0
EXIT_FAILED = 1
EXIT_NOTHING_LEARNED = 3

# CEF's Windows runtime files, straight from the distribution layout. The Linux build
# stages .so files; Windows stages these. Anything absent is skipped rather than
# failing: a CEF build may legitimately ship fewer of them, and inventing a file would
# break the "the browser starts" promise that staging exists to keep.
CEF_RUNTIME_FILES = (
    "libcef.dll",
    "chrome_elf.dll",
    "libEGL.dll",
    "libGLESv2.dll",
    "d3dcompiler_47.dll",
    "vk_swiftshader.dll",
    "vulkan-1.dll",
    "vk_swiftshader_icd.json",
    "v8_context_snapshot.bin",
    "icudtl.dat",
    "resources.pak",
    "chrome_100_percent.pak",
    "chrome_200_percent.pak",
)
CEF_RUNTIME_DIRS = ("locales", "swiftshader")

# The SDK this project is developed and released against. Not a list of acceptable SDKs -
# any one Windows 10/11 SDK builds BrowserX, because the whole Win32 surface it uses is
# CreateMutexA/CloseHandle/GetModuleFileNameA/GetFileAttributesA. This is the one the
# tooling names when something newer is installed, so a preview SDK is chosen knowingly
# rather than by accident.
RECOMMENDED_SDK = "10.0.26100"

# The libraries CEF documents for a Windows client (libcef.lib plus the system ones it
# calls through). libcef_dll_wrapper is built here from the distribution's own sources -
# CEF does not ship a prebuilt wrapper, and linking libcef.lib without it fails at
# runtime, not at link time, which is the sort of failure worth designing out.
SYSTEM_LIBS = (
    "user32.lib",
    "gdi32.lib",
    "ole32.lib",
    "oleaut32.lib",
    "shlwapi.lib",
    "advapi32.lib",
    "comdlg32.lib",
    "winmm.lib",
    "imm32.lib",
    "dbghelp.lib",
    "version.lib",
    "ws2_32.lib",
    "wininet.lib",
    "bcrypt.lib",
    "crypt32.lib",
)


class BuildError(Exception):
    """Something the script can explain to a human, rather than a traceback."""


def say(message: str = "") -> None:
    print(message, flush=True)


def step(message: str) -> None:
    print(f"  → {message}", flush=True)


def ok(message: str) -> None:
    print(f"    ✔ {message}", flush=True)


def warn(message: str) -> None:
    print(f"    › {message}", flush=True)


# ---------------------------------------------------------------------------
# Locating the toolchain and CEF, by reading the filesystem rather than by
# hoping `cl.exe` is on PATH. Visual Studio does not put its compiler on PATH -
# that is what vcvarsall.bat is for - and a build script that needs vcvarsalls
# to have been run first is a build script that fails with "cl.exe not found"
# the first time someone opens a fresh shell.
# ---------------------------------------------------------------------------
def find_vswhere() -> Path | None:
    candidates = [
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
        / "Microsoft Visual Studio" / "Installer" / "vswhere.exe",
        Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
        / "Microsoft Visual Studio" / "Installer" / "vswhere.exe",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def find_msvc() -> dict:
    """The newest MSVC toolchain installed, with its directory in the environment."""
    vswhere = find_vswhere()
    installations: list[Path] = []
    if vswhere is not None:
        code, text = run([str(vswhere), "-latest", "-products", "*", "-property",
                          "installationPath"], capture=True)
        if code == 0:
            for line in text.splitlines():
                line = line.strip()
                if line:
                    installations.append(Path(line))
    # Without vswhere (or with it reporting nothing) the directories still tell the truth.
    for root in (Path(os.environ.get("ProgramFiles", r"C:\Program Files")),
                 Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))):
        for edition in ("BuildTools", "Community", "Professional", "Enterprise",
                        "Preview"):
            candidate = root / "Microsoft Visual Studio" / "2022" / edition
            if candidate.is_dir():
                installations.append(candidate)

    for installation in installations:
        tools_root = installation / "VC" / "Tools" / "MSVC"
        if not tools_root.is_dir():
            continue
        versions = sorted((p for p in tools_root.iterdir() if p.is_dir()),
                          key=lambda p: p.name, reverse=True)
        if not versions:
            continue
        msvc = versions[0]
        host_arch = "x64"
        cl = msvc / "bin" / f"Host{host_arch}" / "x64" / "cl.exe"
        if not cl.is_file():
            continue
        sdk = find_windows_sdk()
        return {"installation": installation, "msvc": msvc, "cl": cl, "sdk": sdk,
                "host_arch": host_arch}
    raise BuildError(
        "MSVC was not found. Install Visual Studio 2022 Build Tools with the "
        "'Desktop development with C++' workload:\n"
        "    winget install --id Microsoft.VisualStudio.2022.BuildTools ^\n"
        "        --override \"--quiet --add Microsoft.VisualStudio.Workload.VCTools "
        "--includeRecommended\"\n"
        "  or install Visual Studio 2022 Community and tick that workload in the installer.")


def sdk_version_key(name: str) -> list[int]:
    """Sort key for a Windows SDK version directory ("10.0.26100.0" -> [10, 0, 26100, 0]).

    Public because tools/check_build_requirements.py reports which SDK the build will use,
    and a report that disagrees with the build about that is worse than no report. Two
    copies of this comparison would eventually disagree: sorting the names as strings puts
    "10.0.10000.0" before "10.0.9999.0".
    """
    return [int(part) if part.isdigit() else 0 for part in re.split(r"[.\-+]", name)]


def sdk_note(chosen: str) -> str:
    """One line about the chosen SDK, or an empty string when there is nothing to say.

    Newer is not better here. A Windows 11 SDK numbered above the released one is usually
    an Insider preview (Canary/Dev), and a preview toolchain is a strange thing to ship a
    browser against for no gain: none of the APIs this project calls has changed since
    Windows XP. So the build says which one it is using and which one this project targets,
    and then does what the machine allows.
    """
    if not chosen:
        return ""
    try:
        # Compared on major.minor.build only. An SDK directory is named with a fourth
        # component ("10.0.26100.8249"), and comparing all four makes the same SDK look
        # newer than itself - which is exactly the false alarm the dry run caught.
        newer = sdk_version_key(chosen)[:3] > sdk_version_key(RECOMMENDED_SDK)[:3]
    except (TypeError, ValueError):
        return ""
    if newer:
        return (f"{chosen} is newer than the SDK this project targets "
                f"({RECOMMENDED_SDK}). Newer buys nothing here and an SDK above the "
                "released one is often a Windows Insider preview. Building is fine; just "
                "know which one you are building with.")
    return ""


def find_windows_sdk() -> dict | None:
    """The newest Windows 10/11 SDK: include directories, and the x64 libraries."""
    kits = [Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
            / "Windows Kits" / "10"]
    for kit in kits:
        include_root = kit / "Include"
        lib_root = kit / "Lib"
        if not include_root.is_dir():
            continue
        versions = sorted((p.name for p in include_root.iterdir() if p.is_dir()),
                          key=sdk_version_key, reverse=True)
        for version in versions:
            # The SDK must have all three, or a Windows build fails at an unrelated-looking
            # place (a missing windows.h, then a missing kernel32.lib).
            needed = [include_root / version / part for part in ("ucrt", "shared", "um")]
            if all(p.is_dir() for p in needed):
                libs = lib_root / version
                if (libs / "um" / "x64").is_dir() and (libs / "ucrt" / "x64").is_dir():
                    return {"version": version, "include_root": include_root,
                            "lib_root": lib_root}
        return None
    return None


def find_cef_root() -> Path:
    """The extracted CEF Windows distribution under .cef-deps/."""
    override = os.environ.get("BROWSERX_CEF_DIR")
    if override:
        root = Path(override)
        if (root / "include" / "cef_app.h").is_file():
            return root
        raise BuildError(f"BROWSERX_CEF_DIR={override} does not look like a CEF "
                         "distribution (no include/cef_app.h)")

    if DEPS_DIR.is_dir():
        # Newest first, and prefer a distribution whose platform is windows.
        candidates = sorted((p for p in DEPS_DIR.glob("cef_binary_*") if p.is_dir()),
                            key=lambda p: p.name, reverse=True)
        for candidate in candidates:
            if "windows" in candidate.name and (candidate / "include" / "cef_app.h").is_file():
                return candidate
        for candidate in candidates:
            if (candidate / "include" / "cef_app.h").is_file() and (
                    (candidate / "Release" / "libcef.lib").is_file()
                    or (candidate / "libcef.lib").is_file()):
                return candidate
    raise BuildError(
        "no CEF Windows distribution in .cef-deps/.\n"
        "  Fetch it (about 200 MB download, ~1 GB extracted):\n"
        "    python tools\\fetch_cef.py\n"
        "  On a machine without network access, point at an archive:\n"
        "    set BROWSERX_CEF_ARCHIVE=cef_binary_<version>_windows64.tar.bz2\n"
        "    python tools\\fetch_cef.py")


def cef_lib_dirs(cef_root: Path) -> list[Path]:
    """Where libcef.lib actually is: the layout moved around between CEF versions."""
    dirs = [cef_root / "Release", cef_root / "libcef_dll_wrapper" / "Release",
            cef_root / "libcef_dll_wrapper", cef_root]
    return [d for d in dirs if d.is_dir()]


def find_libcef_lib(cef_root: Path) -> Path:
    for directory in cef_lib_dirs(cef_root):
        candidate = directory / "libcef.lib"
        if candidate.is_file():
            return candidate
    raise BuildError(f"libcef.lib is not in {cef_root} - the distribution looks "
                     "incomplete; re-fetch it with tools/fetch_cef.py --force")


# ---------------------------------------------------------------------------
# Source lists: the same rule as the Makefile, on purpose.
# ---------------------------------------------------------------------------
def app_sources() -> list[Path]:
    """Every C++ source the browser is built from.

    The Makefile says: find src -name '*.cc' -not -name '*_unittest.cc'. Keeping the
    identical rule means the Windows build cannot quietly miss a file that the Linux
    build compiles (or the reverse), which --check-sources then proves against a real
    Makefile.
    """
    sources = sorted(p for p in SRC_DIR.rglob("*.cc") if not p.name.endswith("_unittest.cc"))
    return sources


def wrapper_sources(cef_root: Path) -> list[Path]:
    """CEF's own binding layer, which every CEF client must compile."""
    root = cef_root / "libcef_dll"
    if not root.is_dir():
        # Very old distributions kept the wrapper elsewhere; the error is worth naming.
        raise BuildError(f"{root} is missing: the CEF distribution should contain "
                         "libcef_dll/ sources; re-fetch it")
    sources = sorted(p for p in root.rglob("*.cc") if "tests" not in p.parts)
    return sources


def tool_sources() -> list[Path]:
    return sorted((REPO_ROOT / "tools").glob("*.cc"))


# ---------------------------------------------------------------------------
# Command construction. Kept as pure functions so --dry-run prints exactly what
# a real run would execute: a dry run that takes a different path through the
# code is a dry run that proves nothing.
# ---------------------------------------------------------------------------
APP_DEFINES = (
    "UNICODE", "_UNICODE", "WIN32_LEAN_AND_MEAN", "NOMINMAX", "USING_CEF_SHARED",
    "_FILE_OFFSET_BITS=64",
)
# CEF's own binding layer is compiled with its own flag and nothing else - matching the
# Makefile's rule for it. This is not a style choice: the wrapper is what turns the C API
# libcef.dll exports into the C++ classes the browser uses, and it only does that when it
# is compiled this way.
WRAPPER_DEFINES = ("UNICODE", "_UNICODE", "WIN32_LEAN_AND_MEAN", "NOMINMAX",
                   "WRAPPING_CEF_SHARED")


def compile_command(cl: Path, source: Path, obj: Path, includes: list[Path],
                    defines: list[str], debug: bool, sdk: dict | None) -> list[str]:
    command = [
        str(cl), "/nologo", "/c", "/std:c++20", "/EHsc", "/W4",
        # /permissive-: MSVC's strict mode. The code is written for C++20 and built with
        # -Wall -Wextra on g++, so the same intent has to hold here.
        "/permissive-", "/Zc:__cplusplus", "/Zc:preprocessor",
        # /MD (dynamic runtime) is what CEF itself is built with on Windows. Mixing
        # runtimes is undefined behaviour that usually shows up as a crash in
        # libcef.dll rather than as a link error, so it is not a choice here.
        "/MD",
    ]
    if debug:
        command += ["/Od", "/Zi", "/FS"]
    else:
        command += ["/O2", "/GL", "/Gy", "/DNDEBUG"]
    for define in defines:
        command.append(f"/D{define}")
    for include in includes:
        command.append(f"/I{include}")
    command += [f"/Fo{obj}", str(source)]
    return command


def link_command(cl: Path, objects: list[Path], output: Path, libs: list[str],
                 lib_paths: list[Path], debug: bool, subsystem: str = "windows") -> list[str]:
    # cl.exe is used as the linker driver, exactly as a Visual Studio project does: it
    # finds link.exe beside itself and passes the object files through.
    command = [str(cl), "/nologo", "/Fe:" + str(output)]
    if debug:
        command += ["/DEBUG"]
    else:
        command += ["/LTCG", "/OPT:REF", "/OPT:ICF"]
    command += ["/SUBSYSTEM:" + subsystem.upper()]
    if subsystem == "windows":
        # src/app/main.cc has main(), not WinMain, and /SUBSYSTEM:WINDOWS looks for
        # WinMain by default - so the entry point is named explicitly. Without this the
        # link fails with "unresolved external symbol WinMain", which reads like a coding
        # mistake rather than a linker setting.
        command += ["/ENTRY:mainCRTStartup"]
    for path in lib_paths:
        command.append(f"/LIBPATH:{path}")
    command += [str(obj) for obj in objects]
    command += libs
    return command


def is_wrapper_source(source: Path, cef_root: Path) -> bool:
    """True for CEF's own binding layer rather than BrowserX code.

    These two kinds of file are not compiled the same way, and getting it wrong is not a
    style problem: see defines_for_source.
    """
    return cef_root in source.parents


def defines_for_source(source: Path, cef_root: Path,
                       app_defines: list[str] | None = None) -> list[str]:
    if is_wrapper_source(source, cef_root):
        return list(WRAPPER_DEFINES)
    return list(APP_DEFINES if app_defines is None else app_defines)


def includes_for_source(source: Path, cef_root: Path,
                        app_includes: list[str] | None = None) -> list[str]:
    if is_wrapper_source(source, cef_root):
        # The wrapper sees CEF's headers and nothing of ours: it must not be able to see
        # our code at all, which is exactly the rule the Makefile enforces for it.
        return [str(cef_root)]
    return list(app_includes or [str(SRC_DIR), str(cef_root)])


def cef_includes(cef_root: Path) -> list[Path]:
    return [cef_root]


def base_includes(cef_root: Path) -> list[Path]:
    return [SRC_DIR, cef_root]


# ---------------------------------------------------------------------------
# Incremental compilation.
#
# The POSIX build gets this from make. Without it, every Windows rebuild recompiled every
# file - including CEF's own binding layer, which is over a hundred translation units that
# never change - so a rebuild that should take a second took minutes. That is the thing a
# distributed build accelerator would be papering over, and it is fixable directly.
#
# How the decision is made, and what it can miss:
#   * A source is recompiled when the object is missing, when its recipe changed (compiler,
#     flags, defines, include paths, or the CEF pin), or when any file it includes is newer
#     than the object.
#   * Includes are found by scanning quoted #include directives transitively through the
#     project and CEF headers. Angle-bracket includes (the standard library, the Windows
#     SDK) are not scanned: they do not change between runs on one machine, and the recipe
#     hash covers the SDK path if it is ever replaced.
#   * CEF's headers are covered by the recipe hash (the pin, plus the newest mtime in
#     include/), so re-fetching CEF rebuilds everything without needing to walk its tree.
#   * If the cache is missing, unreadable, or older than the object it describes, the file
#     is compiled: the failure mode of this whole scheme has to be "do the work", never
#     "link something stale".
# ---------------------------------------------------------------------------
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"')


def scan_includes(start: Path, include_dirs: list[Path]) -> list[Path]:
    """Every file `start` pulls in through quoted includes, transitively.

    Quoted includes only, and unresolved ones are skipped rather than guessed at: a header
    this cannot find is a header whose mtime cannot be compared, and inventing a
    dependency would be worse than compiling one file too many.
    """
    seen: dict[Path, None] = {}
    stack = [start]
    while stack:
        current = stack.pop()
        if current in seen:
            continue
        seen[current] = None
        try:
            text = current.read_text(errors="replace")
        except OSError:
            continue
        for line in text.splitlines():
            match = INCLUDE_RE.match(line)
            if not match:
                continue
            name = match.group(1)
            # Quoted includes resolve against the including file's directory first, then
            # the include directories - which is what the compiler does.
            candidates = [current.parent / name] + [d / name for d in include_dirs]
            for candidate in candidates:
                if candidate.is_file():
                    stack.append(candidate)
                    break
    seen.pop(start, None)
    return sorted(seen)


def recipe_for(source: Path, cl: Path, includes: list[Path], defines: list[str],
               debug: bool, cef_root: Path, extra: str = "") -> str:
    """A hash of everything that changes what an object file should contain.

    If any of this changes, every object is rebuilt: a flag or an include path is not a
    detail, and an object compiled under a different recipe is not usable proof that the
    new one works.
    """
    parts = [str(cl), "debug" if debug else "release", *defines,
             *(str(p) for p in includes), str(cef_root), extra]
    return hashlib.sha1("\x00".join(parts).encode("utf-8", "replace")).hexdigest()


def cef_fingerprint(cef_root: Path) -> str:
    """A cheap stamp for the CEF headers: the pin, plus the newest header mtime."""
    stamp = ""
    lock = REPO_ROOT / "cef.lock.json"
    if lock.is_file():
        try:
            stamp += lock.read_text(errors="replace")
        except OSError:
            pass
    newest = 0.0
    include_dir = cef_root / "include"
    if include_dir.is_dir():
        for header in include_dir.rglob("*.h"):
            try:
                newest = max(newest, header.stat().st_mtime)
            except OSError:
                continue
    return stamp + f"|{newest:.0f}"


def deps_file(obj: Path) -> Path:
    return obj.with_suffix(obj.suffix + ".deps")


def read_deps(obj: Path) -> dict | None:
    path = deps_file(obj)
    if not path.is_file():
        return None
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None  # unreadable cache means "compile it", never "assume it is fine"
    if not isinstance(data, dict) or "recipe" not in data or "deps" not in data:
        return None
    return data


def write_deps(obj: Path, recipe: str, deps: list[Path]) -> None:
    try:
        deps_file(obj).write_text(
            json.dumps({"recipe": recipe, "deps": [str(p) for p in deps]}),
            encoding="utf-8")
    except OSError:
        pass  # a cache that cannot be written costs time, not correctness


def stale_reason(source: Path, obj: Path, recipe: str, fresh_deps: list[Path],
                 deps: dict | None = None) -> str:
    """Why this source must be compiled, or an empty string when the object is usable.

    Pure on purpose: the rule that decides whether a build happens is worth testing, and
    testing it must not require a compiler.
    """
    if not obj.is_file():
        return "no object file yet"
    cached = read_deps(obj) if deps is None else deps
    if cached is None:
        return "no dependency record (compiled before this existed)"
    if cached.get("recipe") != recipe:
        return "the compiler, flags or CEF changed"
    try:
        built_at = obj.stat().st_mtime
    except OSError:
        return "the object file is unreadable"
    # The source is its own first dependency. Leaving it out is the bug the self-test
    # caught: an edited .cc file would have been skipped, and the build would have linked
    # an object compiled from the old text - the failure mode this whole scheme exists to
    # avoid, and the one that is hardest to notice.
    for dependency in [source] + (fresh_deps or [Path(p) for p in cached.get("deps", [])]):
        try:
            if dependency.stat().st_mtime > built_at:
                return f"{dependency.name} changed"
        except OSError:
            return f"{dependency.name} is gone"
    return ""


# ---------------------------------------------------------------------------
# Running things
# ---------------------------------------------------------------------------
def run(command: list[str], capture: bool = True, cwd: Path | None = None,
        env: dict | None = None) -> tuple[int, str]:
    try:
        completed = subprocess.run(
            command, cwd=str(cwd or REPO_ROOT), env=env,
            stdout=subprocess.PIPE if capture else None,
            stderr=subprocess.STDOUT if capture else None,
            text=True, errors="replace")
    except FileNotFoundError as error:
        return 127, f"{error}"
    return completed.returncode, (completed.stdout or "") if capture else ""


def tail(text: str, lines: int = 12) -> str:
    kept = [line for line in text.splitlines() if line.strip()]
    return "\n".join(kept[-lines:])


def toolchain_environment(toolchain: dict) -> dict:
    """PATH with the compiler and SDK tools on it.

    cl.exe invokes link.exe from its own directory, but the Windows SDK's rc.exe and the
    CEF DLL's dependency resolution still want the SDK on PATH - so the environment is
    built here rather than inherited and hoped for.
    """
    env = dict(os.environ)
    parts = [str(toolchain["cl"].parent)]
    sdk = toolchain.get("sdk")
    if sdk:
        for version in (sdk["version"],):
            for sub in ("bin",):
                for arch in ("x64",):
                    candidate = sdk["include_root"].parent / "bin" / version / arch
                    if candidate.is_dir():
                        parts.append(str(candidate))
                del sub, arch
    env["PATH"] = os.pathsep.join(parts + [env.get("PATH", "")])
    return env


# ---------------------------------------------------------------------------
# The build itself
# ---------------------------------------------------------------------------
def plan_build(args) -> dict:
    """Everything the build needs, resolved and explained - before anything runs."""
    try:
        cef_root = find_cef_root()
    except BuildError:
        if not args.dry_run:
            raise
        # A dry run exists so the build can be inspected before anything is installed.
        # Printing the commands with a placeholder keeps that promise; failing here would
        # make --dry-run useless exactly when it is most useful.
        warn("no CEF distribution found: using a placeholder path below. Fetch it with"
             "  python tools\\fetch_cef.py  before a real build.")
        cef_root = DEPS_DIR / "cef_binary_<version>_windows64"
    sources = app_sources()
    if not sources:
        raise BuildError(f"no C++ sources found under {SRC_DIR}")
    if args.dry_run and not (cef_root / "libcef_dll").is_dir():
        wrapper = [cef_root / "libcef_dll" / "wrapper" / "cef_client.cc"]
    else:
        wrapper = wrapper_sources(cef_root)
    tools = tool_sources()
    if args.dry_run and not cef_lib_dirs(cef_root):
        libcef = cef_root / "Release" / "libcef.lib"
    else:
        libcef = find_libcef_lib(cef_root)
    return {"cef_root": cef_root, "sources": sources, "wrapper": wrapper, "tools": tools,
            "libcef": libcef}


def object_path(source: Path, root: Path = REPO_ROOT, obj_root: Path = OBJ_DIR) -> Path:
    return obj_root / source.relative_to(root).with_suffix(".obj")


def build(args) -> int:
    started = time.time()
    if platform.system() != "Windows" and not args.dry_run:
        raise BuildError(
            "this builds Windows binaries with MSVC, and this machine is "
            f"{platform.system()}. On Linux use `make`; to inspect the Windows build "
            "without running it, use --dry-run.")

    plan = plan_build(args)
    cef_root: Path = plan["cef_root"]
    toolchain = {}
    cl = Path("cl.exe")
    if args.dry_run:
        # A dry run still needs a believable compiler path, so that the printed commands
        # are the ones that would run on a real Windows machine.
        toolchain = {
            "cl": Path(r"C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
                       r"\VC\Tools\MSVC\14.4x.x\bin\Hostx64\x64\cl.exe"),
            "msvc": Path(r"C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
                         r"\VC\Tools\MSVC\14.4x.x"),
            "sdk": {"version": "10.0.26100.0",
                    "include_root": Path(r"C:\Program Files (x86)\Windows Kits\10\Include"),
                    "lib_root": Path(r"C:\Program Files (x86)\Windows Kits\10\Lib")}}
        cl = toolchain["cl"]
    else:
        toolchain = find_msvc()
        cl = toolchain["cl"]

    defines = list(APP_DEFINES) + [f'BROWSERX_VERSION="{version_string()}"']
    includes = base_includes(cef_root) if args.dry_run else base_includes(cef_root)
    sdk = toolchain.get("sdk")

    # The SDK's um/shared/ucrt include roots are what make <windows.h> and <cstdint> work;
    # MSVC's own include directory covers the standard library.
    if sdk:
        includes = includes + [sdk["include_root"] / sdk["version"] / part
                               for part in ("ucrt", "shared", "um")]
        includes = includes + [toolchain["msvc"] / "include"]

    sources = plan["sources"] + plan["wrapper"]
    objects = [object_path(source) for source in sources]

    def defines_for(source: Path) -> list[str]:
        return defines_for_source(source, cef_root, defines)

    def includes_for(source: Path) -> list[str]:
        return includes_for_source(source, cef_root, [str(path) for path in includes])
    output = OUT_DIR / "BrowserX.exe"

    say("")
    say("  BrowserX — Windows build (MSVC)")
    say(f"  repository : {REPO_ROOT}")
    say(f"  CEF        : {cef_root}")
    say(f"  compiler   : {cl}")
    if sdk:
        say(f"  Windows SDK: {sdk.get('version', 'unknown')}")
        note = sdk_note(str(sdk.get("version", "")))
        if note:
            warn(note)
    say(f"  sources    : {len(plan['sources'])} browser + {len(plan['wrapper'])} CEF wrapper"
        f" + {len(plan['tools'])} tools")
    say(f"  config     : {'debug' if args.debug else 'release'}, jobs {args.jobs}")
    say("")

    if args.dry_run:
        warn("dry run: nothing below this line is executed")
        # One of each kind, because they are not compiled the same way: an application
        # source, a tool, and CEF's wrapper (whose flags differ on purpose). Printing
        # three app sources would hide the difference a reader most needs to see.
        sample = sources[:2] + ([sources[-1]] if len(sources) > 2 else []) + plan["wrapper"][:1]
        for source in sample:
            say("  " + " ".join(compile_command(cl, source, object_path(source),
                                                [Path(p) for p in includes_for(source)],
                                                defines_for(source), args.debug, sdk)))
        say(f"  … and the same for the other {len(sources) - len(sample)} sources")
        link_libs = [str(plan["libcef"])] + list(SYSTEM_LIBS)
        link_paths = list(cef_lib_dirs(cef_root))
        if sdk:
            link_paths += [sdk["lib_root"] / sdk["version"] / part / "x64"
                           for part in ("ucrt", "um")]
        say("  " + " ".join(link_command(cl, objects, output, link_libs, link_paths,
                                         args.debug)))
        say("")
        say("  finished (dry run)")
        return EXIT_OK

    # --- compile -----------------------------------------------------------
    env = toolchain_environment(toolchain)
    OBJ_DIR.mkdir(parents=True, exist_ok=True)
    for obj in objects:
        obj.parent.mkdir(parents=True, exist_ok=True)

    # Decide what actually needs compiling, before compiling anything.
    cef_stamp = cef_fingerprint(cef_root)
    to_compile: list[tuple[Path, Path, str, list[Path]]] = []   # source, obj, recipe, deps
    reused = 0
    for source in sources:
        obj = object_path(source)
        include_paths = [Path(p) for p in includes_for(source)]
        recipe = recipe_for(source, cl, include_paths, defines_for(source), args.debug,
                            cef_root, cef_stamp)
        if args.force:
            reason = "--force"
        else:
            reason = stale_reason(source, obj, recipe, [])
        if reason:
            # Scanning includes is the expensive part, so it is done only for the files
            # that are going to be compiled anyway - and the result is cached with the
            # object for the next run to compare against.
            to_compile.append((source, obj, recipe, scan_includes(source, include_paths)))
            if args.verbose:
                say(f"      compiling {source.relative_to(REPO_ROOT)} — {reason}")
        else:
            reused += 1

    if not to_compile:
        step(f"nothing to compile: all {len(sources)} objects are up to date")
    else:
        step(f"compiling {len(to_compile)} of {len(sources)} sources with {args.jobs} job(s)"
             + (f", reusing {reused}" if reused else ""))

    failures: list[tuple[Path, str]] = []

    def compile_one(item: tuple[Path, Path, str, list[Path]]) -> tuple[Path, int, str, list[Path], str]:
        source, obj, recipe, deps = item
        command = compile_command(cl, source, obj, [Path(p) for p in includes_for(source)],
                                  defines_for(source), args.debug, sdk)
        code, text = run(command, capture=True, env=env)
        return source, code, text, deps, recipe

    done = 0
    if to_compile:
        with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
            for source, code, text, deps, recipe in pool.map(compile_one, to_compile):
                done += 1
                if code != 0:
                    failures.append((source, text))
                else:
                    # The record is written only after a successful compile, so a failed
                    # one is always retried rather than remembered as done.
                    write_deps(object_path(source), recipe, deps)
                    if args.verbose:
                        say(f"      [{done}/{len(to_compile)}] "
                            f"{source.relative_to(REPO_ROOT)}")

    if failures:
        for source, text in failures[: args.max_errors]:
            say("")
            say(f"    ✘ {source.relative_to(REPO_ROOT)}")
            for line in tail(text, 10).splitlines():
                say(f"      {line}")
        say("")
        raise BuildError(f"{len(failures)} source file(s) failed to compile "
                         f"(showing up to {args.max_errors})")
    ok(f"{len(to_compile)} sources compiled"
       + (f", {reused} reused from the last build" if reused else ""))

    # --- link --------------------------------------------------------------
    lib_paths = cef_lib_dirs(cef_root)
    if sdk:
        lib_paths += [sdk["lib_root"] / sdk["version"] / part / "x64"
                      for part in ("ucrt", "um")]
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    step("linking out\\BrowserX.exe")
    code, text = run(link_command(cl, objects, output,
                                  [str(plan["libcef"])] + list(SYSTEM_LIBS),
                                  lib_paths, args.debug), capture=True, env=env)
    if code != 0:
        say("")
        for line in tail(text, 20).splitlines():
            say(f"    {line}")
        say("")
        raise BuildError("the link failed - the messages above are from link.exe")
    ok(f"out\\BrowserX.exe ({human_size(output.stat().st_size)})")

    # --- stage the runtime next to the exe --------------------------------
    if args.stage:
        staged = stage_runtime(cef_root, OUT_DIR, quiet=args.quiet)
        if staged:
            ok(f"{staged} CEF runtime file(s)/folder(s) staged next to the exe")
        else:
            warn("nothing staged: no CEF runtime files were found in the distribution")
    else:
        warn("not staged: run with --stage so libcef.dll sits next to the exe, or the "
             "browser will not start")

    say("")
    say(f"  built in {time.time() - started:.1f}s → {output}")
    say(f"  run it:        out\\BrowserX.exe")
    say(f"  then update:   tools\\update.cmd")
    say("")
    return EXIT_OK


def stage_runtime(cef_root: Path, out_dir: Path, quiet: bool = False) -> int:
    """Copies the CEF runtime next to the executable, because Windows has no rpath."""
    staged = 0
    # Where the distribution keeps these varies: some versions have Release/ with the
    # DLLs, others put them at the top level.
    search_dirs = [cef_root / "Release", cef_root]
    resources = cef_root / "Resources"
    if resources.is_dir():
        search_dirs.append(resources)

    for name in CEF_RUNTIME_FILES:
        for directory in search_dirs:
            candidate = directory / name
            if candidate.is_file():
                shutil.copy2(candidate, out_dir / name)
                staged += 1
                if not quiet:
                    say(f"      {name}")
                break
    for name in CEF_RUNTIME_DIRS:
        for directory in search_dirs:
            candidate = directory / name
            if candidate.is_dir():
                target = out_dir / name
                if target.exists():
                    shutil.rmtree(target)
                shutil.copytree(candidate, target)
                staged += 1
                if not quiet:
                    say(f"      {name}\\")
                break
    return staged


def human_size(count: float) -> str:
    for unit in ("B", "KB", "MB", "GB"):
        if count < 1024 or unit == "GB":
            return f"{count:.0f} {unit}" if unit == "B" else f"{count:.1f} {unit}"
        count /= 1024
    return f"{count:.1f} GB"


def version_string() -> str:
    """The version the binary reports, taken from the same pin the POSIX build reads."""
    lock = REPO_ROOT / "cef.lock.json"
    if lock.is_file():
        try:
            data = json.loads(lock.read_text(encoding="utf-8"))
            if data.get("version"):
                return str(data["version"])
        except (OSError, ValueError):
            pass
    return "dev"


# ---------------------------------------------------------------------------
# --check-portability: the other way a Windows build fails. A POSIX header
# included outside a platform guard compiles on Linux and dies on Windows at
# the first line that uses it, in a file that looks fine.
# ---------------------------------------------------------------------------
POSIX_ONLY_HEADERS = (
    "unistd.h", "sys/", "pthread.h", "dirent.h", "dlfcn.h", "netinet/", "arpa/",
    "termios.h", "syslog.h", "poll.h", "spawn.h", "grp.h", "pwd.h",
)
PLATFORM_GUARD = re.compile(r"_WIN32|__linux__|_POSIX|__APPLE__|WIN32_LEAN_AND_MEAN")


def check_portability(args) -> int:
    """Finds POSIX-only includes that are not inside a platform guard.

    Deliberately narrow: it reports what cannot compile on Windows, not style. A check
    that cried wolf about `snprintf` (which MSVC has had since 2015) would be turned off
    within a week, and then it would catch nothing.
    """
    problems: list[tuple[str, int, str]] = []
    files = app_sources() + tool_sources()
    for path in files:
        stack: list[str] = []
        for number, line in enumerate(path.read_text(errors="replace").splitlines(), start=1):
            stripped = line.strip()
            if stripped.startswith("#"):
                directive = re.match(r"#\s*(if|ifdef|ifndef|endif)\b(.*)", stripped)
                if directive:
                    if directive.group(1) == "endif":
                        if stack:
                            stack.pop()
                    else:
                        stack.append(directive.group(2))
                    continue
                included = re.match(r'#\s*include\s*[<"]([^">]+)[">]', stripped)
                if included:
                    header = included.group(1)
                    if any(header == posix or header.startswith(posix)
                           for posix in POSIX_ONLY_HEADERS):
                        if not any(PLATFORM_GUARD.search(frame) for frame in stack):
                            problems.append((str(path.relative_to(REPO_ROOT)), number, header))

    say("")
    if not problems:
        ok(f"no POSIX-only include outside a platform guard in {len(files)} file(s)")
        say("")
        return EXIT_OK
    say(f"    ✘ {len(problems)} POSIX-only include(s) that would break the Windows build:")
    for name, number, header in problems:
        say(f"        {name}:{number}  {header}")
    say("")
    say("      Wrap each one in #if !defined(_WIN32), with the Windows path beside it.")
    say("")
    return EXIT_FAILED


# ---------------------------------------------------------------------------
# --check-sources: the honest part. A second build system's usual failure is
# compiling a different set of files from the first one.
# ---------------------------------------------------------------------------
def check_sources(args) -> int:
    mine = {p.relative_to(REPO_ROOT).as_posix() for p in app_sources()}

    makefile = REPO_ROOT / "Makefile"
    if not makefile.is_file():
        warn("no Makefile here: nothing to compare against, so this check cannot say "
             "anything useful")
        return EXIT_NOTHING_LEARNED

    # Run the Makefile's own discovery rule rather than a copy of it: if the two ever
    # diverge, this check must notice, and a copy would diverge with it.
    code, output = run(["make", "-s", "print-app-sources"], capture=True)
    if code != 0 or not output.strip():
        warn("this Makefile has no 'print-app-sources' target, so the POSIX source list "
             "cannot be read; add it, or compare by hand")
        return EXIT_NOTHING_LEARNED
    theirs = {line.strip() for line in output.splitlines() if line.strip()}

    missing_here = sorted(theirs - mine)
    missing_there = sorted(mine - theirs)

    say("")
    if not missing_here and not missing_there:
        ok(f"both builds compile the same {len(mine)} source files")
        say("")
        return EXIT_OK
    if missing_here:
        say(f"    ✘ {len(missing_here)} file(s) the POSIX build compiles and this one does not:")
        for name in missing_here[:20]:
            say(f"        {name}")
    if missing_there:
        say(f"    ✘ {len(missing_there)} file(s) this build compiles and the POSIX one does not:")
        for name in missing_there[:20]:
            say(f"        {name}")
    say("")
    return EXIT_FAILED


# ---------------------------------------------------------------------------
def self_test() -> int:
    """Checks the parts of the Windows build that do not need Windows.

    The compiler and the linker can only be exercised on a Windows machine, but the
    decisions around them - which SDK wins, which files are compiled the wrapper's way,
    which sources are compiled at all - are where a second build system usually goes
    wrong, and all of those are checkable anywhere.
    """
    failures = 0

    def expect(condition: bool, what: str) -> None:
        nonlocal failures
        print(f"    {'✔' if condition else '✘'} {what}")
        if not condition:
            failures += 1

    print("\n  BrowserX — Windows build self-test\n")

    # --- choosing an SDK, which is what the user is asked to install ---------------
    offered = ["10.0.22621.0", "10.0.26100.8249", "10.0.28000.2705"]
    expect(max(offered, key=sdk_version_key) == "10.0.28000.2705",
           "the newest SDK wins by version, not by name order")
    # The bug that makes this worth a test: "10.0.10000.0" < "10.0.9999.0" as text.
    expect(max(["10.0.9999.0", "10.0.10000.0"], key=sdk_version_key) == "10.0.10000.0",
           "10.0.10000.0 beats 10.0.9999.0 (string sorting would pick the wrong one)")
    expect(sdk_version_key("10.0.26100.0") == [10, 0, 26100, 0],
           "a version parses into its numeric parts")
    expect(sdk_version_key("nonsense") == [0], "an unparsable version does not crash the sort")

    # Choosing a preview SDK silently is the failure this note exists to prevent: the
    # number alone is all the build can know, so it says what it knows and no more.
    expect(sdk_note(RECOMMENDED_SDK) == "", "the recommended SDK needs no note")
    expect(sdk_note("10.0.22621.0") == "", "an older SDK needs no note")
    expect("newer than the SDK this project targets" in sdk_note("10.0.28000.2705"),
           "a newer SDK - the usual shape of an Insider preview - is named, not used silently")
    expect("Windows Insider preview" in sdk_note("10.0.28000.2705"),
           "the note says what a version above the released one usually means")
    expect(sdk_note("") == "", "no SDK, nothing to say")
    # The false alarm this caught: a full SDK directory name carries a fourth component,
    # and comparing all four makes 10.0.26100.8249 "newer" than 10.0.26100.
    expect(sdk_note("10.0.26100.8249") == "",
           "the same SDK named with its build component is not a newer SDK")
    expect(sdk_note("10.0.26100.0") == "", "nor is the zeroed form")

    # --- CEF's wrapper is not compiled like our code -------------------------------
    cef = Path("/tmp/fake-cef")
    wrapper = cef / "libcef_dll" / "wrapper" / "cef_client.cc"
    ours = REPO_ROOT / "src" / "core" / "format.cc"
    expect(is_wrapper_source(wrapper, cef) and not is_wrapper_source(ours, cef),
           "CEF's wrapper is recognised as CEF's, and our sources are not")
    wrapper_defines = defines_for_source(wrapper, cef, ["USING_CEF_SHARED", "UNICODE"])
    expect("WRAPPING_CEF_SHARED" in wrapper_defines,
           "the wrapper is compiled with WRAPPING_CEF_SHARED (linking fails without it)")
    expect("USING_CEF_SHARED" not in wrapper_defines,
           "the wrapper is not given the application's defines")
    expect(defines_for_source(ours, cef, ["USING_CEF_SHARED"]) == ["USING_CEF_SHARED"],
           "our sources keep the application defines")
    expect(includes_for_source(wrapper, cef, ["src", "cef"]) == [str(cef)],
           "the wrapper cannot see our headers at all")

    # --- incremental compilation ----------------------------------------------------
    # Tested with real files, because the whole point of this logic is what the filesystem
    # says about time - and because the failure that matters is linking something stale.
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "inc").mkdir()
        header = root / "inc" / "thing.h"
        deep = root / "inc" / "deep.h"
        source = root / "a.cc"
        obj = root / "a.obj"
        header.write_text("#pragma once\n#include \"deep.h\"\n")
        deep.write_text("#pragma once\nint deep();\n")
        source.write_text('#include "thing.h"\n#include <vector>\n')

        deps = scan_includes(source, [root / "inc"])
        expect(set(deps) == {header, deep},
               "a source's dependencies include the headers it pulls in, transitively")
        expect(source not in deps, "a source is not its own dependency")

        recipe = "recipe-1"
        expect(stale_reason(source, obj, recipe, deps) == "no object file yet",
               "a missing object is compiled")
        obj.write_text("object")
        expect("no dependency record" in stale_reason(source, obj, recipe, deps),
               "an object with no dependency record is compiled (the cache cannot vouch "
               "for it)")
        write_deps(obj, recipe, deps)
        def age(path: Path, seconds: float) -> None:
            stamp = time.time() + seconds
            os.utime(path, (stamp, stamp))

        age(source, -60); age(header, -60); age(deep, -60); age(obj, 0)
        expect(stale_reason(source, obj, recipe, deps) == "",
               "an up-to-date object is reused: nothing is compiled")
        age(source, 30)
        expect("a.cc changed" in stale_reason(source, obj, recipe, []),
               "an edited source is recompiled, naming the file - which the cached path "
               "must notice on its own")
        age(source, -60); age(deep, 30)
        expect("deep.h changed" in stale_reason(source, obj, recipe, []),
               "a change in a header two levels up recompiles the dependent source")
        age(deep, -60)
        expect("compiler, flags or CEF changed" in stale_reason(source, obj, "recipe-2", deps),
               "a changed recipe (compiler, flags, CEF) rebuilds everything")
        (root / "gone.h").write_text("")
        write_deps(obj, recipe, deps + [root / "gone.h"])
        (root / "gone.h").unlink()
        # Asked the way the build asks on a second run: no fresh scan, so the cached list
        # is what has to notice.
        expect("gone.h is gone" in stale_reason(source, obj, recipe, []),
               "a deleted header recompiles what included it, instead of trusting the cache")

        # A corrupted cache must mean "compile it", never "assume it is fine".
        deps_file(obj).write_text("{ this is not json")
        expect("no dependency record" in stale_reason(source, obj, recipe, deps),
               "an unreadable cache is treated as no cache")
        deps_file(obj).write_text('{"recipe": "recipe-1"}')
        expect("no dependency record" in stale_reason(source, obj, recipe, deps),
               "a cache missing its dependency list is not trusted")

        # The recipe must actually notice the things that change an object's meaning.
        cl = Path("C:/fake/cl.exe")
        base = recipe_for(source, cl, [root / "inc"], ["UNICODE"], False, root / "cef", "pin")
        for name, other in (
                ("a different define", recipe_for(source, cl, [root / "inc"], ["UNICODE", "X"],
                                                  False, root / "cef", "pin")),
                ("release to debug", recipe_for(source, cl, [root / "inc"], ["UNICODE"], True,
                                                root / "cef", "pin")),
                ("a different include path", recipe_for(source, cl, [root], ["UNICODE"],
                                                        False, root / "cef", "pin")),
                ("a re-fetched CEF", recipe_for(source, cl, [root / "inc"], ["UNICODE"],
                                                False, root / "cef", "pin-2")),
                ("a different compiler", recipe_for(source, Path("C:/other/cl.exe"),
                                                    [root / "inc"], ["UNICODE"], False,
                                                    root / "cef", "pin"))):
            expect(base != other, f"the recipe changes when there is {name}")

    # --- the source rule -----------------------------------------------------------
    sources = app_sources()
    expect(bool(sources), "the source rule finds the browser's sources")
    expect(not any(p.name.endswith("_unittest.cc") for p in sources),
           "no *_unittest.cc is compiled into the browser")
    expect(all(p.suffix == ".cc" for p in sources), "only .cc files are compiled")

    # --- the runtime that has to sit beside the exe --------------------------------
    expect("libcef.dll" in CEF_RUNTIME_FILES,
           "libcef.dll is staged: Windows has no rpath, so the exe needs it next to it")
    expect(any(name.endswith("v8_context_snapshot.bin") for name in CEF_RUNTIME_FILES),
           "the V8 snapshot and ICU data are staged too (no snapshot, no JavaScript)")

    # The bug this catches: the incremental build used a local named `plan`, which is
    # already the resolved build plan (CEF root, sources, libcef). The linker then asked
    # that list for libcef and would have failed on the first real build, while a dry run
    # returned before the line that used it - so nothing would have noticed until someone
    # with MSVC installed ran the build.
    source_text = Path(__file__).read_text(errors="replace")
    body = source_text[source_text.index("def build(args)"):]
    body = body[:body.index("\ndef ")] if "\ndef " in body else body
    shadowing = [m.group(1) for m in re.finditer(r"^\s{4}(\w+)\s*:[^=]*=\s*\[\]|"
                                                 r"^\s{4}(\w+)\s*=\s*plan_build", body, re.M)]
    expect("libcef" in body, "the link step still asks the build plan for libcef")
    expect(shadowing.count("plan") <= 1,
           "the resolved build plan is not shadowed by a local of the same name")

    print()
    if failures:
        print(f"  {failures} self-test failure(s)\n")
        return EXIT_FAILED
    print("  self-test passed\n")
    return EXIT_OK


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="build_windows.py",
        description="Build BrowserX on Windows with MSVC (same sources as the POSIX "
                    "build, different compiler).")
    parser.add_argument("--debug", action="store_true", help="debug build (/Od /Zi)")
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2)),
                        help="parallel compilations (default: one per CPU)")
    parser.add_argument("--stage", action="store_true",
                        help="copy the CEF runtime files next to the exe")
    parser.add_argument("--dry-run", action="store_true",
                        help="print the commands instead of running them")
    parser.add_argument("--check-sources", action="store_true",
                        help="prove this build compiles the same sources as the Makefile")
    parser.add_argument("--force", action="store_true",
                        help="compile every source, ignoring the incremental cache")
    parser.add_argument("--check-portability", action="store_true",
                        help="find POSIX-only includes that would break this build")
    parser.add_argument("--self-test", action="store_true",
                        help="check the platform-independent build logic (no MSVC needed)")
    parser.add_argument("--verbose", action="store_true", help="name every file as it compiles")
    parser.add_argument("--quiet", action="store_true", help="less output")
    parser.add_argument("--max-errors", type=int, default=5,
                        help="how many failing files to show in full (default 5)")
    argv, pasted_note = argv_notes.strip_trailing_comment(argv)
    if pasted_note:
        say(f"note: {pasted_note}")
    args = parser.parse_args(argv)

    try:
        if args.self_test:
            return self_test()
        if args.check_sources:
            return check_sources(args)
        if args.check_portability:
            return check_portability(args)
        return build(args)
    except BuildError as error:
        say("")
        lines = str(error).splitlines()
        for index, line in enumerate(lines):
            say(f"  {'✘' if index == 0 else ' '} {line}")
        say("")
        return EXIT_FAILED
    except KeyboardInterrupt:
        say("\n  interrupted")
        return EXIT_FAILED


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
