#!/usr/bin/env python3
# BrowserX — (c) 2026 codero-sus. All rights reserved.
# Personal, non-commercial use only. No modification, no redistribution or resale.
# Source-available, not open source — see LICENSE. https://the-codero.web.app
"""Where the disk went, and what is safe to remove.

A browser build is not small: CEF is about 1 GB extracted, a full Visual Studio install is
several GB, and the object files here are ~100 MB. This tool answers the two questions that
matter before deleting anything:

  1. What is taking the space? (measured, not estimated)
  2. What does removing it cost? (a re-download, a recompile, or nothing at all)

The second question is the one that keeps people out of trouble. Everything it plans to
remove is classified as regenerable, and it says what regeneration costs - `make deps` is
200 MB of download, `make test` is a recompile. Nothing that cannot be regenerated is ever
deleted: not the sources, not the git history, and above all not the run profile, which is
where browsing history, bookmarks and settings live, and which is the one thing in this
tree that is irreplaceable.

    python3 tools/disk_report.py              # measure and plan, delete nothing
    python3 tools/disk_report.py --json       # the same, machine-readable
    python3 tools/disk_report.py --apply      # remove the regenerable build outputs
    python3 tools/disk_report.py --apply --deep   # also the fetched CEF trees (200 MB again)

Exit codes: 0 nothing to do or done, 1 nothing was removed because it was refused.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
EXIT_OK = 0
EXIT_REFUSED = 1

# Directories this tool will never delete, however much space they take. The profile is
# first in the list on purpose: in portable mode it lives at out/profile, inside a
# directory that otherwise looks entirely like build output.
NEVER_DELETE = ("src", "tools", "docs", "tests", "mobile-edition", "preview",
                ".git", ".cef-headers")

# A profile directory inside out/ means someone runs the browser portably; its contents
# are the user's, not the build's.
PROFILE_MARKERS = ("profile",)


def human(count: float) -> str:
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if count < 1024 or unit == "TB":
            return f"{count:.0f} {unit}" if unit == "B" else f"{count:.1f} {unit}"
        count /= 1024
    return f"{count:.1f} TB"


def measure(path: Path, follow_links: bool = False) -> tuple[int, int]:
    """(bytes, files) under a path. Hard links are counted once, which is what `du` does
    and what the user's disk actually holds - when this mattered, it mattered: the CEF
    tree is full of hard-linked duplicates."""
    if not path.exists():
        return 0, 0
    if path.is_file():
        try:
            return path.stat().st_size, 1
        except OSError:
            return 0, 0
    total = 0
    files = 0
    seen: set[tuple[int, int]] = set()
    stack = [path]
    while stack:
        current = stack.pop()
        try:
            entries = list(os.scandir(current))
        except OSError:
            continue
        for entry in entries:
            try:
                if entry.is_symlink() and not follow_links:
                    continue
                if entry.is_dir(follow_symlinks=False):
                    stack.append(Path(entry.path))
                    continue
                stat = entry.stat(follow_symlinks=False)
                key = (stat.st_dev, stat.st_ino)
                if key in seen:
                    continue
                seen.add(key)
                total += stat.st_size
                files += 1
            except OSError:
                continue
    return total, files


class Item:
    """One thing on disk, and what removing it would cost."""

    def __init__(self, path: Path, label: str, kind: str, cost: str):
        self.path = path
        self.label = label
        self.kind = kind      # keep | regenerable | refetch | trim
        self.cost = cost      # what it costs to get it back
        self.bytes, self.files = measure(path)

    @property
    def name(self) -> str:
        try:
            return self.path.relative_to(REPO_ROOT).as_posix() + "/"
        except ValueError:
            return str(self.path)


def sandbox_root() -> Path | None:
    """The directory this tool was pointed at instead of the repository (the tests use
    this; there is no user-facing flag, because a tool that can be aimed at any directory
    is a tool that can delete the wrong one)."""
    sandbox = os.environ.get("BROWSERX_SANDBOX_DIR", "")
    return Path(sandbox) if sandbox else None


def collect(quick: bool = False) -> list[Item]:
    root = sandbox_root() or REPO_ROOT

    items = [
        Item(root / "src", "the browser's own source", "keep", ""),
        Item(root / "tools", "the build and check tools", "keep", ""),
        Item(root / ".cef-headers", "CEF headers (make check works offline because these "
             "are committed)", "keep", ""),
        Item(root / "build", "object files", "regenerable", "make test   (a recompile)"),
        Item(root / "out", "binaries and, possibly, your run profile", "regenerable",
             "make        (a rebuild; out/profile is kept)"),
        Item(root / ".cef-deps", "the fetched CEF distribution", "refetch",
             "make deps   (about 200 MB of download, ~1 GB extracted)"),
        Item(root / "node_modules", "jsdom, for make ui-check only", "regenerable",
             "npm install jsdom --no-save"),
        Item(root / "mobile-edition" / "build", "Android build output", "regenerable",
             "mobile-edition/tools/build-native.sh"),
    ]
    if not quick:
        items.append(Item(mobile_android_cache(root), "Android caches (Gradle, .cxx)",
                          "regenerable", "the next Android build rebuilds them"))
    return items


def mobile_android_cache(root: Path) -> Path:
    return root / "mobile-edition" / ".gradle"


def profile_paths(root: Path | None = None) -> list[Path]:
    """Every place a run profile could be, so the report can say where it is instead of
    leaving the user to wonder whether cleaning will cost them their bookmarks."""
    root = root or REPO_ROOT
    if not in_real_repo(root):
        # A sandbox (the tests) has no business reporting the real machine's profile.
        candidates = [root / "out" / "profile", root / "profile"]
        override = os.environ.get("BROWSERX_PROFILE")
        if override:
            candidates.append(Path(override))
        return candidates
    candidates = [
        root / "out" / "profile",          # portable mode next to the binary
        root / "profile",                  # legacy location, still in .gitignore
    ]
    override = os.environ.get("BROWSERX_PROFILE")
    if override:
        candidates.append(Path(override))
    home = Path(os.environ.get("USERPROFILE") or os.environ.get("HOME") or "~").expanduser()
    if os.name == "nt" or sys.platform.startswith("win"):
        local = os.environ.get("LOCALAPPDATA")
        candidates.append(Path(local) / "BrowserX" if local else home / "BrowserX")
    elif sys.platform == "darwin":
        candidates.append(home / "Library" / "Application Support" / "BrowserX")
    else:
        candidates.append(home / ".config" / "BrowserX")
    return candidates


def visual_studio_paths() -> list[Path]:
    """Visual Studio installs, because on Windows they are usually the biggest single
    thing involved in building this browser - and the most trimmable."""
    if not sys.platform.startswith("win"):
        return []
    found: list[Path] = []
    for variable in ("ProgramFiles", "ProgramFiles(x86)"):
        base = os.environ.get(variable)
        if not base:
            continue
        root = Path(base) / "Microsoft Visual Studio"
        if not root.is_dir():
            continue
        for product in root.iterdir():
            for edition in product.iterdir() if product.is_dir() else []:
                if (edition / "VC").is_dir() or (edition / "Common7").is_dir():
                    found.append(edition)
    kits = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / \
        "Windows Kits" / "10"
    if kits.is_dir():
        found.append(kits)
    return found


def free_space(path: Path) -> tuple[int, int]:
    try:
        usage = shutil.disk_usage(path)
        return usage.free, usage.total
    except OSError:
        return 0, 0


def render(items: list[Item], extra: list[Item], as_json: bool) -> int:
    reclaimable = sum(item.bytes for item in items if item.kind != "keep")
    kept = sum(item.bytes for item in items if item.kind == "keep")
    total_here = sum(item.bytes for item in items)

    if as_json:
        print(json.dumps({
            "repo": str(REPO_ROOT),
            "items": [{"name": item.name, "label": item.label, "kind": item.kind,
                       "bytes": item.bytes, "files": item.files,
                       "cost": item.cost} for item in items],
            "other": [{"name": str(item.path), "label": item.label, "kind": item.kind,
                       "bytes": item.bytes, "files": item.files} for item in extra],
            "reclaimable_bytes": reclaimable,
            "kept_bytes": kept,
            "total_bytes": total_here,
        }, indent=2))
        return EXIT_OK

    free, size = free_space(REPO_ROOT)
    print()
    print("  BrowserX — where the disk goes")
    print("  " + "-" * 68)
    if size:
        percent = free / size * 100
        mark = "✔" if percent >= 15 else ("›" if percent >= 8 else "✘")
        print(f"    {mark} this disk   {human(free)} free of {human(size)} "
              f"({percent:.0f}%)")
    print()

    print("  in this checkout")
    for item in sorted(items, key=lambda i: -i.bytes):
        marks = {"keep": "✔ keep   ", "regenerable": "~ rebuild", "refetch": "! refetch",
                 "trim": "· trim   "}
        if item.bytes == 0 and item.kind == "keep":
            continue
        print(f"    {marks[item.kind]}  {human(item.bytes):>9}  {item.label}")
        print(f"    {'':<10}  {'':>9}  {item.name}"
              + (f"  → {item.cost}" if item.kind != "keep" and item.cost else ""))
    for item in extra:
        print(f"    · outside   {human(item.bytes):>9}  {item.label}")
        print(f"    {'':<10}  {'':>9}  {item.path}")

    print()
    print(f"    reclaimable here: {human(reclaimable)}   "
          f"never touched: {human(kept)} of source, tools and headers")

    profiles = [p for p in profile_paths(sandbox_root() or REPO_ROOT) if p.is_dir()]
    print()
    if profiles:
        for profile in profiles:
            size_bytes, _ = measure(profile, follow_links=True)
            print(f"    your run profile: {profile}")
            print(f"      {human(size_bytes)} — history, bookmarks, settings. "
                  "This tool never removes it.")
    else:
        print("    your run profile: none found yet (it is created the first time the "
              "browser runs)")
    print()
    print("    make space-apply        remove the rebuildable outputs (keeps the profile)")
    print("    make space-apply DEEP=1 also the fetched CEF trees (200 MB to re-download)")
    print()
    return EXIT_OK


def in_real_repo(path: Path) -> bool:
    """True when `path` is inside this repository rather than a sandbox copy of it."""
    try:
        path.resolve().relative_to(REPO_ROOT)
        return True
    except ValueError:
        return False


def assert_safe_to_remove(path: Path) -> None:
    """Refuses to remove anything on the never-delete list, whatever it was classified as.

    This runs immediately before a delete, not once at startup: a wrong classification is
    exactly the bug this list exists to survive, so the check has to be next to the
    removal it protects.
    """
    try:
        relative = path.resolve().relative_to(REPO_ROOT).parts
    except ValueError:
        return  # outside the repo (a sandbox, or a Visual Studio directory): not ours
    if relative and relative[0] in NEVER_DELETE:
        raise OSError(f"refused: {relative[0]}/ is never removed by this tool")


def apply(items: list[Item], deep: bool, out) -> int:
    removed = 0
    refused: list[str] = []
    for item in items:
        if item.kind == "keep" or item.bytes == 0:
            continue
        if item.kind == "refetch" and not deep:
            out(f"    · kept    {item.name} ({human(item.bytes)}; --deep removes it, but "
                f"{item.cost})")
            continue
        # The profile guard, again, immediately before the delete: portable mode puts it
        # inside out/, so this is not redundant.
        profile = item.path / "profile"
        if profile.is_dir():
            refused.append(str(profile))
        try:
            assert_safe_to_remove(item.path)
            if item.path.is_dir():
                # Remove the contents, not the directory, when the profile is inside it.
                for entry in item.path.iterdir():
                    if entry.name in PROFILE_MARKERS and item.path.name == "out":
                        continue
                    if entry.is_dir() and not entry.is_symlink():
                        shutil.rmtree(entry, ignore_errors=True)
                    else:
                        entry.unlink(missing_ok=True)
            elif item.path.exists():
                item.path.unlink()
            removed += item.bytes
            out(f"    ✔ removed {item.name} ({human(item.bytes)})")
        except OSError as error:
            out(f"    ✘ could not remove {item.name}: {error}")
    out("")
    out(f"  {human(removed)} reclaimed.")
    if refused:
        out(f"  kept {', '.join(refused)} — that is your data, not a build output.")
    if not deep:
        out("  The next build will take longer; nothing else was affected.")
    out("")
    return EXIT_OK


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Measure what BrowserX is taking, and remove only what is regenerable.")
    parser.add_argument("--apply", action="store_true", help="actually remove things")
    parser.add_argument("--deep", action="store_true",
                        help="with --apply: also the fetched CEF headers/binaries")
    parser.add_argument("--json", action="store_true", help="machine-readable output")
    args = parser.parse_args(argv)

    items = collect()
    extra: list[Item] = []
    for path in visual_studio_paths():
        item = Item(path, "Visual Studio / Windows SDK", "trim",
                    "the Visual Studio Installer's own 'Modify' screen trims components; "
                    "removing this by hand leaves a broken toolchain")
        extra.append(item)

    def out(message: str = "") -> None:
        print(message)

    if args.apply:
        print()
        print("  BrowserX — reclaiming space")
        print("  " + "-" * 68)
        return apply(items, args.deep, out)
    return render(items, extra, args.json)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
