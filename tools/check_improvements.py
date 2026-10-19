#!/usr/bin/env python3
# BrowserX — (c) 2026 codero-sus. All rights reserved.
# Personal, non-commercial use only. No modification, no redistribution or resale.
# Source-available, not open source — see LICENSE. https://the-codero.web.app
"""Verifies docs/IMPROVEMENTS.md.

A list of improvements is worth nothing if nobody can check it. Every row in the
ledger names the file it lives in and an *anchor*: a string that must appear in that
file. This tool reads the ledger and proves each claim against the code, so an entry
cannot outlive the thing it describes.

    python3 tools/check_improvements.py            # verify, print the count
    python3 tools/check_improvements.py --min 200  # also require at least 200 rows
    python3 tools/check_improvements.py --list     # print every row, for review

Exit code 0 only when every anchor is found in its file (and the minimum is met).
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LEDGER = ROOT / "docs" / "IMPROVEMENTS.md"

# | 12 | Area | What it improves | path/to/file | anchor-string |
ROW = re.compile(r"^\|\s*(\d+)\s*\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|\s*`([^`]+)`\s*\|\s*$")


def parse(path: Path):
    rows = []
    problems = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = line.rstrip()
        if not line.startswith("|"):
            continue
        if set(line) <= set("|-: "):  # separator row
            continue
        match = ROW.match(line)
        if match is None:
            # A header row is fine; anything else that starts like a row is a mistake.
            if "improvement" in line.lower() or "area" in line.lower():
                continue
            problems.append(f"{path.name}:{number}: not a valid row — {line[:90]}")
            continue
        rows.append(
            {
                "id": int(match.group(1)),
                "area": match.group(2),
                "what": match.group(3),
                "file": match.group(4),
                "anchor": match.group(5),
                "line": number,
            }
        )
    return rows, problems


def main() -> int:
    parser = argparse.ArgumentParser(description="verify the improvements ledger")
    parser.add_argument("--min", type=int, default=0, help="minimum number of entries")
    parser.add_argument("--json", action="store_true", help="machine-readable ledger (used by the preview)")
    parser.add_argument("--list", action="store_true", help="print every entry")
    args = parser.parse_args()

    if not LEDGER.is_file():
        print(f"  !! ledger missing: {LEDGER.relative_to(ROOT)}")
        return 1

    rows, problems = parse(LEDGER)
    seen_ids = {}
    for row in rows:
        if row["id"] in seen_ids:
            problems.append(f"duplicate id {row['id']} (lines {seen_ids[row['id']]} and {row['line']})")
        seen_ids[row["id"]] = row["line"]

    by_area = {}
    for row in rows:
        by_area[row["area"]] = by_area.get(row["area"], 0) + 1

        # The file column may be written plain or in backticks; both are the same path.
        row["file"] = row["file"].strip().strip("`").strip()
        target = ROOT / row["file"]
        if not target.is_file():
            problems.append(f"#{row['id']}: file does not exist: {row['file']}")
            continue
        try:
            text = target.read_text(encoding="utf-8", errors="replace")
        except OSError as error:
            problems.append(f"#{row['id']}: cannot read {row['file']} ({error})")
            continue
        if "`" in row["anchor"]:
            problems.append(
                f"#{row['id']}: the anchor contains a backtick, which the row format cannot carry\n"
                f"      (write the anchor without backticks, or reword the text it points at)"
            )
            continue
        if "|" in row["what"]:
            problems.append(
                f"#{row['id']}: the description contains a raw '|', which splits the table into\n"
                f"      extra columns; write it as ' / ' or 'or'"
            )
            continue
        anchor = row["anchor"].replace("\\n", "\n")
        if anchor not in text:
            problems.append(
                f"#{row['id']}: anchor not found in {row['file']} — {row['anchor'][:70]!r}\n"
                f"      the improvement this row claims is no longer in the code"
            )

    if args.json:
        import json as json_module  # only here, so the checker stays dependency-free
        print(json_module.dumps({
            "ok": not problems,
            "count": len(rows),
            "minimum": args.min,
            "areas": by_area,
            "problems": problems,
            "entries": [
                {"id": row["id"], "area": row["area"], "what": row["what"], "file": row["file"]}
                for row in rows
            ],
        }, indent=2))
        return 1 if problems or (args.min and len(rows) < args.min) else 0

    if args.list:
        for row in rows:
            print(f"{row['id']:>3}  {row['area']:<22} {row['what'][:78]}")

    print(f"\n  improvements ledger: {len(rows)} entries in {len(by_area)} areas")
    for area in sorted(by_area):
        print(f"    {area:<26} {by_area[area]:>3}")

    if problems:
        print(f"\n  !! {len(problems)} problem(s) with the ledger:\n")
        for problem in problems:
            print(f"    {problem}")
        return 1

    if args.min and len(rows) < args.min:
        print(f"\n  !! the ledger has {len(rows)} entries but {args.min} are required")
        return 1

    print("\n  ok every improvement in the ledger is present in the code it names\n")
    return 0


if __name__ == "__main__":
    # Windows console code pages cannot encode ✔ or →; without this a tool dies while
    # reporting something else. Best effort: it never makes a working run fail.
    import utf8_console  # same directory: sys.path[0] when run as a script
    utf8_console.enable()
    sys.exit(main())
