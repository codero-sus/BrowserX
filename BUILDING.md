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
| **The full browser on Windows** (`tools\build_windows.cmd`) | Visual Studio 2022 Build Tools with the C++ workload, Windows 10/11 SDK, Python 3.8+, Git, and the CEF **windows64** distribution | MSVC only, and no CMake or Visual Studio project file to keep in sync: `tools/build_windows.py` compiles the same sources the Makefile does. See the Windows section below. |
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

## Disk space: what BrowserX costs, and what you can trim

**Ask before deleting anything:**

```bash
make space                 # measured sizes, and what each one costs to get back
make space-apply           # remove the rebuildable outputs (keeps your profile)
make space-apply DEEP=1    # also the fetched CEF trees: 200 MB of download again
```

The figures for this checkout, measured rather than guessed: `build/` (object files) about
**94 MB**, `out/` (the test binary alone is 37 MB of it) about **40 MB**, and the committed
CEF headers **1.9 MB**. The CEF *binary* distribution is the big one: **~200 MB downloaded,
~1 GB extracted** — and it is the one thing here worth thinking about before removing,
because the download is not small.

Two rules the tool follows, and the reason `Tooling` here is not just a `du` wrapper:

* **`make clean` never deletes `out/profile`.** In portable mode the run profile — history,
  bookmarks, settings, caches — lives inside `out/`, so `rm -rf out` would destroy the only
  data in this project that cannot be regenerated. `make clean` removes everything in `out/`
  except that directory, and says so when it finds one. `make space-apply` has the same
  guard, checked immediately before each delete rather than once at startup.
* **Removal is separated from measurement.** `make space` deletes nothing, ever.

### Trimming Visual Studio

This is usually the biggest single number, and the most trimmable. The **Build Tools** are
the compiler without the IDE — and the IDE is most of the download:

| What | Space | Needed? |
|------|-------|---------|
| Build Tools + C++ workload (MSVC v143, x64/x86) | ~2 GB | **Yes** — this is the compiler |
| One Windows SDK (10.0.26100) | ~1 GB | **Yes** — `windows.h`, `kernel32.lib` |
| *Desktop development with C++* recommended extras: CMake tools, AddressSanitizer, testing tools, vcpkg | ~1–2 GB | **No** — BrowserX needs none of them |
| The full Visual Studio IDE | +6 GB and up | **No** — unless you want its debugger |
| Additional SDKs, Clang toolchain, ATL/MFC, ARM/ARM64 toolsets | 1–3 GB each | **No** — unless you build for them |

`BrowserX.vsconfig` in the repository root names exactly the three components this build
needs. If the Visual Studio Installer offers **More → Import configuration**, point it at
that file and it will install the compiler and one SDK, nothing else. Otherwise, in the
installer tick only:

* **Desktop development with C++** (workload)
* **MSVC v143 - VS 2022 C++ x64/x86 build tools (Latest)**
* **Windows 11 SDK (10.0.26100)** — or any one Windows 10/11 SDK; one is required

...and untick everything else on the right-hand pane, which is where most of the space
goes. The installer shows the running total as you tick, so the number is visible before
you commit to it.

## Building the browser on Windows

BrowserX on Windows is built with **MSVC** and the **prebuilt CEF Windows binaries**.
There is no building Chromium involved — that is the entire point of CEF: it *is*
Chromium, packaged as a binary distribution that you link against.

### Step 1 — the compiler: use the Visual Studio Installer

