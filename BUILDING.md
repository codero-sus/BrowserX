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

## Get the code with git, not a ZIP

A **Download ZIP** copy looks identical to a checkout and cannot be updated: every fix
means another download, and `tools\update.cmd` refuses to run because it is not a
repository. The repository is small (a few hundred KB — the CEF binaries you download are
not in it), so a checkout is the cheap option.

### You already have a ZIP copy

Six commands, in the folder you already have. Nothing is downloaded again:

```bat
git init
git remote add origin https://github.com/codero-sus/BrowserX.git
git fetch origin arena/419ef7ef-mybrowser
git reset --hard FETCH_HEAD
git checkout -B arena/419ef7ef-mybrowser
git branch --set-upstream-to=origin/arena/419ef7ef-mybrowser
```

Afterwards `git pull`, `tools\update.cmd` and `tools\update.cmd --check` all work, and
`git status` is clean. This has been rehearsed on a copy exactly like yours: a folder with
no `.git` and a 1.4 GB `.cef-deps` download in it — the download came through byte for
byte, because `git reset --hard` only writes the paths the repository knows about.

A copy of this project from this commit onwards also carries a tool that does the same
thing and explains itself first:

```bat
tools\adopt_clone.cmd
rem what would change (by file), and what would be kept. Nothing is touched.
tools\adopt_clone.cmd --yes
rem do it, then verify the result
```

If your copy is older than the tool, use the six commands above — that is what the tool
runs.

**What it keeps:** everything git does not track — `.cef-deps` (your CEF download, about
1.4 GB), `build`, `out`, and your profile inside `out/profile`. You do not re-download
anything because you changed how you get updates.

**What it replaces:** every file the repository tracks. If you edited `BUILDING.md`, that
edit goes; the dry run lists exactly which files are being replaced before it happens, and
it names the ones kept beside them.

### Or clone from scratch

```bat
git clone --branch arena/419ef7ef-mybrowser https://github.com/codero-sus/BrowserX.git
```

`--branch` matters: `main` is still the repository's first commit, so a plain
`git clone` gets an empty tree. Then, inside the clone:

```bat
python tools\fetch_cef.py
rem the Chromium/CEF binaries, about 200 MB, once
tools\build_windows.cmd --stage
```

### Staying up to date afterwards

```bat
tools\update.cmd --check
rem is there anything new? exit 10 when yes, 0 when no
tools\update.cmd
rem pull, rebuild what this machine can build, verify
```

`tools\update.cmd` and `tools\update.sh` are the same updater (`tools/update.py`); on
Windows it rebuilds with MSVC through `tools\build_windows.py`. Nothing in it runs
`git clean`, so the profile in `out/profile` cannot be deleted by an update.

## Building is a git operation

Every build records the commit it came from, and the binary can tell you itself:

```bat
out\BrowserX.exe --version
```

```text
BrowserX 154.0.34 by codero-sus <https://the-codero.web.app> [409d710] (x86_64, CEF/Chromium, release)
```

`409d710` is the commit. If the folder had uncommitted edits when it was built, it reads
`[409d710+]` — the `+` means the commit alone does not describe that binary. A build from
a folder that is not a checkout says `[unknown]` instead of inventing a number.

`browserx://version` shows the same thing under **Built from**, so it is visible while the
browser is running.

The same commands answer the other direction — is what I have *current*?

```bat
python tools\revision.py
rem branch, commit, subject, clean or dirty, ahead/behind the remote
python tools\revision.py --check out\build-info.json
rem exit 0 = built from this code, 10 = rebuild, 3 = cannot tell
```

`out/build-info.json` is written by both builders (`make` and `tools\build_windows.cmd`)
after a successful link, and records the commit, branch, dirty flag, subject and exe size.
`tools\update.cmd` checks it after rebuilding, so an update that pulled new code but left
an old binary is reported as a failure rather than as success.

How the revision gets in: `tools/revision.py --header` writes `build/generated/revision.h`
(git-ignored), and both build systems force-include it — g++ `-include`, MSVC `/FI`. It is
rewritten **only when its content changes**, so an unchanged revision does not trigger a
full rebuild.

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

