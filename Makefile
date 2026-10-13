# =============================================================================
#  BrowserX — Makefile
#  An ULTRA-optimised Chromium (CEF) browser with GAME / HIGH-PERFORMANCE /
#  LOW-RAM / ECO modes.
#
#  Quick start:
#      make            # show help
#      make headers    # CEF headers only  (works offline-ish; used for `make check`)
#      make deps       # download + verify + extract the pinned CEF build
#      make check      # compile-check every source file
#      make test       # build & run the unit tests (no CEF needed)
#      make bench      # print the resolved Chromium flag plan for every mode
#      make run        # build & launch the browser
# =============================================================================

SHELL          := /bin/bash
.DEFAULT_GOAL  := help

# -----------------------------------------------------------------------------
# Paths & configuration
# -----------------------------------------------------------------------------
ROOT           := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
SRC_DIR        := $(ROOT)/src
TEST_DIR       := $(ROOT)/tests/unit
TOOLS_DIR      := $(ROOT)/tools
OUT_DIR        := $(ROOT)/out
OBJ_DIR        := $(ROOT)/build/obj
CHECK_OBJ_DIR  := $(ROOT)/build/obj-check
TEST_OBJ_DIR   := $(ROOT)/build/obj-tests
DEPS_DIR       := $(ROOT)/.cef-deps
HEADERS_DIR    := $(ROOT)/.cef-headers

PYTHON         ?= python3
BUILD_TYPE     ?= Release
JOBS           ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Sanitizers:   make SANITIZE=asan build
SANITIZE       ?=
# Extra args for `make run`:   make run RUN_ARGS="--mode=gaming"
RUN_ARGS       ?=

# --- locate a real CEF binary distribution (preferred) -----------------------
ifdef BROWSERX_CEF_DIR
CEF_ROOT       := $(BROWSERX_CEF_DIR)
else
CEF_ROOT       := $(lastword $(sort $(wildcard $(DEPS_DIR)/cef_binary_*)))
endif
CEF_INCLUDE    := $(CEF_ROOT)/include
CEF_BINARY_DIR := $(CEF_ROOT)/$(BUILD_TYPE)

# --- fallback: header-only tree (used by `make check` in restricted envs) ----
CHECK_CEF_ROOT := $(HEADERS_DIR)
ifneq ($(wildcard $(CEF_INCLUDE)/cef_app.h),)
HAVE_CEF_DIST  := 1
else
HAVE_CEF_DIST  :=
endif
ifeq ($(HAVE_CEF_DIST),1)
CHECK_CEF_ROOT := $(CEF_ROOT)
endif

# -----------------------------------------------------------------------------
# Compiler / linker flags  (mirrors CEF's own cmake/cef_variables.cmake.in)
# -----------------------------------------------------------------------------
CXX            ?= g++
CC             ?= gcc

CXX_STD        := -std=c++20
WARN           := -Wall -Wno-unused-parameter -Wno-missing-field-initializers \
                  -Wno-deprecated-declarations -Wno-comment -Wno-sign-compare
BASE_FLAGS     := $(CXX_STD) -fno-rtti -fno-exceptions -fno-threadsafe-statics \
                  -fPIC -fno-strict-aliasing -fstack-protector -funwind-tables \
                  -fvisibility=hidden -fvisibility-inlines-hidden -pipe -pthread
DEFINES        := -D_FILE_OFFSET_BITS=64 -DUSING_CEF_SHARED -DBROWSERX_VERSION=\"$(shell $(PYTHON) -c "import json;print(json.load(open('$(ROOT)/cef.lock.json')).get('version') or 'dev')" 2>/dev/null)\"

ifeq ($(SANITIZE),asan)
SAN_FLAGS      := -fsanitize=address -fno-omit-frame-pointer -g -O1
LIBSAN         := -fsanitize=address
else ifeq ($(SANITIZE),ubsan)
SAN_FLAGS      := -fsanitize=undefined -fno-omit-frame-pointer -g -O1
LIBSAN         := -fsanitize=undefined
else ifeq ($(SANITIZE),tsan)
SAN_FLAGS      := -fsanitize=thread -fno-omit-frame-pointer -g -O1
LIBSAN         := -fsanitize=thread
else
SAN_FLAGS      :=
LIBSAN         :=
endif

