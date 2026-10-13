#!/usr/bin/env bash
# BrowserX — assemble a distributable in out/dist/.
#
#   make package
#
# Produces a directory that runs on another Linux machine of the same
# architecture without a build step:
#
#   out/dist/BrowserX-<version>-linux64/
#     BrowserX              the executable
#     browserx              launcher script (sets up the sandbox-friendly env)
#     libcef.so, *.so.1     the Chromium runtime
#     Resources/            icudtl.dat, *.pak
#     locales/              per-language .pak files
#     VERSION               version + build provenance
#     README.txt            how to run it, and what to do if it refuses to start
#
# Deliberately conservative: it copies (never symlinks) everything, verifies the
# payload is complete, and refuses to produce a package that cannot start.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/.." && pwd)"
OUT_DIR="${ROOT}/out"
DEPS_DIR="${ROOT}/.cef-deps"

say()  { printf '  %s\n' "$*"; }
step() { printf '→ %s\n' "$*"; }
die()  { printf '\n  ✘ %s\n\n' "$*" >&2; exit 1; }

# ------------------------------------------------------------------ layout ---
CEF_ROOT="${BROWSERX_CEF_DIR:-}"
if [[ -z "${CEF_ROOT}" ]]; then
  CEF_ROOT="$(ls -d "${DEPS_DIR}"/cef_binary_* 2>/dev/null | sort | tail -1 || true)"
fi
[[ -n "${CEF_ROOT}" && -d "${CEF_ROOT}" ]] || die "no CEF distribution found. Run: make deps"

BUILD_TYPE="${BUILD_TYPE:-Release}"
CEF_BIN="${CEF_ROOT}/${BUILD_TYPE}"
[[ -d "${CEF_BIN}" ]] || CEF_BIN="$(ls -d "${CEF_ROOT}"/Release "${CEF_ROOT}"/Debug 2>/dev/null | head -1)"
[[ -d "${CEF_BIN}" ]] || die "CEF binaries not found under ${CEF_ROOT} (looked for ${BUILD_TYPE}/)"

BIN="${OUT_DIR}/BrowserX"
[[ -x "${BIN}" ]] || die "out/BrowserX is missing. Run: make"

VERSION="$(python3 - "${ROOT}/cef.lock.json" <<'PY'
import json, sys
try:
    lock = json.load(open(sys.argv[1]))
except Exception:
    print("dev"); raise SystemExit
print(lock.get("version") or "dev")
PY
)"
ARCH="$(uname -m)"
PLATFORM="linux64"
[[ "${ARCH}" == "aarch64" || "${ARCH}" == "arm64" ]] && PLATFORM="linuxarm64"
PKG_NAME="BrowserX-${VERSION%%+*}-${PLATFORM}"
DIST="${OUT_DIR}/dist"
TARGET="${DIST}/${PKG_NAME}"

step "packaging BrowserX ${VERSION} for ${PLATFORM}"
rm -rf "${TARGET}"
mkdir -p "${TARGET}"

# ------------------------------------------------------------------ payload --
step "copying the browser"
install -m 0755 "${BIN}" "${TARGET}/BrowserX"

step "copying the CEF runtime"
copied=0
for pattern in 'libcef.so' '*.so.1' '*.so' 'vk_swiftshader_icd.json' 'v8_context_snapshot.bin' 'chrome-sandbox'; do
  for file in "${CEF_BIN}"/${pattern}; do
    [[ -e "${file}" ]] || continue
    name="$(basename "${file}")"
    [[ "${name}" == "BrowserX" ]] && continue
    [[ -e "${TARGET}/${name}" ]] && continue
    cp -L "${file}" "${TARGET}/${name}"
    [[ "${name}" == "chrome-sandbox" ]] && chmod 4755 "${TARGET}/${name}" 2>/dev/null || true
    copied=$((copied + 1))
  done
done
[[ "${copied}" -gt 0 ]] || die "no CEF libraries were copied from ${CEF_BIN}"
say "runtime files: ${copied}"

step "copying resources and locales"
RESOURCES="${CEF_ROOT}/Resources"
if [[ -d "${RESOURCES}" ]]; then
  mkdir -p "${TARGET}/Resources"
  cp -aL "${RESOURCES}/." "${TARGET}/Resources/"
  if [[ -d "${TARGET}/Resources/locales" ]]; then
    mv "${TARGET}/Resources/locales" "${TARGET}/locales"
  fi
  say "pak files: $(find "${TARGET}/Resources" -maxdepth 1 -name '*.pak' | wc -l | tr -d ' ')"
  say "locales  : $(find "${TARGET}/locales" -maxdepth 1 -name '*.pak' 2>/dev/null | wc -l | tr -d ' ')"
