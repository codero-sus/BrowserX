# BrowserX

An ULTRA-optimised Chromium browser with **five performance modes + Auto-Pilot**,
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
| **AUTO-PILOT** | hands-free | watches battery level, memory pressure, fullscreen/gaming and idle time; `auto` switches, `ask` suggests, `off` never touches your mode |

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

Other useful targets:

```bash
make check       # compile every source file against the real CEF headers
make test        # 167 unit tests over the CEF-free core (no CEF needed)
make bench       # print the resolved Chromium flag plan for every mode
make smoke       # headless start-up probe: does the browser actually come up?
make doctor      # environment diagnosis (missing libs, sandbox, GL, caches)
make package     # distributable layout in out/
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
find-in-page, zoom, fullscreen, developer tools, and internal pages
(`browserx://settings|telemetry|history|bookmarks|downloads|modes|find|about`).
Extensions and sync are deliberately deferred to v2.

## Layout

```
src/core/       CEF-free, fully unit-tested policy: modes, memory, auto-pilot,
                shield, settings, omnibox, formatting, logging, reports
src/app/        entry point, CLI, paths, settings store, metrics sampler,
                single-instance lock, CEF host (switch application)
src/browser/    window chrome (CEF Views), tab strip, session model, tab commands,
                headless probe/smoke test
tests/unit/     167 tests, no dependencies beyond the standard library
tools/          flagplan printer (make bench), doctor
mobile-edition/ Android app: Kotlin UI + JNI bridge to src/core
PLAN.md         the build plan, the verified evidence, and the decision log
```

## Honest status

* `make check` (25 source files against real CEF headers), `make test`
  (167/167 passing) and `make bench` are green in the development sandbox.
* The GUI itself cannot be launched in that sandbox (no CEF binary, no GTK/X11),
  so the first real `make run` happens on your machine; `--smoke-test`, `--doctor`
  and the compile-checked CEF API usage are what de-risk it.
* The Android project is source-complete and its JNI bridge is verified to
  compile and to export the exact symbols the Kotlin code declares, but it needs
  your Android toolchain for the first real build.