ifeq ($(BUILD_TYPE),Debug)
OPT_FLAGS      := -O0 -g -DDEBUG
else
OPT_FLAGS      := -O2 -g1 -DNDEBUG -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2 \
                  -ffunction-sections -fdata-sections
endif

INCLUDES       := -I$(ROOT)/src -I$(CHECK_CEF_ROOT)

LD_FLAGS       := -fPIC -pthread -Wl,--disable-new-dtags -Wl,-z,noexecstack \
                  -Wl,-z,now -Wl,-z,relro -Wl,-O1 -Wl,--as-needed -Wl,--gc-sections \
                  -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,$(CEF_BINARY_DIR)
LIBS           := -lX11 -ldl -lm -lpthread $(LIBSAN)

# -----------------------------------------------------------------------------
# Source discovery
# -----------------------------------------------------------------------------
APP_SRCS       := $(shell find $(SRC_DIR) -name '*.cc' -not -name '*_unittest.cc')
CORE_SRCS      := $(shell find $(SRC_DIR)/core -name '*.cc')
TEST_SRCS      := $(shell find $(TEST_DIR) -name '*.cc' 2>/dev/null)
TOOL_SRCS      := $(wildcard $(TOOLS_DIR)/*.cc)
WRAPPER_SRCS   := $(shell find $(CEF_ROOT)/libcef_dll -name '*.cc' -not -path '*/tests/*' 2>/dev/null)

APP_OBJS       := $(patsubst $(ROOT)/%.cc,$(OBJ_DIR)/%.o,$(APP_SRCS))
WRAPPER_OBJS   := $(patsubst $(ROOT)/%.cc,$(OBJ_DIR)/%.o,$(WRAPPER_SRCS))
CHECK_OBJS     := $(patsubst $(ROOT)/%.cc,$(CHECK_OBJ_DIR)/%.o,$(APP_SRCS))
# App-layer files that are deliberately CEF-free and therefore unit tested too.
# Compiled by `make test` without CEF headers on the include path, so they must
# stay CEF-free (check-test-extra-purity enforces it).
TEST_EXTRA_SRCS := $(SRC_DIR)/app/cli.cc $(SRC_DIR)/app/app_paths.cc \
                   $(SRC_DIR)/browser/session.cc $(SRC_DIR)/browser/tab_commands.cc \
                   $(SRC_DIR)/web/internal_pages.cc
TEST_OBJS      := $(patsubst $(ROOT)/%.cc,$(TEST_OBJ_DIR)/%.o,$(TEST_SRCS)) \
                  $(patsubst $(ROOT)/%.cc,$(TEST_OBJ_DIR)/%.o,$(CORE_SRCS)) \
                  $(patsubst $(ROOT)/%.cc,$(TEST_OBJ_DIR)/%.o,$(TEST_EXTRA_SRCS))

BIN            := $(OUT_DIR)/BrowserX
TEST_BIN       := $(OUT_DIR)/browserx_tests
BENCH_BIN      := $(OUT_DIR)/browserx-flagplan
DOCTOR_BIN     := $(OUT_DIR)/browserx-doctor

# -----------------------------------------------------------------------------
# Help
# -----------------------------------------------------------------------------
.PHONY: help
help:
	@echo ""
	@echo "  BrowserX — build targets"
	@echo "  ------------------------------------------------------------------"
	@echo "  make deps            download + SHA-1 verify + extract pinned CEF"
	@echo "  make deps-update     resolve newest stable CEF, re-pin, download"
	@echo "  make deps-list       list recent CEF builds for this platform"
	@echo "  make headers         fetch CEF headers only (for compile checks)"
	@echo "  make build           build out/BrowserX ($(BUILD_TYPE))"
	@echo "  make debug           build with -O0 -g"
	@echo "  make check           compile-check every source file (strict)"
	@echo "  make test            build + run unit tests (no CEF required)"
	@echo "  make bench           print the resolved flag plan for all modes"
	@echo "  make doctor          environment + capability report"
	@echo "  make run             build and launch BrowserX"
	@echo "  make run-nosandbox   launch without the Chromium sandbox (dev)"
	@echo "  make smoke           run the headless smoke test (needs CEF)"
	@echo "  make package         assemble a distributable in out/dist/"
	@echo "  make sandbox-setup   chown/chmod chrome-sandbox (needs sudo)"
	@echo "  make preview         serve the clickable UI preview on :8770"
	@echo "  make clean / distclean"
	@echo ""
	@echo "  CEF root    : $(if $(CEF_ROOT),$(CEF_ROOT),<not downloaded yet>)"
	@echo "  CEF headers : $(CHECK_CEF_ROOT)"
	@echo "  platform    : $(shell uname -s -m)"
	@echo "  jobs        : $(JOBS)"
	@echo ""

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------
.PHONY: deps
deps:
	@$(PYTHON) $(ROOT)/tools/fetch_cef.py

.PHONY: deps-update
deps-update:
	@$(PYTHON) $(ROOT)/tools/fetch_cef.py --update

.PHONY: deps-list
deps-list:
	@$(PYTHON) $(ROOT)/tools/fetch_cef.py --list

.PHONY: headers
headers:
	@$(PYTHON) $(ROOT)/tools/fetch_cef.py --headers-only

# -----------------------------------------------------------------------------
# Build
# -----------------------------------------------------------------------------
$(OBJ_DIR)/%.o: $(ROOT)/%.cc
	@mkdir -p $(dir $@)
	@echo "  CXX  $(patsubst $(ROOT)/%,%,$<)"
	@$(CXX) $(BASE_FLAGS) $(WARN) $(OPT_FLAGS) $(SAN_FLAGS) $(DEFINES) $(INCLUDES) -MMD -MP -c $< -o $@

$(OBJ_DIR)/%.o: %.cc
	@mkdir -p $(dir $@)
	@echo "  CXX  $<"
	@$(CXX) $(BASE_FLAGS) $(WARN) $(OPT_FLAGS) $(SAN_FLAGS) $(DEFINES) -DWRAPPING_CEF_SHARED -I$(CEF_ROOT) -c $< -o $@

ifeq ($(WRAPPER_SRCS),)
WRAPPER_LIB :=
else
WRAPPER_LIB := $(OBJ_DIR)/libcef_dll_wrapper.a
endif

$(WRAPPER_LIB): $(WRAPPER_OBJS)
	@echo "  AR   libcef_dll_wrapper.a ($(words $(WRAPPER_OBJS)) objects)"
	@ar rcs $@ $(WRAPPER_OBJS)

.PHONY: build
build: $(BIN)

$(BIN): $(APP_OBJS) $(WRAPPER_LIB)
	@mkdir -p $(OUT_DIR)
	@echo "  LD   $(patsubst $(ROOT)/%,%,$@)"
	@$(CXX) $(BASE_FLAGS) $(OPT_FLAGS) $(SAN_FLAGS) $(APP_OBJS) $(WRAPPER_LIB) \
		-L$(CEF_BINARY_DIR) -lcef $(LD_FLAGS) $(LIBS) -o $@
	@echo ""
	@echo "  ✔ built $(patsubst $(ROOT)/%,%,$@)"
	@echo "    next: make run    (or: make package)"
	@echo ""

.PHONY: debug
debug:
	@$(MAKE) --no-print-directory BUILD_TYPE=Debug build

-include $(shell find $(OBJ_DIR) -name '*.d' 2>/dev/null)

# -----------------------------------------------------------------------------
# Compile checks — the "does it even compile" gate.
# With a CEF distribution: real object compilation.
# Header-only fallback:     -fsyntax-only against the CEF headers.
# -----------------------------------------------------------------------------
$(CHECK_OBJ_DIR)/%.o: $(ROOT)/%.cc
	@mkdir -p $(dir $@)
	@echo "  CHK  $(patsubst $(ROOT)/%,%,$<)"
	@$(CXX) $(BASE_FLAGS) $(WARN) -Werror=return-type -Werror=delete-non-virtual-dtor \
		-fsyntax-only $(OPT_FLAGS:-O2=-O0) $(SAN_FLAGS) $(DEFINES) -I$(ROOT)/src -I$(CHECK_CEF_ROOT) $<

.PHONY: check
check: check-headers check-core-purity $(CHECK_OBJS)
	@echo ""
	@echo "  ✔ compile check passed ($(words $(APP_SRCS)) source files)"
	@if [ -z "$(HAVE_CEF_DIST)" ]; then \
		echo "    note: CEF headers only — run 'make deps' for a full build"; \
	fi
	@echo ""

# Files listed in TEST_EXTRA_SRCS are compiled by `make test` without CEF headers
# on the include path, so a stray CEF include there would break the test build.
.PHONY: check-test-extra-purity
check-test-extra-purity:
	@bad=$$(grep -l "cef_" $(TEST_EXTRA_SRCS) 2>/dev/null || true); \
	if [ -n "$$bad" ]; then \
		echo "  ✘ these tested-alongside-core files include CEF and cannot be unit tested:"; \
		echo "$$bad"; exit 1; \
	fi

.PHONY: check-headers
check-headers:
	@if [ ! -f "$(CHECK_CEF_ROOT)/include/cef_app.h" ]; then \
		echo "  → CEF headers missing; fetching them now"; \
		$(PYTHON) $(ROOT)/tools/fetch_cef.py --headers-only >/dev/null; \
	fi

# The core library (and the app files the tests link) must stay free of CEF/UI
# dependencies so they can be unit tested without Chromium. Enforce mechanically.
.PHONY: check-core-purity
check-core-purity: check-test-extra-purity
	@bad=$$(grep -rlE '#include[[:space:]]*[<"](include/)?cef_[a-z_]+\.h[>"]' $(SRC_DIR)/core 2>/dev/null || true); \
	if [ -n "$$bad" ]; then \
		echo "  ✘ core purity violated — CEF include found in:"; echo "$$bad"; exit 1; \
	fi
	@bad=$$(grep -rlE '#include[[:space:]]*<(windows|X11|gtk)' $(SRC_DIR)/core 2>/dev/null || true); \
	if [ -n "$$bad" ]; then \
		echo "  ✘ core purity violated — platform include found in:"; echo "$$bad"; exit 1; \
	fi

# -----------------------------------------------------------------------------
# Unit tests — pure C++, no CEF, so they run anywhere (including CI sandboxes)
# -----------------------------------------------------------------------------
$(TEST_OBJ_DIR)/%.o: $(ROOT)/%.cc
	@mkdir -p $(dir $@)
	@echo "  TST  $(patsubst $(ROOT)/%,%,$<)"
	@$(CXX) $(CXX_STD) -fexceptions -frtti -O1 -g $(SAN_FLAGS) -DUNIT_TEST \
		-I$(ROOT)/src -I$(ROOT)/tests -MMD -MP -c $< -o $@

.PHONY: test
test: $(TEST_BIN)
	@echo ""
	@$(TEST_BIN) $(TEST_FILTER)
	@echo ""

$(TEST_BIN): $(TEST_OBJS)
	@mkdir -p $(OUT_DIR)
	@echo "  LD   $(patsubst $(ROOT)/%,%,$@)"
	@$(CXX) $(CXX_STD) -fexceptions -frtti -pthread $(SAN_FLAGS) $(TEST_OBJS) -o $@

# -----------------------------------------------------------------------------
# Tools: flag plan printer + doctor (core-only, runnable without CEF binaries)
# -----------------------------------------------------------------------------
$(BENCH_BIN): $(TOOLS_DIR)/flagplan.cc $(CORE_SRCS)
	@mkdir -p $(OUT_DIR)
	@echo "  CXX+LD tools/flagplan.cc"
	@$(CXX) $(CXX_STD) -O2 -pthread -I$(ROOT)/src $^ -o $@

$(DOCTOR_BIN): $(TOOLS_DIR)/doctor.cc $(CORE_SRCS)
	@mkdir -p $(OUT_DIR)
	@echo "  CXX+LD tools/doctor.cc"
	@$(CXX) $(CXX_STD) -O2 -pthread -I$(ROOT)/src $^ -o $@

.PHONY: bench
bench: $(BENCH_BIN)
	@$(BENCH_BIN) $(ARGS)

.PHONY: doctor
doctor: $(DOCTOR_BIN)
	@$(DOCTOR_BIN) $(ARGS)

# -----------------------------------------------------------------------------
# Run
# -----------------------------------------------------------------------------
.PHONY: run
run: build stage
	@echo "  → launching BrowserX$(if $(RUN_ARGS), $(RUN_ARGS),)"
	@cd $(OUT_DIR) && ./BrowserX $(RUN_ARGS)

.PHONY: run-nosandbox
run-nosandbox: build stage
	@echo "  → launching BrowserX (sandbox disabled — dev only)"
	@cd $(OUT_DIR) && ./BrowserX --no-sandbox $(RUN_ARGS)

.PHONY: smoke
smoke: build stage
	@cd $(OUT_DIR) && ./BrowserX --smoke-test $(RUN_ARGS)

# Symlink the CEF runtime next to our binary so `$ORIGIN` rpath resolves and
# Chromium finds icudtl.dat / *.pak / locales.
.PHONY: stage
stage:
	@if [ ! -d "$(CEF_BINARY_DIR)" ]; then \
		echo "  ✘ CEF binaries missing. Run: make deps"; exit 1; \
	fi
	@mkdir -p $(OUT_DIR)
	@for f in $(CEF_BINARY_DIR)/libcef.so $(CEF_BINARY_DIR)/libvk_swiftshader.so \
	          $(CEF_BINARY_DIR)/libvulkan.so.1 $(CEF_BINARY_DIR)/v8_context_snapshot.bin \
	          $(CEF_BINARY_DIR)/vk_swiftshader_icd.json $(CEF_BINARY_DIR)/chrome-sandbox; do \
		[ -e "$$f" ] && ln -sfn "$$f" "$(OUT_DIR)/$$(basename $$f)" || true; \
	done
	@ln -sfn $(CEF_ROOT)/Resources $(OUT_DIR)/Resources
	@ln -sfn $(CEF_ROOT)/Resources/locales $(OUT_DIR)/locales
	@echo "  ✔ CEF runtime staged into out/"

.PHONY: sandbox-setup
sandbox-setup:
	@if [ ! -e "$(OUT_DIR)/chrome-sandbox" ]; then echo "run 'make stage' first"; exit 1; fi
	@sudo chown root:root $(OUT_DIR)/chrome-sandbox && sudo chmod 4755 $(OUT_DIR)/chrome-sandbox
	@echo "  ✔ chrome-sandbox is now setuid root — the Chromium sandbox is active"

# -----------------------------------------------------------------------------
# Packaging
# -----------------------------------------------------------------------------
.PHONY: package
package: build
	@bash $(ROOT)/tools/package.sh

# -----------------------------------------------------------------------------
# UI preview (clickable design preview; runs in any browser, no CEF needed)
# -----------------------------------------------------------------------------
.PHONY: preview
preview:
	@$(PYTHON) $(ROOT)/preview/serve.py

# -----------------------------------------------------------------------------
# Clean
# -----------------------------------------------------------------------------
.PHONY: clean
clean:
	@rm -rf $(OUT_DIR) $(OBJ_DIR) $(CHECK_OBJ_DIR) $(TEST_OBJ_DIR)
	@echo "  ✔ cleaned build outputs"

.PHONY: distclean
distclean: clean
	@rm -rf $(DEPS_DIR) $(HEADERS_DIR)
	@echo "  ✔ removed downloaded CEF headers/binaries"
