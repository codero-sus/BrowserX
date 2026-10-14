# BrowserX — (c) 2026 codero-sus. All rights reserved.
# Personal, non-commercial use only. No modification, no redistribution or resale.
# Source-available, not open source — see LICENSE. https://the-codero.web.app

# ============================================================================
#  BrowserX mobile — native performance core
#
#  Builds libbrowserxcore.so out of the *shared* C++ core in the repository root
#  (../../../../../src/core) plus the JNI bridge. Nothing is duplicated: the same
#  mode engine, memory policy, auto-pilot rules, blocklist and omnibox parser the
#  desktop edition runs are compiled here for Android ABIs.
#
#  Override the core location if this directory is ever copied elsewhere:
#      cmake -DBROWSERX_CORE_DIR=/path/to/MyBrowser/src/core ..
# ============================================================================
cmake_minimum_required(VERSION 3.22.1)
project(browserxcore CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED BROWSERX_CORE_DIR)
    # jni -> main -> src -> app -> mobile-edition -> repository root -> src/core
    set(BROWSERX_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src/core")
endif()
if(NOT EXISTS "${BROWSERX_CORE_DIR}/mode_engine.cc")
    message(FATAL_ERROR "shared core not found at ${BROWSERX_CORE_DIR} (set -DBROWSERX_CORE_DIR=...)")
endif()

file(GLOB BROWSERX_CORE_SOURCES "${BROWSERX_CORE_DIR}/*.cc")

add_library(browserxcore SHARED
    ${BROWSERX_CORE_SOURCES}
    core_bridge.cc
)

target_include_directories(browserxcore PRIVATE "${BROWSERX_CORE_DIR}/..")

# The desktop build is CEF-free in src/core by construction (enforced by the
# Makefile's check-core-purity target), which is what makes this file possible.
target_compile_options(browserxcore PRIVATE
    -fno-rtti
    -fno-exceptions
    -fvisibility=hidden
    -Wall
    -Wextra
    -Wno-unused-parameter
)

# Keep the JNI entry points visible; everything else stays hidden.
target_link_options(browserxcore PRIVATE "-Wl,-z,max-page-size=16384")

# Debug symbols for crash reports in internal builds, stripped in release.
target_compile_definitions(browserxcore PRIVATE
    $<$<CONFIG:Debug>:BROWSERX_DEBUG=1>
)