A wrong CEF build is the largest thing this project can waste: one extracted distribution
is about 1.4 GB. If a build other than the pinned one is sitting in `.cef-deps`, remove it
with the tool that put it there:

```bat
python tools\fetch_cef.py --prune
rem lists the trees that are not the pinned one, and what each is costing
python tools\fetch_cef.py --prune --apply
rem removes them; the pinned tree is never touched
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
| **Windows 11 SDK (10.0.26100)** | **Yes** — the released SDK this project targets, and the one the workload marks *Recommended* |
| Windows 11 SDK (10.0.28000) | **Not needed.** It is a Windows Insider **preview** SDK (Canary channel, the 26H1-era builds), not a released one. It *would* build BrowserX — the APIs used are ancient — but a preview toolchain is a strange thing to ship a browser against, for no gain. Take it only if it is the only SDK you have |
| Windows 11 SDK (10.0.22621) or (10.0.22000) | Older duplicates: not needed if you have 26100 |
| Windows 10 SDK (10.0.19041) | The safe fallback: it installs on any Windows 10 |

**Exactly one.** The build uses the newest SDK it finds, and when that is newer than
10.0.26100 it says so rather than choosing in silence — because *newer is not better here*:
nothing in this browser calls an API that has changed since Windows XP, so a newer SDK adds
nothing, and one numbered above the released build is usually an Insider preview.

BrowserX calls no version-specific API — the entire Windows surface it
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

That download is about 200 MB and it is the one step that depends on someone else's
server, so it is worth knowing how the version is chosen. The script asks the CEF index
for the versions published for your platform and takes the **newest one by number** — not
the first or the last line of the index, because a list's order is not a fact about a
version. It then uses the exact file name and SHA-1 the index publishes for that build.

Two rules decide which build that is, and both exist because of a real failure:

* **The numbers decide, never the order of the list.** The index lists builds newest
  first; taking its first or last line is how a Windows machine was once offered a 2016
  build of Chromium 51.
* **A released build is chosen, not just the newest one.** The newest entry can be a
  beta: that same machine then started downloading
  `cef_binary_156.0.7+..._windows64_beta.tar.bz2`. Pre-releases (beta, dev, canary) are
  skipped and the skip is reported; if there is nothing but pre-releases, the newest is
  used and the tool says so rather than pretending. This is the same rule the Windows SDK
  section states — newer is not better.

The sources in this repository are compile-checked against the headers of
**CEF 154.0.34 (Chromium 154.0.8037)** — the build a plain fetch resolves to today — so a
green `make check` here means the same code compiles against the CEF version you are
about to download, not merely against whatever headers happened to be around.

If you want to see what it would take before it downloads anything:

```bat
python tools\fetch_cef.py --list
rem newest first, and the one a plain fetch would take is marked
python tools\fetch_cef.py --version 141.0.12+g3f1c9a2+chromium-141.0.7390.55
rem exactly that build instead
```

An older build is never chosen for you because it happened to be listed last, and a
pre-release is never chosen because it happened to be newest: both were real failures, on
a real machine, and `make check-fetch` replays the captured index data that caused them on
every build.

### Step 3 — build

```bat
tools\build_windows.cmd --stage
```

```bat
out\BrowserX.exe
```

`--stage` copies `libcef.dll` and its data files next to the executable, which Windows
needs because it has nothing like `rpath`: an exe and its DLLs must sit together.

### If the compiler reports a missing standard header

```
fatal error C1083: Cannot open include file: 'string': No such file or directory
```

That is not a problem with the source file. `<string>`, `<optional>` and `<cstdint>` live
inside the compiler's own installation, and `cl.exe` finds them through the `INCLUDE`
environment variable — which a fresh command prompt does not have. The build script sets
`INCLUDE`, `LIB` and `PATH` itself, so this should not happen; it was exactly what went
wrong on the first real Windows build, for two reasons that are both fixed:

* `INCLUDE` was never set at all (only `PATH`), so the compiler could not find its own
  standard library, and every one of 246 files failed the same way.
* the compiler's include directory was added to the command line only *when an SDK was
  found* — so on a machine where the SDK was not discovered, the flag went missing too.

Both are covered by `tools\build_windows.py --self-test`, which builds a fake toolchain
on disk and asserts that `INCLUDE` carries the compiler's headers and the SDK's, in the
order `vcvarsall.bat` would set them. If you still see this message, run:

```bat
python tools\build_windows.py --dry-run
```

and look at the `Windows SDK:` line, and at the `/I` flags in the commands it prints: they
are the directories the compiler will actually search.

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

### Or build it from a window

```bat
python tools\build_gui.py
```

A Tkinter front-end — nothing to pip install, because the window is meant to work on a
machine that is already short of space. It shows what this machine has (compiler, SDK, CEF),
greys out the buttons it cannot run and says why, and every button runs a command that
exists on the command line: `tools\build_windows.py`, `tools\fetch_cef.py`,
`tools\update.py`, `tools\disk_report.py`. It is a front-end, never a second build system,
so it cannot disagree with `tools\build_windows.cmd` about how the browser is built.

Output streams into the window as it runs, and **Stop** kills the whole process tree — a
build that is stopped must not leave compilers running.

On a machine with no display, or from a script, the same state prints instead:

```bash
python tools/build_gui.py --check        # exactly what the window would show
python tools/build_gui.py --self-test    # the window's logic, no window
```

`make gui` opens the window and `make check-gui` (`make gui-check` also works) runs the
self-test, which is what those two commands do on the Linux side. `make check` runs
`check-gui`, so the window's logic is checked on every build even though the window itself
is only ever looked at by a person.

### It builds only what changed

The Windows build is incremental, the same way `make` is on Linux. The first build compiles
everything; after that a source is recompiled only when it changed, when a header it
includes changed (transitively), or when something about the build itself changed — the
compiler, the flags, the include paths, or the CEF pin. Measured on this repository: a
rebuild with nothing changed compiles **nothing**; editing one `.cc` compiles **one file**;
editing `src/core/format.h` recompiles the **26 files that include it** and reuses the
other 8.

```bat
rem compiles only what changed (the default)
tools\build_windows.cmd
rem and says why each file was recompiled
tools\build_windows.cmd --verbose
rem ignore the cache and rebuild everything
tools\build_windows.cmd --force
```

If the dependency record is missing or unreadable the file is compiled rather than
assumed good: the failure mode of a cache like this has to be *doing the work*, never
linking something stale.

### What you do *not* need: a build accelerator

**Incredibuild** (the "Incredibuild - Build Acceleration" component in the VS installer) is
in no way required, and on a single machine it would do nothing here:

* It accelerates builds driven by **MSBuild or the Visual Studio IDE**, by intercepting the
  `cl.exe` processes *that* build spawns. `tools\build_windows.py` calls `cl.exe` directly,
  so there is nothing for it to intercept unless the whole build is run under its own
  launcher.
* Its free tier is **local** acceleration — using your own cores. This build already does
  that: `--jobs` defaults to one compile per CPU core.
* Its paid value is **distributed** builds across several machines. With one PC there is
  nothing to distribute to.
* It installs an agent and a coordinator service, and takes gigabytes of disk on a machine
  that is already trimming.

The honest version of "my builds are slow" was that this build recompiled every file on
every run; that is fixed above, which is worth more than a build accelerator would have
been. For reference, a full Windows build is about 180 translation units (the browser, the
tools, and CEF's own binding layer, which is most of them) — enough that recompiling all of
them on every keystroke was the real problem.

### Commands worth knowing

```bat
tools\build_windows.cmd                     build out\BrowserX.exe (release)
tools\build_windows.cmd --debug             /Od /Zi, for a debugger
tools\build_windows.cmd --jobs 8            parallel compiles
tools\build_windows.cmd --dry-run           print every compiler command, run none
tools\build_windows.cmd --check-sources     prove it compiles the same files as the Linux build
tools\build_windows.cmd --check-portability  find POSIX-only includes that would break this build
python tools\build_gui.py                   the same build steps, as a window
python tools\check_build_requirements.py --for windows   the tool table for your machine
```

Every command in this file and in the README is checked by `make check-docs`: no note
sitting after a command on the same line (in `cmd.exe` that is an argument, not a comment,
which is how `python tools\fetch_cef.py  rem downloads the CEF runtime` came to fail), every
`tools\` script named exists, every `--flag` is one that tool accepts, and every
`make <target>` is a target that exists.

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
