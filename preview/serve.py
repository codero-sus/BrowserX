#!/usr/bin/env python3
# BrowserX — (c) 2026 codero-sus. All rights reserved.
# Personal, non-commercial use only. No modification, no redistribution or resale.
# Source-available, not open source — see LICENSE. https://the-codero.web.app
"""BrowserX — interactive preview server.

Serves a clickable mock of the BrowserX window whose *data is real*: every switch,
threshold and reason shown in the UI comes from `out/browserx-flagplan --json`,
i.e. from the same `src/core` mode engine the C++ browser uses. Nothing about the
numbers here is hand-written.

Why this exists: the browser itself is C++/CEF and cannot run inside every
environment (no CEF binary / no GTK), but the optimisation logic is pure C++ with
no CEF dependency, so it can be demonstrated anywhere — including a browser tab.

    make preview            # http://localhost:8770
    python3 preview/serve.py --port 8080 --open

Environment:
    BROWSERX_PREVIEW_HOST   bind address (default 0.0.0.0)
    BROWSERX_PREVIEW_PORT   port (default 8770)
"""

from __future__ import annotations

import argparse
import json
import mimetypes
import os
import subprocess
import sys
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

REPO_ROOT = Path(__file__).resolve().parent.parent
PREVIEW_DIR = Path(__file__).resolve().parent
FLAGPLAN = REPO_ROOT / "out" / "browserx-flagplan"
# The offline benchmark page. It lives in the Android edition's assets because that
# edition cannot compile C++, and `make check` verifies it is byte-identical to what
# BrowserX serves at browserx://bench - so serving it here is honest.
# Every internal page, rendered by the browser's own renderer (`make pages`).
PAGES_DIR = REPO_ROOT / "out" / "pages"
BENCH_PAGE = (
    REPO_ROOT / "mobile-edition" / "app" / "src" / "main" / "assets" / "bench.html"
)
DOCTOR = REPO_ROOT / "out" / "browserx-doctor"

# The mode list is fetched from the binary, so this is only used before the first
# successful run to give the API something honest to say.
FALLBACK_MODES = [
    "ultra", "gaming", "high-performance", "low-ram", "eco",
    "super-internet", "download", "streaming",
    "privacy", "data-saver", "weak-network", "low-latency", "reader", "benchmark", "kiosk",
]

_build_lock = threading.Lock()


def log(message: str) -> None:
    print(f"  {message}", flush=True)


def tool_path(primary: Path, fallbacks: list[str]) -> Path:
    """Find a built tool, building it if necessary (once, under a lock)."""
    if primary.is_file():
        return primary
    with _build_lock:
        if primary.is_file():
            return primary
        log(f"building {primary.name} (first run)")
        for command in fallbacks:
            subprocess.run(command, shell=True, cwd=REPO_ROOT, check=False,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if primary.is_file():
                return primary
    raise FileNotFoundError(primary)


def run_json(tool: Path, args: list[str]) -> object:
    completed = subprocess.run([str(tool), *args], cwd=REPO_ROOT, capture_output=True, text=True)
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or f"{tool.name} exited {completed.returncode}")
    return json.loads(completed.stdout)


