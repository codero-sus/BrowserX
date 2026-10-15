# BrowserX — build requirements

Two commands answer this page for your machine, instead of you comparing versions by eye:

```bash
make requirements            # a table: what is here, what is missing, per build target
make requirements FOR=core   # only the core; exits non-zero when it cannot be built
```

`make requirements` never installs anything and never downloads: it looks, reports, and
names the package or command that fixes each gap. `make doctor` is the other half — it
asks whether the built browser can *run* here (display, GL, sandbox, caches).

---

## The four things you can build, and what each needs

| You want | Requirement | Notes |
|----------|-------------|-------|
| **Core, tests, tools** (`make test`, `make check`, `make bench`, `make perf`, `make pages`) | A C++20 compiler (g++ 12+ or clang 15+), GNU make 4+, Python 3.8+ | No CEF, no network, no display. Python is used by the Makefile for the CEF fetcher and the ledger checker. |
| **The full browser** (`make`, `make run`) | Everything above, plus CEF (about 200 MB download), X11 and GTK 3 development files, and a display to open a window | `make deps` downloads, SHA-1 verifies and extracts the pinned CEF build. Headless machines can build and use `make smoke`. |
| **The Android edition** | JDK 17+, Android SDK 34+, NDK r26+, Gradle 8.6+ | The shared performance core cross-compiles to `libbrowserxcore.so`; see `mobile-edition/README.md`. |
| **Preview and DOM checks** (`make preview`, `make ui-check`) | Python 3 (preview). Node plus jsdom for `make ui-check` only | Optional. The browser itself has no JavaScript dependencies, and the preview needs no build at all. |

### Versions this project is developed against

| Tool | Version used here | Floor |
|------|-------------------|-------|
| g++ | 12.2 (Debian 12) | 12 — the code uses C++20 library features (`std::string::starts_with`, `std::optional`, designated initialisers through C++20 aggregates) |
| clang++ | not required | 15 if used instead |
| GNU make | 4.3 | 4.0 |
| Python | 3.11 | 3.8 (stdlib only: no pip packages, ever) |
| CEF | pinned in `cef.lock.json` | Chromium 144-era (`CEF_API_VERSION_EXPERIMENTAL 999999` is accepted) |
| Node | 22 (only for `make ui-check`) | 18 |
| JDK / SDK / NDK / Gradle | 17 / 34 / r26 / 8.6 | same |

### Disk and network

* **Disk:** about 130 MB for the object files, the test binary and the tools (measured on
  a full test run: build/ at 89 MB, out/ at 35 MB); roughly 1.5 GB more once CEF is
  extracted. `make requirements` checks for 400 MB, which covers a build with the CEF
  objects as well.
* **Network:** needed once, by `make deps`, to fetch CEF. Everything after that builds and
  runs offline. Offline machines have three supported ways in, all of which skip the
  download and still verify what they install:

  ```bash
  BROWSERX_CEF_DIR=/opt/cef_binary_... make           # use an existing distribution
  BROWSERX_CEF_ARCHIVE=cef_binary.tar.bz2 make deps   # install from a local archive
  BROWSERX_CEF_URL=https://mirror/... make deps       # a mirror (still SHA-1 checked)
  ```

  `make headers` fetches only the CEF headers — enough for `make check`, not enough to
  link. They are tracked in the repository on purpose: that is why `make check` (the
  compile check and the ledger gate) works on a machine with no network at all, which is
  how the checks for this project are run. `make headers` refreshes them.

### Distribution packages

Debian/Ubuntu (the build machine used for this project):

```bash
sudo apt install g++ make python3 libx11-dev libgtk-3-dev
```

Fedora:

```bash
sudo dnf install gcc-c++ make python3 libX11-devel gtk3-devel
```

Arch:

```bash
sudo pacman -S base-devel python libx11 gtk3
```

The browser needs no GTK at *build* time beyond the headers CEF's build requires; if you
only ever run `make test`, `make check` or the tools, the compiler, make and Python are
the whole list.

---

## What each build step does, so the requirements make sense

```bash
make requirements   # is this machine able to build what I want?
make deps           # CEF: download -> SHA-1 verify -> extract (.cef-deps/, pinned in cef.lock.json)
make check          # compile every source file against the real CEF headers + the ledger gate
make test           # 274 unit tests over the CEF-free core (no CEF needed at all)
make                # link out/BrowserX (needs the CEF binaries from make deps)
make run            # start it
make smoke          # headless start-up probe: does the browser actually come up?
make doctor         # runtime environment, not build requirements
```

`make check` is deliberately split into `check-compile` (the compiler) and
`check-improvements` (the improvement ledger, see `docs/IMPROVEMENTS.md`), so a failing
ledger can never hide a compiler error, and vice versa.

## Updating BrowserX

```sh
make update            # pull, rebuild what this machine can build, verify
make update-check      # only look: exit 10 when there is something to pull, 0 when not
```

On Windows use `tools\update.cmd` with the same flags. It runs the identical
`tools/update.py` inside WSL, so the two platforms cannot drift apart; without WSL it still
updates the source and says plainly that building needs Linux or WSL. There is no `git
clean` anywhere in it, because your run profile — history, bookmarks, settings — lives
inside this directory, and a rollback happens only when you ask for one with `--rollback`.

Exit codes are the useful part when you script this: **0** updated or already current,
**10** with `--check` when an update is waiting, **1** when something failed. A pull is
fast-forward only: if your branch has local commits the updater stops and tells you instead
of merging something you did not ask for. With a dirty working tree it also stops, unless
you pass `--stash`, which stashes your changes and prints the command that brings them back.

Useful flags: `--dry-run` prints the plan and changes nothing, `--json` prints one machine
readable object (the human log goes to stderr), `--no-build` and `--no-verify` skip the
rebuild and the tests, `--jobs N` sets the compiler parallelism, and `--deps` fetches CEF
(about 200 MB) if you actually want the browser build rather than the core.

## Container and sandbox notes

Chromium's own sandbox needs privileges most containers do not have: use
`make run-nosandbox` there. A container also usually reports a small `/dev/shm`, which is
why `make doctor` checks it — CEF crashes in a small `/dev/shm` unless
`--disable-dev-shm-usage` is applied, and the mode engine applies it when the profile says
so. Neither of these affects the build; both affect running.

## Licence

BrowserX is source-available under the **BrowserX Personal-Use License** — personal,
non-commercial use only; no modification; no redistribution or resale. See `LICENSE`.
Upstream Chromium, CEF and Android components keep their own licences and are credited on
`browserx://about`.
