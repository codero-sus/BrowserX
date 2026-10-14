# BrowserX — Mobile Edition (Android)

> **BrowserX by [codero-sus](https://github.com/codero-sus)** ·
> [the-codero.web.app](https://the-codero.web.app) ·
> [source](https://github.com/codero-sus/MyBrowser) ·
> [BrowserX Personal-Use License](../LICENSE) (personal, non-commercial, no
> modification, no redistribution — the bundled Chromium/CEF code keeps its own
> licences).
> Built on Chromium (the system WebView) and sharing its core with the desktop
> edition, which is built on [CEF](https://github.com/chromiumembedded/cef).
> Credits: [../CREDITS.md](../CREDITS.md).

A Chromium-powered browser for Android that shares the **exact same performance
core** as the desktop edition. Not a re-implementation: the mode engine, the
memory policy, the auto-pilot rules, the tracker shield and the omnibox parser
are the same C++ sources in `../src/core/`, compiled for arm64/x86_64 by the NDK
and called from Kotlin through JNI.

```
                   ┌──────────────────────────────────────────┐
   desktop  ──────▶│  src/core/  (CEF-free, unit tested)      │◀────── mobile
   src/app + CEF   │  mode_engine · memory_policy · auto_     │   JNI bridge
   Views chrome    │  pilot · blocklist · url_utils · json    │   Android WebView
                   └──────────────────────────────────────────┘
```

One core, two front ends. A mode decision can never drift between the two
editions, because there is only one implementation of it.

## Why WebView instead of bundling CEF on Android

The desktop edition uses CEF because it gives the browser *all* of Chromium's
command-line surface (`--disable-gpu-vsync`, `--renderer-process-limit`, …) and
native Views chrome. On Android that surface does not exist: CEF is not shipped
for Android, and a custom Chromium fork is a multi-hundred-gigabyte build that
no roadmap survives. Android's `WebView` **is** Chromium (the system WebView /
Chrome renderer), so the mobile edition drives the same engine through the APIs
Android actually exposes:

| What the mode needs        | Desktop (CEF switches)                 | Mobile (Android APIs)                                  |
|----------------------------|----------------------------------------|--------------------------------------------------------|
| Renderer count / priority  | `--renderer-process-limit=N`           | `WebView.setRendererPriorityPolicy(BOUND/WAIVED, …)`   |
| Background thrashing       | `disable-background-timer-throttling`  | `WebView.pauseTimers()` + per-tab WebView teardown     |
| Frames / vsync             | `--disable-gpu-vsync`                  | `setOffscreenPreRaster(true)`, no vsync override needed|
| Autoplay in a game         | `autoplay-policy=no-user-gesture…`     | `WebSettings.setMediaPlaybackRequiresUserGesture`      |
| Cache budget               | `--disk-cache-size=…`                  | `WebSettings.setCacheMode`, `clearCache` on trim       |
| Sleep/discard hidden tabs  | destroy the `CefBrowserView`           | destroy the `WebView` (keep URL/title/scroll)          |
| Memory pressure            | `/proc` sampler + `MemoryPolicy`       | `onTrimMemory()` → same `MemoryPolicy` decisions       |
| Battery / Auto-Pilot       | sysfs + `/proc`                        | `BatteryManager`, `onTrimMemory`, immersive detection  |

Everything in the left column is decided by the shared core; only the *actuator*
differs. The Kotlin layer translates a decision into Android calls.

## Directory map

```
mobile-edition/
├── app/src/main/
│   ├── java/com/browserx/mobile/
│   │   ├── PerformanceCore.kt   JNI facade (with a pure-Kotlin fallback)
│   │   ├── Modes.kt             mode enum + UI labels (ids match the core)
│   │   ├── ModeController.kt    mode → WebView settings, Auto-Pilot ticker
│   │   ├── TabController.kt     tabs, sleeping, session restore
│   │   ├── ShieldEngine.kt      tracker shield via the shared blocklist
│   │   └── MainActivity.kt      the UI: toolbar, chips, mode menu, credits dialog
│   └── jni/
│       ├── CMakeLists.txt       compiles ../src/core/*.cc into the .so
│       └── core_bridge.cc       the JNI surface
└── tools/build-native.sh        NDK build without Gradle (prints the plan)
```

## Building

Prerequisites: Android SDK 34+, NDK r26+, JDK 17, and Gradle 8.6+ (or just open
the folder in Android Studio, which fetches all of it).

```bash
# 1. native core only — produces libbrowserxcore.so and prints the flag plan
mobile-edition/tools/build-native.sh            # add --abi=x86_64 on an emulator

# 2. full app
cd mobile-edition
gradle :app:assembleDebug                        # or: ./gradlew (Android Studio)
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

### Status in this repository

The desktop edition is built and tested by the Makefile in the repository root:
`make check`, `make test` (158 unit tests, all passing) and `make bench`. The
mobile edition is **source-complete and reviewable but not compiled here**: this
development sandbox has no Android SDK, no NDK and no network access to fetch
them, and Android's Gradle plugin cannot be downloaded either. Everything the
Kotlin/Java layer depends on that *can* be verified here already is:

* the shared core it links against is compiled and unit tested on every
  `make test` run;
* the JNI entry points below match the Kotlin external declarations
  one-for-one and are listed in `app/src/main/jni/README` semantics at the top
  of `core_bridge.cc`;
* `tools/build-native.sh` documents the exact NDK invocation Gradle performs.

So: `gradle :app:assembleDebug` on a machine with the Android toolchain is the
next step, and nothing in this directory pretends to be verified beyond that.

## Modes on mobile

The same eight modes and the same Auto-Pilot policy as the desktop edition; each one
maps onto Android behaviour:

| Mode              | Android behaviour                                                                 |
|-------------------|-----------------------------------------------------------------------------------|
| **ULTRA** (default) | 3 background WebViews kept alive, caches on, normal priority                      |
| **GAME**          | renderer priority `BOUND` (never waived), autoplay allowed, offscreen pre-raster on, screen kept awake, immersive fullscreen auto-detected |
| **HIGH PERFORMANCE** | all background tabs kept alive, largest cache, prefetch on, no throttling      |
| **LOW RAM**       | 1 background WebView, `WAIVED` + `true` (renderer dies when hidden), images blocked on metered networks, aggressive `onTrimMemory` trim |
| **ECO**           | hidden tabs `pauseTimers()`, reduced animations, background network blocked, sleep after 10 min |
| **SUPER INTERNET** | renderer priority `IMPORTANT` (a transfer must not be reclaimed), `LOAD_CACHE_ELSE_NETWORK` so repeat visits never hit the radio, no background timer throttling — Android's own connection stack (happy-eyeballs, connection reuse) already does what the desktop mode's Chromium features do |
| **DOWNLOAD**      | renderer priority `IMPORTANT`, largest cache, `LOAD_CACHE_ELSE_NETWORK`, screen kept awake, no `onTrimMemory` trim of the active tab |
| **STREAMING**     | autoplay without a gesture, renderer priority `IMPORTANT`, screen kept awake, no sleep/discard |
| **AUTO-PILOT**    | switches on battery level, `onTrimMemory` severity, immersive/game detection, and charging state; `ask` shows a chip suggestion instead of switching |

## What is not here (and why)

* **Extensions** — Android WebView has no extension API; the desktop edition's
  extension support will not be mirrored.
* **Sync** — deferred in both editions (v2), and it is a server problem, not a
  UI problem.
* **A custom Chromium build** — see above; `WebView` wins on size, battery and
  update cadence.
* **In-app downloads** — WebView needs a `DownloadListener` plus a
  `DownloadManager`/SAF target before a mobile DOWNLOAD mode has anything to
  speed up. Until that lands, DOWNLOAD mode on mobile only tunes the WebView it
  already controls (renderer priority, cache mode, screen-on, trim policy), and
  the honest reading of the mode here is "the page that *hands off* a download
  gets the best chance of doing so quickly". This is stated rather than papered
  over: no mobile surface claims to accelerate a transfer the app never performs.
* **Software-decoded video** — STREAMING mode asks WebView for hardware playback
  and cannot force it; the desktop edition at least warns when no decoder is
  present.
