# BrowserX

**By [codero-sus](https://github.com/codero-sus) · website [the-codero.web.app](https://the-codero.web.app) · source [github.com/codero-sus/BrowserX](https://github.com/codero-sus/BrowserX)**

**Licence: [BrowserX Personal-Use License](LICENSE) — personal, non-commercial use
only · no modification · no redistribution or resale.** Source-available, not open
source. The Chromium/CEF runtime it bundles keeps its own licences.

An ULTRA-optimised Chromium browser with **eight performance modes + Auto-Pilot**,
in two editions that share one performance core:

| Edition | Engine | Front end | Directory |
|---|---|---|---|
| **Desktop** | CEF (Chromium Embedded Framework), C++20 | native CEF Views chrome | `src/` |
| **Mobile** | Android system WebView (also Chromium) | Kotlin, framework widgets only | `mobile-edition/` |

Both compile the *same* `src/core/`: mode engine, memory policy, Auto-Pilot rules,
tracker shield, omnibox parser. One place to fix a rule, one place to test it.

---

## The modes (the reason this browser exists)

| Mode | Optimised for | What it actually does |
|---|---|---|
| **ULTRA** (default) | everyday balance | background throttling on, idle tabs sleep after 30 min, bounded bfcache, adaptive renderer cap |
| **GAME** | frame rate + input latency | `--disable-gpu-vsync`, `--disable-frame-rate-limit`, throttling and renderer backgrounding off, autoplay allowed, screen kept awake, 500 ms telemetry |
| **HIGH PERFORMANCE** | throughput while plugged in | no layer-tree memory pressure, accelerated video decode/encode, speculative prefetch, bfcache, ≥1 GB HTTP cache, no tab discarding |
| **LOW RAM** | smallest footprint | `--enable-low-end-device-mode`, ≤3 renderers, `--process-per-site`, 48 MB cache, Skia/MSE limits, tabs sleep after 1 min |
| **ECO** | battery life | `--disable-v8-idle-tasks`, animations at 0.5×, 30 FPS target, reduced motion, 3 s telemetry, aggressive sleeping |
| **SUPER INTERNET** | network speed + lowest ping | every reachable network lever on: Happy Eyeballs v3, optimistic DNS for TCP and QUIC, TLS 1.3 0-RTT, HTTP/2 keep-alive, address-sort connect cache, connection prefetch, in-process network service; IPC flooding protection off, background throttling off, nothing sleeps, tracker blocking on (trackers are bandwidth), 500 ms telemetry |
| **DOWNLOAD** | big transfers | **parallel range downloads** (`ParallelDownloading`), plus the connection levers above, the largest cache budget of any mode (≥2 GB), no tab sleep/discard, screen kept awake, bfcache given up to hand the RAM to transfer buffers |
| **STREAMING** | smooth video + audio | hardware video decode/encode requested (with a warning if the machine has no decoder), autoplay without a click, fullscreen video overlays, uncapped frame rate, no sleep/discard, screen kept awake |
| **AUTO-PILOT** | hands-free | watches battery level, memory pressure, fullscreen/gaming and idle time; `auto` switches, `ask` suggests, `off` never touches your mode. It never picks a specialist mode and never leaves one — except on a real memory emergency |

### What SUPER INTERNET can and cannot do

A browser cannot create bandwidth, and the page says so instead of pretending
otherwise. What it *can* remove is everything between the socket and the screen:
DNS waits (optimistic DNS starts the connection mid-resolve), IPv6 stalls (Happy
Eyeballs v3 races both families), cold handshakes (0-RTT resumption, keep-alive,
connection prefetch), queued IPCs (`--disable-ipc-flooding-protection`) and
timers that throttle a background transfer. That is the fastest a browser is
allowed to be — the limit above it is your link, your ISP and the server.

Two honest footnotes, printed with the plan:

* **QUIC/HTTP-3 is already on by default** in Chromium, so no mode can "force" it;
  `--enable-quic` is catalogued as *removed* and never applied. What the mode does
  is make QUIC connections *cheaper to establish*.
* **Parallel range downloads need the server to honour byte ranges.** When it
  does not, the download still completes — just on one connection, as before.

Every switch is validated against the Chromium source tree and carries a
provenance line — `make bench` prints the full plan, including which switches were
**rejected** as myths and which are Chrome-only no-ops under CEF. No cargo-culted
flags: `--enable-quic`, `--media-cache-size` and friends are marked removed and
never applied.

### Memory is the design constraint

LOW RAM mode is not a label. Hidden tabs are *destroyed* (renderer process gone)
and restored from URL/title/scroll on click; the memory policy escalates
trim caches → sleep idle tabs → sleep the LRU tab → warn, with hysteresis so it
never thrashes; the session is written to disk every 30 s so a crash costs at most
one tab. On devices the OS reports as low-memory, **every** mode is additionally
capped (≤3 renderers, ≤2 raster threads, ≤128 MB cache) — see
`tests/unit/mobile_profile_test.cc`.

---

## Build & run (desktop)

Requirements: Linux x86_64, g++ with C++20, GNU make, Python 3 (only for fetching
CEF), plus the usual GTK/X11 runtime libraries. Nothing else — no CMake, no ninja,
no npm.

```bash
make deps        # download + SHA-1 verify + extract the pinned CEF build
make             # build out/BrowserX
make run         # launch it
```

Want to see it before building? `make preview` serves a clickable mock of the
window whose numbers come straight from the mode engine — the mode switcher, the
real switch list with provenance, and the memory-pressure escalation ladder.

```bash
make preview     # http://localhost:8770
```

Other useful targets:

```bash
make check       # compile every source file against the real CEF headers
make test        # 167 unit tests over the CEF-free core (no CEF needed)
make bench       # print the resolved Chromium flag plan for every mode
make smoke       # headless start-up probe: does the browser actually come up?
make doctor      # environment diagnosis (missing libs, sandbox, GL, caches)
make package     # distributable tarball in out/dist/
make preview     # clickable UI preview driven by the real mode engine
make help        # all targets, with one-line explanations
```

Sandboxed/containerised machines: `make run-nosandbox` (Chromium's own sandbox
needs privileges the container does not have). `--flag-plan=all` and
`--print-profile` give the same diagnostics without a display.

## Build & run (mobile)

Android SDK 34+, NDK r26+, JDK 17, Gradle 8.6+. Full details and the mode→Android
API mapping: [`mobile-edition/README.md`](mobile-edition/README.md).

```bash
mobile-edition/tools/build-native.sh      # shared core → libbrowserxcore.so + plan
cd mobile-edition && gradle :app:assembleDebug
```

---

## Feature set (v1)

Tabs (create/close/reorder/pin/mute/group/sleep/discard, Ctrl+Tab cycling,
reopen-closed stack), omnibox with URL-vs-search parsing and a scheme allowlist
(`javascript:` and `data:` can never be navigated to from the address bar),
tracker shield with per-site allowlist and a blocked-count indicator, session
restore, live telemetry status bar, native context menus and accelerators,
find-in-page, zoom, fullscreen, developer tools, and internal pages generated by
the browser itself (`browserx://newtab|modes|telemetry|settings|shortcuts|catalog|
session|about|find`). `browserx://history|bookmarks|downloads` resolve too, but
their pages say plainly that the storage layer is not wired up yet instead of
showing a fake empty list. Extensions and sync are deliberately deferred to v2.

Internal pages are rendered from live browser state by `src/web/internal_pages.cc`
— a CEF-free, unit-tested generator — so what the page shows (the resolved plan,
the switch provenance, the session JSON that will be restored) is exactly what the
browser is doing.

## Layout

```
src/core/       CEF-free, fully unit-tested policy: modes, memory, auto-pilot,
                shield, settings, omnibox, formatting, logging, reports
src/app/        entry point, CLI, paths, settings store, metrics sampler,
                single-instance lock, CEF host (switch application)
src/browser/    window chrome (CEF Views), tab strip, session model, tab commands,
                headless probe/smoke test
tests/unit/     167 tests, no dependencies beyond the standard library
tools/          fetch_cef.py (verified CEF acquisition), flagplan printer
                (make bench), doctor, package.sh
preview/        clickable preview served by preview/serve.py
mobile-edition/ Android app: Kotlin UI + JNI bridge to src/core
PLAN.md         the build plan, the verified evidence, and the decision log
```

## Credits

**BrowserX — built by [codero-sus](https://github.com/codero-sus).**

| | |
|---|---|
| GitHub | https://github.com/codero-sus |
| Website | https://the-codero.web.app |
| Repository | https://github.com/codero-sus/BrowserX |

**Licence.** BrowserX is source-available under the
[BrowserX Personal-Use License](LICENSE): you may read the source and run the
browser for your own **personal, non-commercial** use. **No modification, no
redistribution, no resale** — asking first is always an option. The Chromium/CEF
runtime it bundles keeps its own licences (BSD-3-Clause and friends); BrowserX
claims no copyright over it.

It stands on other people's work: **[Chromium](https://www.chromium.org/)** (the
engine) and the **[Chromium Embedded Framework](https://github.com/chromiumembedded/cef)**
(embedding it in C++), both BSD-3-Clause; on Android the mobile edition drives the
system `WebView`, which is Chromium too. Every switch in `src/core/chromium_catalog.cc`
records the Chromium source file it was verified against — see
[source.chromium.org](https://source.chromium.org/). Full list:
[CREDITS.md](CREDITS.md).

The same credits are inside the product, not only in this file: `browserx://about`,
the CLI (`--help` / `--version`) and the footer of every internal page.

## Honest status

* `make check` (25 source files against real CEF headers), `make test`
  (167/167 passing) and `make bench` are green in the development sandbox.
* The GUI itself cannot be launched in that sandbox (no CEF binary, no GTK/X11),
  so the first real `make run` happens on your machine; `--smoke-test`, `--doctor`
  and the compile-checked CEF API usage are what de-risk it.
* The Android project is source-complete and its JNI bridge is verified to
  compile and to export the exact symbols the Kotlin code declares, but it needs
  your Android toolchain for the first real build.