Download **Build Tools for Visual Studio 2022** ("Tools for Visual Studio" on the
[download page](https://visualstudio.microsoft.com/downloads/)), run it, and on the
*Workloads* tab tick **Desktop development with C++**. Then open the **Installation
details** pane on the right and leave ticked only:

* **MSVC v143 - VS 2022 C++ x64/x86 build tools (Latest)**
* **one** Windows SDK — the installer usually offers three:

| SDK offered | Take it? |
|-------------|----------|
| **Windows 11 SDK (10.0.26100)** | **Yes** — the newest, and the workload marks it *Recommended* |
| Windows 11 SDK (10.0.22621) or (10.0.22000) | Only if the newest refuses to install |
| Windows 10 SDK (10.0.19041) | The safe fallback: it installs on any Windows 10 |

**Exactly one.** BrowserX calls no version-specific API — the entire Windows surface it
uses is `CreateMutexA`, `CloseHandle`, `GetModuleFileNameA` and `GetFileAttributesA`, all
of which have existed since Windows XP — so the SDK version does not affect what can be
built or what the binary runs on. Two SDKs are the same build plus wasted disk. `make
requirements FOR=windows` says which one your machine will use and how many extra ones it
finds.

The SDK you build with does **not** set the oldest Windows the browser runs on: that is set
by Chromium, and since Chromium 110 that floor is **Windows 10 or later** (Chrome 109 was
the last release supporting Windows 7/8/8.1). CEF here is Chromium 144-era, so a Windows 7
machine could not run the result whatever SDK you install.

Untick CMake, vcpkg, Clang, ATL/MFC, and other SDKs: BrowserX needs none of them, and
together they are most of the download. The installer shows the running total, so you can
watch it fall as you untick. If your installer offers **More → Import configuration**,
point it at [`BrowserX.vsconfig`](BrowserX.vsconfig) in this repository instead — it lists
exactly those three components and nothing else.

`winget` can do the same thing in one line, but it asks for source agreements the first
time it runs, and it must be given the flags for them or it will sit at a prompt:

```bat
winget install --id Microsoft.VisualStudio.2022.BuildTools --accept-package-agreements ^
    --accept-source-agreements ^
    --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Then Python and Git — **one command at a time**, and **open a new window afterwards** so
the new PATH applies:

```bat
winget install --id Python.Python.3.12 --accept-package-agreements --accept-source-agreements
```

```bat
winget install --id Git.Git --accept-package-agreements --accept-source-agreements
```

> **If you paste several commands in one go**, the shell hands the later ones to whatever
> prompt is on screen — which is how a winget agreement prompt turns into the answer
> "winget install --id Python.Python.3.12". Paste one line, wait for it to finish.

### Step 2 — the Chromium/CEF binaries

```bat
python tools\fetch_cef.py
```

### Step 3 — build

```bat
tools\build_windows.cmd --stage
```

```bat
out\BrowserX.exe
```

`--stage` copies `libcef.dll` and its data files next to the executable, which Windows
needs because it has nothing like `rpath`: an exe and its DLLs must sit together.

### What each tool is for

| Tool | Why | Size |
|------|-----|------|
| **Visual Studio 2022 Build Tools**, workload *Desktop development with C++* | The compiler (`cl.exe`), the linker (`link.exe`) and the C++/20 standard library. MSVC's C++ toolset is called **v143**; 14.30 or newer is required for `std::string::starts_with` and the other C++20 library features this code uses. | ~3 GB download, ~7 GB installed |
| **Windows 10/11 SDK** | `windows.h`, `kernel32.lib` and the rest of the Win32 API. It arrives with the workload above; the build uses the newest one it finds. | ~1 GB (part of the above) |
| **Python 3.8+** | Fetches and SHA-1 verifies CEF, runs the build script, and runs every tool in `tools/`. | ~30 MB |
| **Git** | The checkout, and `tools\update.cmd`. | ~60 MB |
| **CEF windows64 distribution** | Chromium itself, prebuilt: `libcef.dll` (the browser engine), its headers, `libcef.lib`, and the C++ binding layer that is compiled into BrowserX. | ~200 MB download, ~1 GB extracted |

**What you do *not* need:** CMake, a Visual Studio project file, Ninja, MSYS2, or WSL.
The build script finds MSVC itself (via `vswhere.exe`, then on disk), so it works from a
plain `cmd.exe` without running `vcvarsall.bat` first — which is the usual reason a
Windows build fails with "cl.exe is not recognized".

### Commands worth knowing

```bat
tools\build_windows.cmd                     build out\BrowserX.exe (release)
tools\build_windows.cmd --debug             /Od /Zi, for a debugger
tools\build_windows.cmd --jobs 8            parallel compiles
tools\build_windows.cmd --dry-run           print every compiler command, run none
tools\build_windows.cmd --check-sources     prove it compiles the same files as the Linux build
tools\build_windows.cmd --check-portability  find POSIX-only includes that would break this build
python tools\check_build_requirements.py --for windows   the tool table for your machine
```

`--check-sources` and `--check-portability` are how the Windows build stays honest on a
machine that is not Windows: the first asks the Makefile for its source list and compares,
so the two builds cannot silently diverge, and the second refuses a POSIX include that is
not behind a platform guard. Both run on any OS.

### The honest state of this path

The Linux build is the one that has been compiled and tested here. The Windows path is
written from the same sources with the same rules, and it is checked as far as a Linux
machine can check it: the source list matches the verified Linux build exactly (33 files),
the portability audit passes, and `--dry-run` prints the exact `cl.exe` and `link.exe`
commands — but **it has not yet been compiled by MSVC on a real Windows machine**, because
none was available. The first build may need a small correction; if it does, the failure
will be a compiler or linker message naming the file, and that message is the whole fix.
`out\BrowserX.exe` from this path has never been run either.

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
