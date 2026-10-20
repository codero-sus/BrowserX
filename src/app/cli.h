// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — command line.
//
// This file is deliberately CEF-free: it is compiled into the unit tests too
// (`make test`), so the argument grammar is verified without Chromium.
//
// Rules of the grammar
//   * `--flag=value` and `--flag value` are both accepted for our own options.
//   * Unknown `--switches` are handed to Chromium untouched, which is what makes
//     `make run RUN_ARGS="--enable-logging=stderr --v=1"` work.
//   * A bare argument that is not a URL is treated as a search query.

#ifndef BROWSERX_APP_CLI_H_
#define BROWSERX_APP_CLI_H_

#include <optional>
#include <string>
#include <vector>

#include "core/settings_schema.h"
#include "core/tor.h"

namespace browserx {
namespace app {

/// What main() should do after parsing.
enum class CliAction {
  kRun,            // start the browser
  kPrintHelp,
  kPrintVersion,
  kPrintFlagPlan,  // print the switch plan for one/all modes and exit
  kPrintProfile,   // print the detected hardware profile and exit
  kPrintCatalog,   // print the Chromium switch catalogue and exit
  kWriteBenchPage, // write the standalone benchmark page to a file and exit
  kListModes,
  kSmokeTest,      // headless start-up check (used by `make smoke`)
  kBenchmark,      // headless measurement run, prints telemetry and exits
  kDoctor,         // environment/capability report
};

struct LaunchOptions {
  CliAction action = CliAction::kRun;

  // Startup state.
  core::Mode mode = core::Mode::kUltra;
  bool mode_explicit = false;  // user passed --mode; beats the saved setting
  core::AutoPilotMode auto_pilot = core::AutoPilotMode::kAsk;
  bool auto_pilot_explicit = false;

  // Capability gates.
  bool advanced = false;
  bool lab = false;
  bool sandbox = true;
  bool single_process = false;
  bool frameless = false;
  bool headless = false;
  bool portable = false;
  bool allow_multiple = false;
  bool disable_gpu = false;
  bool reset_settings = false;

  // --- native features -----------------------------------------------------
  /// Route every request through a Tor process' SOCKS port. See core/tor.h for what this
  /// does *not* protect against - the warnings are shown before the first page loads.
  bool tor = false;
  int tor_port = core::kDefaultTorSocksPort;
  /// Unpacked extensions are loaded from <profile>/extensions unless --no-extensions.
  bool extensions = true;

  // Overrides.
  std::string profile_dir;
  std::string cache_dir;
  std::string downloads_dir;
  int renderer_limit = 0;      // 0 = let the mode decide
  int cache_mb = 0;            // 0 = let the mode decide
  double target_fps = 0.0;     // 0 = let the mode decide
  std::string gl_backend;
  std::string log_level;       // "trace"|"debug"|"info"|"warn"|"error"
  int benchmark_seconds = 8;   // --benchmark
  std::string flag_plan_modes;  // --flag-plan[=mode|all]
  std::string bench_page_path;  // --write-bench-page[=file]; empty means the default name

  // What to open. Both are *raw* user input ("example.com", "how to cook rice");
  // CefHost::Initialize() resolves them with the configured search template.
  std::string url;                     // may be empty
  std::vector<std::string> tabs;       // additional tabs to restore
  std::vector<std::string> passthrough;  // raw switches for Chromium
  std::vector<std::string> argv;       // full argv, for CefMainArgs
  std::string executable;
  std::string parse_error;             // non-empty => print usage and exit 2
};

/// Parses argv. Never throws. On a malformed option, `parse_error` is set and
/// the caller should print usage.
LaunchOptions ParseCli(int argc, const char* const* argv);

/// Full help text (also used by `make help`-style output).
std::string CliUsage(const char* program);
/// One-line version banner.
std::string VersionBanner();

/// True for switches that must be forwarded to Chromium verbatim.
bool IsChromiumSwitch(const std::string& name);

}  // namespace app
}  // namespace browserx

#endif  // BROWSERX_APP_CLI_H_
