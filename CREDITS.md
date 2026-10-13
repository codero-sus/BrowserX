# Credits

## BrowserX

**Built by codero-sus.**

| | |
|---|---|
| GitHub | [github.com/codero-sus](https://github.com/codero-sus) |
| Repository | [github.com/codero-sus/MyBrowser](https://github.com/codero-sus/MyBrowser) |
| Website | [the-codero.web.app](https://the-codero.web.app) |

BrowserX — an ULTRA-optimised Chromium browser with five performance modes
(ULTRA · GAME · HIGH PERFORMANCE · LOW RAM · ECO), Auto-Pilot and a tracker
shield, in two editions (C++/CEF desktop, Android mobile) that share one
performance core.

If you use BrowserX or parts of it, please keep this file and the copyright
notice with it. Bug reports, patches and mode suggestions are welcome on GitHub.

## Upstream — the giants whose shoulders this stands on

| Project | What BrowserX uses it for | Licence |
|---|---|---|
| [Chromium](https://www.chromium.org/) | the engine itself: Blink, V8, the network stack, the sandboxed multi-process architecture | BSD-3-Clause + third-party licences |
| [CEF — Chromium Embedded Framework](https://github.com/chromiumembedded/cef) | embedding Chromium in a C++ application: process model, Views UI toolkit, resource loading | BSD-3-Clause |
| [AndroidX-free Android platform APIs](https://developer.android.com/) | the mobile edition's UI (framework widgets only) and `WebView`, which is Chromium | — |
| [Chromium source tree](https://source.chromium.org/) | the ground truth behind every optimisation: each switch in `src/core/chromium_catalog.cc` records the file it was verified against | — |

The bundled Chromium/CEF runtime that `make deps` downloads and `make package`
ships stays under its own licences — see `Resources/` in a packaged build, and
`LICENSE` in the CEF distribution. BrowserX's own code is BSD-3-Clause (see
[LICENSE](LICENSE)); those licences do not cover the bundled runtime.

## Honest note about the tools

BrowserX was developed with AI assistance (Arena.ai agent mode). Every
optimisation claim in this repository is backed by something you can re-run
yourself: `make bench` prints the resolved switch plan with the Chromium source
file each switch came from, `make test` runs the unit tests over the CEF-free
core, and `make check` compiles every source file against the real CEF headers.
Where something is not finished, the code says so instead of pretending
(for example `browserx://history` states plainly that its store is not wired up
yet).