class PreviewHandler(BaseHTTPRequestHandler):
    server_version = "BrowserXPreview/1.0"
    protocol_version = "HTTP/1.1"

    # -- helpers ----------------------------------------------------------
    def send_bytes(self, status: int, body: bytes, content_type: str) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def send_json(self, payload: object, status: int = 200) -> None:
        self.send_bytes(status, json.dumps(payload).encode("utf-8"), "application/json; charset=utf-8")

    def send_error_json(self, status: int, message: str) -> None:
        self.send_json({"error": message}, status=status)

    # -- routing ----------------------------------------------------------
    def do_GET(self) -> None:  # noqa: N802 (http.server API)
        parsed = urlparse(self.path)
        route = parsed.path
        try:
            if route in ("/", "/index.html"):
                self.serve_file(PREVIEW_DIR / "index.html")
            elif route == "/pages":
                # Same listing as /api/pages: readable by a human, parseable by the UI.
                self.serve_pages_index()
            elif route.startswith("/pages/"):
                name = route[len("/pages/"):]
                if "/" in name or not name.endswith(".html"):
                    self.send_error_json(404, "no such page")
                else:
                    self.serve_file(PAGES_DIR / name)
            elif route in ("/bench", "/bench.html"):
                # The same file the browser serves at browserx://bench, generated from
                # src/web/bench_page.cc by `make bench-page`. Serving it here means the
                # comparison can be started from the preview: run it in this browser, then
                # open the same file in BrowserX on the machine that has it.
                self.serve_file(BENCH_PAGE)
            elif route.startswith("/api/"):
                self.serve_api(route, parse_qs(parsed.query))
            else:
                target = (PREVIEW_DIR / route.lstrip("/")).resolve()
                if PREVIEW_DIR not in target.parents and target != PREVIEW_DIR:
                    self.send_error_json(403, "outside the preview directory")
                elif not target.is_file():
                    self.send_error_json(404, f"no such file: {route}")
                else:
                    self.serve_file(target)
        except FileNotFoundError as error:
            self.send_error_json(503, f"{Path(error.args[0]).name} is not built yet; run: make bench")
        except RuntimeError as error:
            self.send_error_json(500, str(error))
        except Exception as error:  # keep the preview alive; the UI shows the message
            self.send_error_json(500, f"{type(error).__name__}: {error}")

    def do_HEAD(self) -> None:  # noqa: N802
        self.do_GET()

    def serve_file(self, path: Path) -> None:
        body = path.read_bytes()
        guess, _ = mimetypes.guess_type(str(path))
        content_type = guess or "application/octet-stream"
        if content_type.startswith("text/") or content_type in ("application/javascript", "application/json"):
            content_type += "; charset=utf-8"
        self.send_bytes(200, body, content_type)

    def serve_pages_index(self) -> None:
        """Lists the rendered internal pages, so the preview can show a picker.

        The files come from `make pages`, which renders them with the same function
        the browser uses. Nothing here is hand-written HTML: if this list is empty,
        the honest answer is "run make pages".
        """
        if not PAGES_DIR.is_dir():
            self.send_error_json(503, "no rendered pages yet; run: make pages")
            return
        pages = []
        for path in sorted(PAGES_DIR.glob("*.html")):
            pages.append({"name": path.stem, "url": "/pages/" + path.name,
                          "bytes": path.stat().st_size})
        self.send_json({"ok": True, "count": len(pages), "pages": pages,
                        "note": "rendered by src/web/internal_pages.cc; the tab list "
                                "is sample data (there is no live browser process here)"})

    def serve_api(self, route: str, query: dict[str, list[str]]) -> None:
        if route == "/api/pages":
            self.serve_pages_index()
        elif route == "/api/plans":
            self.send_json(run_json(tool_path(FLAGPLAN, ["make bench"]), ["--json"]))
        elif route == "/api/catalog":
            self.send_json(run_json(tool_path(FLAGPLAN, ["make bench"]), ["--catalog", "--json"]))
        elif route == "/api/profile":
            text = run_json  # not used; kept for symmetry
            del text
            completed = subprocess.run(
                [str(tool_path(DOCTOR, ["make doctor"])), "--quiet"],
                cwd=REPO_ROOT, capture_output=True, text=True,
            )
            self.send_json({"text": completed.stdout, "ok": completed.returncode == 0})
        elif route == "/api/health":
            self.send_json({"ok": True, "repo": str(REPO_ROOT)})
        else:
            self.send_error_json(404, f"unknown endpoint: {route}")

    def log_message(self, fmt: str, *args) -> None:
        # Only report API calls; static assets would flood the console.
        if self.path.startswith("/api/") or " 4" in str(args[1] if len(args) > 1 else ""):
            log(f"{self.address_string()} {fmt % args}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="preview/serve.py",
        description="Serve the interactive BrowserX preview (real mode-engine data).",
    )
    parser.add_argument("--port", type=int, default=int(os.environ.get("BROWSERX_PREVIEW_PORT", "8770")))
    parser.add_argument("--host", default=os.environ.get("BROWSERX_PREVIEW_HOST", "0.0.0.0"))
    parser.add_argument("--open", action="store_true", help="open the preview in a browser")
    args = parser.parse_args(argv)

    print()
    print("  BrowserX — interactive preview")
    print("  " + "-" * 60)
    log(f"repo    : {REPO_ROOT}")
    log("data    : live from out/browserx-flagplan (the real mode engine)")
    log(f"url     : http://{'localhost' if args.host in ('0.0.0.0', '') else args.host}:{args.port}")
    log("stop    : Ctrl-C")
    print()

    if args.open:
        threading.Timer(0.6, lambda: webbrowser.open(f"http://localhost:{args.port}")).start()

    server = ThreadingHTTPServer((args.host, args.port), PreviewHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n  preview stopped\n")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
