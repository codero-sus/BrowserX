#!/usr/bin/env bash
# ============================================================================
#  BrowserX mobile — build the shared native core without Gradle.
#
#  Handy while working on src/core (the same code the desktop edition tests) or
#  for CI that only wants the .so and the resolved plan. Gradle does exactly the
#  same thing through app/build.gradle -> externalNativeBuild.
#
#  Usage:
#      tools/build-native.sh                     # arm64-v8a, Release
#      tools/build-native.sh --abi=x86_64        # emulator
#      tools/build-native.sh --abi=all --debug
#      tools/build-native.sh --plan              # print the plan, build nothing
# ============================================================================
set -euo pipefail

ABI="arm64-v8a"
BUILD_TYPE="Release"
PRINT_PLAN=0
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JNI_DIR="${HERE}/../app/src/main/jni"
CORE_DIR="${HERE}/../../src/core"
OUT_DIR="${HERE}/../build/native"

for arg in "$@"; do
    case "$arg" in
        --abi=*)   ABI="${arg#*=}" ;;
        --debug)   BUILD_TYPE="Debug" ;;
        --release) BUILD_TYPE="Release" ;;
        --plan)    PRINT_PLAN=1 ;;
        -h|--help) sed -n '2,14p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

ANDROID_API=26                     # same minSdk as app/build.gradle
: "${ANDROID_NDK_HOME:=${ANDROID_NDK_ROOT:-${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}/ndk}}"

cat <<PLAN
BrowserX mobile — native core build plan
  shared core : ${CORE_DIR}
  jni bridge  : ${JNI_DIR}/core_bridge.cc
  cmake file  : ${JNI_DIR}/CMakeLists.txt
  abi         : ${ABI}
  build type  : ${BUILD_TYPE}
  output      : ${OUT_DIR}
  ndk         : ${ANDROID_NDK_HOME}

  Sources compiled into libbrowserxcore.so:
$(ls "${CORE_DIR}"/*.cc 2>/dev/null | sed 's|^|    |')
PLAN

if [ "${PRINT_PLAN}" = "1" ]; then
    exit 0
fi

if [ ! -d "${ANDROID_NDK_HOME}" ]; then
    cat >&2 <<'MISSING'
No Android NDK found.

Set ANDROID_NDK_HOME (or ANDROID_HOME / ANDROID_SDK_ROOT pointing at an SDK with
an ndk/ directory), or install it with:
    ${ANDROID_HOME}/cmdline-tools/latest/bin/sdkmanager "ndk;26.1.10909125"

The desktop edition does not need any of this: `make check`, `make test` and
`make bench` in the repository root compile and test the *same* core sources with
plain g++.
MISSING
    exit 3
fi

TOOLCHAIN="${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake"
if [ ! -f "${TOOLCHAIN}" ]; then
    echo "NDK toolchain file not found: ${TOOLCHAIN}" >&2
    exit 3
fi

cmake -S "${JNI_DIR}" -B "${OUT_DIR}/${ABI}" \
      -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
      -DANDROID_ABI="${ABI}" \
      -DANDROID_PLATFORM="android-${ANDROID_API}" \
      -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
      -DBROWSERX_CORE_DIR="${CORE_DIR}"
cmake --build "${OUT_DIR}/${ABI}" --parallel

echo
echo "built: $(find "${OUT_DIR}/${ABI}" -name 'libbrowserxcore.so' -print -quit)"
echo "copy it into app/src/main/jniLibs/${ABI}/ if you want Gradle to package a prebuilt one."
