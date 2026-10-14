# Credits

## BrowserX

**Built by codero-sus.**

| | |
|---|---|
| Author | codero-sus |
| GitHub | [github.com/codero-sus](https://github.com/codero-sus) |
| Repository | [github.com/codero-sus/BrowserX](https://github.com/codero-sus/BrowserX) |
| Website | [the-codero.web.app](https://the-codero.web.app) |
| Licence | [BrowserX Personal-Use License](LICENSE) — personal, non-commercial use only; no modification; no redistribution or resale |

BrowserX — an ULTRA-optimised Chromium browser with fifteen performance modes
(ULTRA · GAME · HIGH PERFORMANCE · LOW RAM · ECO · SUPER INTERNET · DOWNLOAD ·
STREAMING · PRIVACY · DATA SAVER · WEAK NETWORK · LOW LATENCY · READER ·
BENCHMARK · KIOSK), Auto-Pilot and a tracker shield, in two editions
(C++/CEF desktop, Android mobile) that share one performance core.

**Licence, in plain words.** BrowserX is *source-available*, not open source.
You may read the code and run the browser for your own personal, non-commercial
use. You may **not** modify it, redistribute it, publish it, or sell it. See
[LICENSE](LICENSE) for the exact terms — this file is only the summary.

Bug reports, feature requests and suggestions are genuinely welcome on GitHub;
sending them does not give you rights over the Software (LICENSE section 6).

## Upstream — the giants whose shoulders this stands on

| Project | What BrowserX uses it for | Licence |
|---|---|---|
| [Chromium](https://www.chromium.org/) | the engine itself: Blink, V8, the network stack, the sandboxed multi-process architecture | BSD-3-Clause + third-party licences |
| [CEF — Chromium Embedded Framework](https://github.com/chromiumembedded/cef) | embedding Chromium in a C++ application: process model, Views UI toolkit, resource loading | BSD-3-Clause |
| [AndroidX-free Android platform APIs](https://developer.android.com/) | the mobile edition's UI (framework widgets only) and `WebView`, which is Chromium | — |
| [Chromium source tree](https://source.chromium.org/) | the ground truth behind every optimisation: each switch in `src/core/chromium_catalog.cc` records the file it was verified against | — |

The bundled Chromium/CEF runtime that `make deps` downloads and `make package`
ships stays under its own licences — see `Resources/` in a packaged build and the
CEF distribution's own `LICENSE`. The [BrowserX Personal-Use License](LICENSE)
covers **only** the original BrowserX code; it cannot and does not relicense
anything above, and none of those licences give anyone rights to BrowserX's
original code.

## Honest note about the tools

BrowserX was developed with AI assistance (Arena.ai agent mode). Every
optimisation claim in this repository is backed by something you can re-run
yourself: `make bench` prints the resolved switch plan with the Chromium source
file each switch came from, `make test` runs the unit tests over the CEF-free
core, and `make check` compiles every source file against the real CEF headers.
Where something is not finished, the code says so instead of pretending
(for example `browserx://history` states plainly that its store is not wired up
yet).

Because BrowserX is personal-use-only, one practical note: a public repository is
readable and forkable *through GitHub's own interface*, but this licence gives
nobody the right to use, modify or publish what they find. If the goal is a fully
closed product, a private repository plus published binaries is the reliable
route.