else
  die "CEF Resources/ directory is missing (${RESOURCES}) — the browser will not render text without icudtl.dat"
fi

for required in "BrowserX" "libcef.so" "Resources/icudtl.dat" "Resources/resources.pak"; do
  [[ -e "${TARGET}/${required}" ]] || die "packaged payload is incomplete: ${required} is missing"
done

# ------------------------------------------------------------------ launcher -
cat > "${TARGET}/browserx" <<'LAUNCH'
#!/usr/bin/env bash
# BrowserX launcher: keeps the runtime and profile paths relative to this
# directory, and explains the two failures people actually hit.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export BROWSERX_RESOURCES="${HERE}/Resources"
export BROWSERX_LOCALES="${HERE}/locales"
export LD_LIBRARY_PATH="${HERE}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

if [[ ! -e /dev/shm ]] || [[ "$(df -m /dev/shm 2>/dev/null | awk 'NR==2 {print $2}')" -lt 256 ]]; then
  export BROWSERX_EXTRA_FLAGS="${BROWSERX_EXTRA_FLAGS:-} --disable-dev-shm-usage"
fi

exec "${HERE}/BrowserX" \
  --resources-dir="${HERE}/Resources" \
  --locales-dir="${HERE}/locales" \
  ${BROWSERX_EXTRA_FLAGS:-} "$@"
LAUNCH
chmod 0755 "${TARGET}/browserx"

# ------------------------------------------------------------------ metadata -
{
  printf 'BrowserX %s\n' "${VERSION}"
  printf 'packaged : %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf 'platform : %s\n' "${PLATFORM}"
  printf 'host     : %s\n' "$(uname -srm)"
  printf 'cef      : %s\n' "$(basename "${CEF_ROOT}")"
  printf 'build    : %s\n' "${BUILD_TYPE}"
  printf 'compiler : %s\n' "$(c++ --version 2>/dev/null | head -1 || echo unknown)"
  printf 'tests    : %s\n' "$(cd "${ROOT}" && ls tests/unit/*_test.cc 2>/dev/null | wc -l | tr -d ' ') suites in the source tree"
} > "${TARGET}/VERSION"

cat > "${TARGET}/README.txt" <<'README'
BrowserX
========

An ULTRA-optimised Chromium browser with five performance modes (ULTRA, GAME,
HIGH PERFORMANCE, LOW RAM, ECO) and Auto-Pilot, built on CEF.

Run it
------
    ./browserx                 start the browser
    ./browserx --help          every command-line option
    ./browserx --mode=gaming   start in GAME mode
    ./browserx --flag-plan=all print the Chromium switches this machine would use
    ./browserx --smoke-test    start headless, load a page, exit non-zero on failure
    ./browserx --doctor        diagnose a machine that refuses to start

If it does not start
--------------------
1. ./browserx --doctor            says which library is missing and how to install it.
2. "Failed to load swiftshader"   your machine has no GPU access in this session;
                                  run ./browserx --disable-gpu to confirm.
3. "The SUID sandbox helper"      in containers, run ./browserx --no-sandbox
                                  (only inside a container/VM you trust).
4. Blank window                   delete the profile directory printed by --doctor
                                  and start again.

Requirements
------------
A 64-bit Linux with glibc 2.31+, GTK-3, NSS, ALSA and X11/Wayland libraries. The
runtime is bundled; no build step and no separate CEF install is needed.

Licences: BrowserX is BSD-3-Clause; the bundled Chromium/CEF runtime is under the
licences listed in Resources/ (see also chromiumembedded/cef).
README

step "writing a tarball"
TARBALL="${DIST}/${PKG_NAME}.tar.gz"
tar -czf "${TARBALL}" -C "${DIST}" "${PKG_NAME}"
SHA="$(sha256sum "${TARBALL}" | cut -d' ' -f1)"
printf '%s  %s\n' "${SHA}" "$(basename "${TARBALL}")" > "${TARBALL}.sha256"

SIZE="$(du -sh "${TARGET}" | cut -f1)"
printf '\n'
say "✔ package ready"
say "  directory : ${TARGET}"
say "  tarball   : ${TARBALL} ($(du -h "${TARBALL}" | cut -f1))"
say "  checksum  : ${SHA}"
say "  contents  : ${SIZE} installed"
printf '\n  next: %s\n\n' "cd ${TARGET} && ./browserx --doctor"
