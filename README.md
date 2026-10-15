# BrowserX

**By [codero-sus](https://github.com/codero-sus) · website [the-codero.web.app](https://the-codero.web.app) · source [github.com/codero-sus/BrowserX](https://github.com/codero-sus/BrowserX)**

**Licence: [BrowserX Personal-Use License](LICENSE) — personal, non-commercial use
only · no modification · no redistribution or resale.** Source-available, not open
source. The Chromium/CEF runtime it bundles keeps its own licences.

An ULTRA-optimised Chromium browser with **fifteen performance modes + Auto-Pilot**,
in two editions that share one performance core:

| Edition | Engine | Front end | Directory |
|---|---|---|---|
| **Desktop** | CEF (Chromium Embedded Framework), C++20 | native CEF Views chrome | `src/` |
| **Mobile** | Android system WebView (also Chromium) | Kotlin, framework widgets only | `mobile-edition/` |

Both compile the *same* `src/core/`: mode engine, memory policy, Auto-Pilot rules,
tracker shield, omnibox parser. One place to fix a rule, one place to test it.

---

## The modes (the reason this browser exists)

Fifteen modes in two groups. **Auto-Pilot manages the five general modes**, because
those are about how much speed and memory to spend right now — which is exactly what
battery level, memory pressure and idle time describe. The ten **specialist** modes
say something Auto-Pilot cannot infer, so it steps back and leaves them alone (with
one exception: a real memory emergency still escalates to LOW RAM).

### General modes — Auto-Pilot's job

| Mode | Optimised for | What it actually does |
|---|---|---|
| **ULTRA** (default) | everyday balance | background throttling on, idle tabs sleep after 30 min, bounded bfcache, adaptive renderer cap |
| **GAME** | frame rate + input latency | `--disable-gpu-vsync`, `--disable-frame-rate-limit`, throttling and renderer backgrounding off, autoplay allowed, screen kept awake, 500 ms telemetry |
| **HIGH PERFORMANCE** | throughput while plugged in | no layer-tree memory pressure, accelerated video decode/encode, speculative prefetch, bfcache, ≥1 GB HTTP cache, no tab discarding |
| **LOW RAM** | smallest footprint | `--enable-low-end-device-mode`, ≤3 renderers, `--process-per-site`, 48 MB cache, Skia/MSE limits, tabs sleep after 1 min |
| **ECO** | battery life | `--disable-v8-idle-tasks`, animations at 0.5×, 30 FPS target, reduced motion, 3 s telemetry, aggressive sleeping |

### Specialist modes — Auto-Pilot steps back

| Mode | Optimised for | What it actually does |
|---|---|---|
| **SUPER INTERNET** | network speed + lowest ping | every reachable network lever on: Happy Eyeballs v3, optimistic DNS for TCP and QUIC, TLS 1.3 0-RTT, HTTP/2 keep-alive, address-sort connect cache, connection prefetch, in-process network service; IPC flooding protection off, background throttling off, nothing sleeps, tracker blocking on (trackers are bandwidth), 500 ms telemetry |
| **DOWNLOAD** | big transfers | **parallel range downloads** (`ParallelDownloading`), plus the connection levers above, the largest cache budget of any mode (≥2 GB), no tab sleep/discard, screen kept awake, bfcache given up to hand the RAM to transfer buffers |
| **STREAMING** | smooth video + audio | hardware video decode/encode requested (with a warning if the machine has no decoder), autoplay without a click, fullscreen video overlays, uncapped frame rate, no sleep/discard, screen kept awake |
| **PRIVACY** (Ctrl+9) | leaving nothing behind | a few-MB disk cache, no GPU shader cache, no compiled-script cache, remote web fonts refused, third-party images/media/fonts cut before they are requested, no speculative connections, tabs slept sooner. The tracker shield is on in every mode; this one removes the third-party heavy resources it has no rule for. Honest limit: it hardens the profile you are in — it is **not** an incognito window |
| **DATA SAVER** | spending the least data | images, media and web fonts cancelled *before the request is sent* (the request that never happens is the only one that really saves), remote fonts refused, big cache so a revisit is nearly free, speculative preconnecting off because prefetch spends data on pages you may never open |
| **WEAK NETWORK** | slow, crowded or metered links | enables Chromium's network quality estimator (off by default) and tells it the link is slow, so the engine stops queueing work it will only have to time out; optimistic DNS and the address cache to skip round trips; images and third-party heavy resources dropped; large cache because on a slow link the cache is the fastest server you have |
| **LOW LATENCY** | the first byte, not the last | in-process network service (one IPC hop fewer per request), TLS 1.3 0-RTT, HTTP/2 keep-alive, address sorting, Happy Eyeballs v3, IPC flooding protection off, audio priority raised for calls. **Prefetch is deliberately off** here: it competes for the connection with the request you are waiting for |
| **READER** | long-form reading | back/forward cache plus a large HTTP cache so an article returns instantly, tabs slept late rather than early (an article you are part-way through is not idle), reduced motion, scrolling and animation work cut, and media never requested — which on a news site is most of the page weight |
| **BENCHMARK** | measuring, not browsing | vsync off, compositor frame cap removed, Chromium's own FPS counter on, GPU benchmarking hints, 250 ms telemetry, memory thresholds raised so nothing trims mid-run. Honest limit: it is a diagnostic mode — the numbers do not describe ordinary browsing |
| **KIOSK** | an unattended screen | display kept awake, window kept on top, autoplay without a click, fullscreen video overlays, page scrollbars hidden, background timer throttling off so a dashboard keeps updating while unfocused, no tab ever slept or discarded |
| **AUTO-PILOT** | hands-free | watches battery level, memory pressure, fullscreen/gaming and idle time; `auto` switches, `ask` suggests, `off` never touches your mode. It never picks a specialist mode and never leaves one — except on a real memory emergency |

### The shield is not a switch that changes an icon

Every request the browser makes passes a single guard (`core/request_guard.cc`,
enforced in CEF by `src/browser/request_guard.cc`). It applies the shield's rule set
first and the mode's resource policy second, and **cancels** the request rather than
downloading it and dropping it in the renderer — which is the difference between
saving data and pretending to. Two absolutes are unit-tested: a top-level navigation
is never blocked by a mode's policy, and third-party *scripts and stylesheets* are
never cut by a mode (sites serve them from CDNs; blocking them breaks pages rather
than saving anything). Allowlisting a site in `browserx://settings` now also covers
the requests that site makes, which is what a user means by "allow this site".

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

### Speed, measured rather than claimed

Browsers market speed with numbers nobody can check. BrowserX does it the other way
round: **every optimisation it ships is a verified Chromium lever**, and the browser
also ships the tool that measures itself.

* **What the modes actually add for rendering.** The frame modes (GAME, HIGH
  PERFORMANCE, BENCHMARK) size the GPU shader disk cache at 32 MB and enable native
  GPU memory buffers for video; the light modes (ULTRA, STREAMING, LOW LATENCY,
  KIOSK) take the 16 MB version. LOW RAM keeps its own 1 MB cap, and the eight
  memory-first modes touch nothing in the GPU pipeline at all.
* **What they deliberately do *not* add.** GPU rasterisation, zero-copy upload and
  out-of-process raster are already the default in the Chromium this browser embeds —
  the switches that once enabled them (`--enable-gpu-rasterization`, `--enable-zero-copy`,
  `--enable-oop-rasterization`) were removed or made no-ops upstream. Applying them
  would be quoting a 2019 release note. BrowserX lists none of them, and a test fails
  the build if one ever appears in a plan.
* **A machine that cannot render in hardware is told so**, once, in the plan's
  warnings — instead of silently running ten times slower.
* **The overhead is measured too.** `make perf` times the browser's *own* work
  (plan building, URL parsing, the tracker shield's per-request decision) against hard
  budgets and exits non-zero if any of them regress. This is why the shield is
  index-based: the per-request cost went from 27 µs to 1.5 µs, because 5 µs of policy
  work multiplied by the ~300 requests on an ordinary page is CPU taken from the page.
* **The comparison itself ships in the browser.** `browserx://bench` (or
  `--write-bench-page`, or `make bench-page`) is one self-contained HTML file with
  script, layout, canvas-2D and WebGL tests. It makes no network requests, so the
  identical bytes can be opened in BrowserX and in any other browser on the same
  machine, one at a time, and the scores compared. A test that cannot run (no WebGL,
  no 2D context) is reported as *skipped* and the score is withheld — a partly-measured
  browser is not handed a number that looks like a full one.

### One design system, three surfaces

The browser chrome (CEF Views), the internal pages and the clickable preview all read
from one set of design tokens — background, panel, line, accent green, muted text — so
they cannot drift into looking like three different products. On the pages those tokens
are CSS custom properties in `src/web/internal_pages.cc`, the same file that renders
every `browserx://` page; the preview uses the same names in `preview/styles.css`; the
native chrome and the Android edition use the same values as C++/Kotlin constants.

What the modern shell adds, without a single framework or network request:

* **A start page that is a start page** — a real search box that uses the engine from
  your settings, tiles for every part of the browser, and a mode switcher rendered
  straight from the engine, so the page can never list fewer modes than the browser has.
* **A command palette on Ctrl+K** — on the browser's own pages it is a fuzzy-filtered
  list of every page and every mode; from any web page the same list is available as the
  **Jump to…** menu, so the shortcut works everywhere instead of only where JavaScript
  is allowed to run.
* **Live panel-style pages** — telemetry and the session page render the actual tab list
  from the persisted session JSON (sleeping tabs labelled, counters for pinned/muted/
  asleep), with the raw JSON left behind a `<details>` for anyone who wants it.
* **Filterable shortcut reference**, status lights on every tab (live / sleeping / muted),
  copyable plan output, and light-mode support via `prefers-color-scheme`.

Everything is still generated by hand in C++ with no dependencies: the pages are
self-contained documents that make no network requests, and every one of them is
rendered by `make pages` so you can open the real output in any browser before running
the real browser. `make check` compiles it all; the page DOM behaviour is asserted in
the unit tests; and the preview's own wiring is exercised against the live server.

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

Requirements: Linux x86_64, a C++20 compiler (g++ 12+ or clang 15+), GNU make, Python 3,
and — for the full browser only — the X11 and GTK 3 development files plus about 200 MB
of CEF download. Nothing else: no CMake, no ninja, no npm. The unit tests, `make check`,
`make bench`, `make perf` and `make pages` need **no CEF and no network at all**.

Rather than comparing versions by eye, ask the machine:

```bash
make requirements            # what this machine has, what is missing, per build target
make requirements FOR=core   # only the core; exit code is 1 when it cannot be built here
```

The full requirements table (versions this project is developed against, disk and network
figures, per-distro package names, offline installs, container notes):
[`BUILDING.md`](BUILDING.md).

```bash
make deps        # download + SHA-1 verify + extract the pinned CEF build
make             # build out/BrowserX
make run         # launch it
```

**On Windows** (Visual Studio 2022 Build Tools with the C++ workload, the Windows
SDK, Python 3 and Git — CEF is still a 200 MB download, still no CMake or solution
file):

```bat
python tools\fetch_cef.py        :: the Chromium/CEF binaries, SHA-1 verified
tools\build_windows.cmd --stage  :: build out\BrowserX.exe
out\BrowserX.exe
```

The Windows build is the same sources and the same rules, compiled by MSVC instead of
g++; `make check-windows` proves both, on any machine. The [Windows section of
`BUILDING.md`](BUILDING.md) has the tool table with sizes, and says plainly what has
and has not been verified so far.

Want to see it before building? `make preview` serves a clickable mock of the
window whose numbers come straight from the mode engine — the mode switcher, the
real switch list with provenance, and the memory-pressure escalation ladder.

```bash
make preview     # http://localhost:8770
```

Other useful targets:

```bash
make check       # compile every source file against the real CEF headers
make test        # unit tests over the CEF-free core (no CEF needed)
make bench       # print the resolved Chromium flag plan for every mode
make perf        # measure BrowserX's own overhead against budgets (fails on regression)
make pages       # render every browserx:// page to out/pages/ (the preview serves them)
make bench-page  # write the offline benchmark page that browserx://bench serves
make smoke       # headless start-up probe: does the browser actually come up?
make doctor      # environment diagnosis (missing libs, sandbox, GL, caches)
make requirements # build requirements for this machine, per target (FOR=core|browser|…)
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
find-in-page, zoom, fullscreen, developer tools, a **Ctrl+K** jump list (every page and
every mode from anywhere), and internal pages generated by the browser itself
(`browserx://newtab|modes|telemetry|settings|shortcuts|catalog|session|about|find|history|bookmarks|error|bench`; `make pages` writes them all, and `make ui-check` fails if one of them stops rendering). `browserx://bench` is the offline benchmark page, also
reachable with Ctrl+Shift+B and writable as a file with `--write-bench-page`. History and
bookmarks are real: `browserx://history` and `browserx://bookmarks` render the store the
browser writes (de-duplicated by canonical URL, tracking parameters stripped before an
address is stored, capped, with Netscape-format import and export), and a reopened tab
comes back with its title, group, zoom and pinning. `browserx://downloads` still says
plainly that its item store is not wired up yet, because the CEF download callbacks are
not subscribed to in this build — an honest empty page, not a fake one. Extensions and
sync are deliberately deferred to v2.

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
src/web/        the design system, every internal page, and the offline benchmark page —
                all rendered from live state by C++ (no framework, no network)
tests/unit/     the unit tests, no dependencies beyond the standard library
tools/          fetch_cef.py (verified CEF acquisition), flagplan printer
                (make bench), perf harness (make perf), bench-page writer, doctor,
                package.sh
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

## The improvement ledger

Every change in this build is recorded in `docs/IMPROVEMENTS.md`, and every entry is
checked against the code: a row names a file and a string that must exist in it.

```bash
make improvements     # read the ledger
python3 tools/check_improvements.py            # verify every entry against the code
python3 tools/check_improvements.py --min 200  # what make check enforces
```

`make check` fails while the ledger is below 200 entries or while any entry's code has
gone, so the count cannot be claimed, only grown. `make ui-check` drives the rendered
pages and the preview in a headless DOM (needs `node`; it says how to get `jsdom` if it is
missing, and the browser itself has no JavaScript dependencies).

## Honest status

* `make check` (every source file against real CEF headers, plus the Android
  benchmark asset versus what the browser serves), `make test` (228 passing),
  `make bench`, `make perf` (inside every budget) and `make doctor` are green in
  the development sandbox. `make test` prints the count, so this line says what the
  last run measured rather than what someone remembered.
* The GUI itself cannot be launched in that sandbox (no CEF binary, no GTK/X11),
  so the first real `make run` happens on your machine; `--smoke-test`, `--doctor`
  and the compile-checked CEF API usage are what de-risk it.
* The Android project is source-complete and its JNI bridge is verified to
  compile and to export the exact symbols the Kotlin code declares, but it needs
  your Android toolchain for the first real build.
