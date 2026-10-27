// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "app/cli.h"

#include <string>
#include <vector>

#include "core/format.h"
#include "core/log.h"
#include "core/settings_schema.h"
#include "core/url_utils.h"

#ifndef BROWSERX_VERSION
#define BROWSERX_VERSION "dev"
#endif

// Stamped by the build from git (tools/revision.py). "unknown" is the honest answer for a
// build made outside a checkout, and it is what a ZIP-folder build reports — never a guess.
#ifndef BROWSERX_GIT
#define BROWSERX_GIT "unknown"
#endif

namespace browserx {
namespace app {
namespace {

/// Options we own. Anything else that starts with "--" goes to Chromium.
const char* const kOwnSwitches[] = {
    "mode",         "auto-pilot",       "pilot",       "advanced", "lab",
    "sandbox",      "no-sandbox",       "portable",    "profile",  "cache-dir",
    "downloads",    "frameless",        "headless",    "renderers", "cache-mb",
    "fps",          "gl",               "single-process", "log-level", "reset-settings",
    "allow-multiple", "new-window",     "flag-plan",   "print-profile", "print-catalog",
    "write-bench-page",
    "list-modes",   "smoke-test",       "benchmark",   "doctor",   "help",
    "version",      "disable-gpu",      "tab",         "no-features-check",
    "tor",          "no-tor",           "tor-port",    "extensions",  "no-extensions",
    "extensions-dir",
};

bool IsOwnSwitch(const std::string& name) {
  for (const char* candidate : kOwnSwitches) {
    if (name == candidate) return true;
  }
  return false;
}

std::string StripDashes(const std::string& token) {
  size_t start = 0;
  while (start < token.size() && token[start] == '-') ++start;
  return token.substr(start);
}

/// Splits "--name=value" into name/value. `has_value` is false when no '='.
void SplitOption(const std::string& token, std::string* name, std::string* value, bool* has_value) {
  const size_t equals = token.find('=');
  if (equals == std::string::npos) {
    *name = StripDashes(token);
    value->clear();
    *has_value = false;
    return;
  }
  *name = StripDashes(token.substr(0, equals));
  *value = token.substr(equals + 1);
  *has_value = true;
}

bool ParseIntArg(const std::string& text, int64_t minimum, int64_t maximum, int64_t* out) {
  int64_t value = 0;
  if (!core::fmt::ParseInt(text, &value)) return false;
  if (value < minimum || value > maximum) return false;
  *out = value;
  return true;
}

}  // namespace

bool IsChromiumSwitch(const std::string& name) { return !IsOwnSwitch(name); }

std::string VersionBanner() {
#if defined(__x86_64__) || defined(_M_X64)
  const char* arch = "x86_64";
#elif defined(__aarch64__)
  const char* arch = "arm64";
#else
  const char* arch = "unknown-arch";
#endif
  std::string banner = "BrowserX ";
  banner += BROWSERX_VERSION;
  banner += " by codero-sus <https://the-codero.web.app>";
  // The commit, so "which build is this?" is answered by the binary itself rather than by
  // trusting that the folder it was copied from is still the folder it was built from.
  banner += " [";
  banner += BROWSERX_GIT;
  banner += "] (";
  banner += arch;
  banner += ", CEF/Chromium";
#if defined(NDEBUG)
  banner += ", release";
#else
  banner += ", debug";
#endif
  banner += ")";
  return banner;
}

std::string CliUsage(const char* program) {
  const std::string name = (program != nullptr && program[0] != '\0') ? program : "browserx";
  std::string text;
  text += VersionBanner() + "\n\n";
  text += "usage: " + name + " [options] [url | search terms]\n\n";
  text += "MODES\n";
  // Generated from the engine so a new mode can never be missing from --help.
  std::string mode_list;
  for (const browserx::core::ModeInfo& info : browserx::core::AllModes()) {
    if (!mode_list.empty()) mode_list += " | ";
    mode_list += info.id;
    if (info.mode == browserx::core::Mode::kUltra) mode_list += " (default)";
  }
  text += "  --mode=<id>          " + mode_list + "\n";
  text += "  --auto-pilot=<how>   auto | ask | off      (default: ask)\n";
  text += "  --list-modes         describe every mode and exit\n\n";
  text += "OPTIMISATION GATES\n";
  text += "  --advanced           allow the riskier-but-faster switches (off by default\n";
  text += "                       for low-ram, on for gaming/high-performance)\n";
  text += "  --lab                allow lab-only flags; implies --advanced\n";
  text += "  --renderers=<n>      cap renderer processes (overrides the mode)\n";
  text += "  --cache-mb=<n>       HTTP cache budget in MB\n";
  text += "  --fps=<n>            target frame rate (0 = uncapped where the mode allows)\n";
  text += "  --gl=<backend>       desktop | egl | swiftshader | default\n\n";
  text += "PROFILE & STORAGE\n";
  text += "  --portable           keep all data next to the executable\n";
  text += "  --profile=<dir>      override the profile directory\n";
  text += "  --cache-dir=<dir>    override the cache directory\n";
  text += "  --downloads=<dir>    override the default download directory\n";
  text += "  --reset-settings     ignore settings.json for this launch\n\n";
  text += "WINDOW\n";
  text += "  --frameless          hide OS window decorations (our own title bar)\n";
  text += "  --no-sandbox         disable the Chromium sandbox (development only!)\n";
  text += "  --headless           start without a window (smoke tests / CI)\n";
  text += "  --allow-multiple     allow a second instance to share the profile\n";
  text += "  --tab=<url>          open another tab (may be repeated)\n";
  text += "  --log-level=<level>  trace | debug | info | warn | error\n\n";
  text += "NATIVE FEATURES\n";
  text += "  --tor                route all traffic through a Tor SOCKS5 proxy\n";
  text += "                       (you run Tor; this browser does not bundle it)\n";
  text += "  --tor-port=<n>       SOCKS port to use (implies --tor, default 9050)\n";
  text += "  --no-tor             force Tor mode off for this run\n";
  text += "  --extensions         load unpacked extensions (default on)\n";
  text += "  --no-extensions      disable extension loading for this run\n";
  text += "  --extensions-dir=<d> search another folder for unpacked extensions\n";
  text += "                       (repeatable, or comma-separated)\n\n";
  text += "AI (configured on browserx://ai, Ctrl+Shift+A)\n";
  text += "  Cortex LLMHoster, Ollama, or any OpenAI-compatible cloud endpoint.\n";
  text += "  Local providers need no key. A cloud key is stored in settings.json.\n\n";
  text += "DIAGNOSTICS (print and exit)\n";
  text += "  --flag-plan[=mode|all]   resolved Chromium switches + why they are used\n";
  text += "  --print-profile          detected hardware and the mode we recommend\n";
  text += "  --print-catalog          every Chromium switch we know and its risk level\n";
  text += "  --write-bench-page[=f]   write the offline benchmark page (default\n";
  text += "                           browserx-bench.html) and exit\n";
  text += "  --doctor                 environment / capability report\n";
  text += "  --benchmark[=<sec>]      headless run that reports RAM and CPU cost\n";
  text += "  --smoke-test             start and exit immediately (exit code is the result)\n";
  text += "  --help, --version\n\n";
  text += "Anything else starting with -- is passed to Chromium unchanged, e.g.\n";
  text += "    " + name + " --enable-logging=stderr --v=1\n\n";
  text += "CREDITS\n";
  text += "  BrowserX by codero-sus\n";
  text += "    GitHub     https://github.com/codero-sus\n";
  text += "    Website    https://the-codero.web.app\n";
  text += "    Source     https://github.com/codero-sus/BrowserX\n";
  text += "    Licence    BrowserX Personal-Use License - personal, non-commercial\n";
  text += "               use only; no modification; no redistribution or resale.\n";
  text += "               Source-available, not open source. See LICENSE. The bundled\n";
  text += "               Chromium/CEF runtime stays under its own licences.\n";
  text += "  Made with Chromium and the Chromium Embedded Framework. See CREDITS.md.\n";
  return text;
}

LaunchOptions ParseCli(int argc, const char* const* argv) {
  LaunchOptions options;
  options.executable = (argc > 0 && argv[0] != nullptr) ? argv[0] : "browserx";
  for (int i = 0; i < argc; ++i) {
    if (argv[i] != nullptr) options.argv.emplace_back(argv[i]);
  }

  bool end_of_options = false;
  for (int i = 1; i < argc; ++i) {
    const std::string token = argv[i] != nullptr ? argv[i] : "";
    if (token.empty()) continue;

    if (end_of_options || token[0] != '-') {
      // A positional argument: URL or search terms. It is kept *raw* here and
      // resolved at launch with the user's configured search template (which the
      // CLI cannot know yet), so `browserx "my query"` respects settings.json.
      if (options.url.empty()) {
        options.url = token;
      } else {
        options.tabs.push_back(token);
      }
      continue;
    }

    if (token == "--") {
      end_of_options = true;
      continue;
    }

    std::string name;
    std::string value;
    bool has_value = false;
    SplitOption(token, &name, &value, &has_value);

    // Options that may take the next argv entry as their value.
    auto need_value = [&](const char* label) -> bool {
      if (has_value) return true;
      if (i + 1 >= argc || argv[i + 1] == nullptr) {
        options.parse_error = std::string("--") + label + " needs a value";
        return false;
      }
      const std::string next = argv[i + 1];
      int64_t numeric = 0;
      const bool looks_like_negative_number =
          !next.empty() && next[0] == '-' && core::fmt::ParseInt(next, &numeric);
      if (!next.empty() && next[0] == '-' && !looks_like_negative_number) {
        options.parse_error = std::string("--") + label + " needs a value";
        return false;
      }
      value = next;
      has_value = true;
      ++i;
      return true;
    };

    if (name == "help" || token == "-h") {
      options.action = CliAction::kPrintHelp;
      continue;
    }
    if (name == "version" || token == "-v") {
      options.action = CliAction::kPrintVersion;
      continue;
    }
    if (name == "list-modes") {
      options.action = CliAction::kListModes;
      continue;
    }
    if (name == "print-profile") {
      options.action = CliAction::kPrintProfile;
      continue;
    }
    if (name == "print-catalog") {
      options.action = CliAction::kPrintCatalog;
      continue;
    }
    if (name == "write-bench-page") {
      options.action = CliAction::kWriteBenchPage;
      if (has_value) {
        options.bench_page_path = value;
      } else if (i + 1 < argc && argv[i + 1] != nullptr && argv[i + 1][0] != '-') {
        options.bench_page_path = argv[i + 1];
        ++i;
      }
      continue;
    }
    if (name == "doctor") {
      options.action = CliAction::kDoctor;
      continue;
    }
    if (name == "smoke-test") {
      options.action = CliAction::kSmokeTest;
      continue;
    }
    if (name == "benchmark") {
      options.action = CliAction::kBenchmark;
      if (has_value) {
        int64_t seconds = 0;
        if (!ParseIntArg(value, 1, 3600, &seconds)) {
          options.parse_error = "--benchmark needs a number of seconds";
          continue;
        }
        options.benchmark_seconds = static_cast<int>(seconds);
      }
      continue;
    }
    if (name == "flag-plan") {
      options.action = CliAction::kPrintFlagPlan;
      if (has_value) {
        options.flag_plan_modes = value;
      } else if (i + 1 < argc && argv[i + 1] != nullptr && argv[i + 1][0] != '-') {
        // `--flag-plan gaming` / `--flag-plan all`
        const std::string next = argv[i + 1];
        core::Mode probe{};
        if (next == "all" || core::ParseMode(next, &probe)) {
          options.flag_plan_modes = next;
          ++i;
        }
      }
      continue;
    }

    if (name == "mode") {
      if (!need_value("mode")) continue;
      core::Mode parsed{};
      if (!core::ParseMode(value, &parsed)) {
        options.parse_error = "unknown mode '" + value + "'";
        continue;
      }
      options.mode = parsed;
      options.mode_explicit = true;
      continue;
    }
    if (name == "auto-pilot" || name == "pilot") {
      if (!need_value("auto-pilot")) continue;
      core::AutoPilotMode parsed{};
      if (!core::ParseAutoPilotMode(value, &parsed)) {
        options.parse_error = "unknown auto-pilot setting '" + value + "' (auto|ask|off)";
        continue;
      }
      options.auto_pilot = parsed;
      options.auto_pilot_explicit = true;
      continue;
    }
    if (name == "advanced") {
      options.advanced = true;
      continue;
    }
    if (name == "lab") {
      options.lab = true;
      options.advanced = true;
      continue;
    }
    if (name == "sandbox") {
      options.sandbox = true;
      continue;
    }
    if (name == "no-sandbox") {
      options.sandbox = false;
      continue;
    }
    if (name == "portable") {
      options.portable = true;
      continue;
    }
    if (name == "frameless") {
      options.frameless = true;
      continue;
    }
    if (name == "headless") {
      options.headless = true;
      continue;
    }
    if (name == "single-process") {
      options.single_process = true;
      continue;
    }
    if (name == "allow-multiple") {
      options.allow_multiple = true;
      continue;
    }
    if (name == "disable-gpu") {
      options.disable_gpu = true;
      continue;
    }
    if (name == "reset-settings") {
      options.reset_settings = true;
      continue;
    }
    if (name == "tor") {
      options.tor = true;
      continue;
    }
    if (name == "no-tor") {
      options.tor = false;
      continue;
    }
    if (name == "extensions") {
      options.extensions = true;
      continue;
    }
    if (name == "no-extensions") {
      options.extensions = false;
      continue;
    }
    if (name == "extensions-dir") {
      if (!need_value("extensions-dir")) continue;
      // Accept a list so a launcher can pass several folders in one argument; a trailing
      // separator is a common copy-paste artefact and is trimmed rather than passed on.
      for (std::string folder : browserx::core::fmt::Split(value, ',')) {
        folder = browserx::core::fmt::Trim(folder);
        while (!folder.empty() && (folder.back() == '/' || folder.back() == '\\')) {
          folder.pop_back();
        }
        if (!folder.empty()) options.extension_dirs.push_back(folder);
      }
      continue;
    }
    if (name == "tor-port") {
      if (!need_value("tor-port")) continue;
      int port = 0;
      if (!browserx::core::ParseTorPort(value, &port)) {
        // A port that is not a port would silently produce a proxy pointing at nothing,
        // which looks like "Tor is broken" rather than "the argument was wrong".
        options.parse_error = "--tor-port needs a port number between 1 and 65535, not '" +
                              value + "'";
        continue;
      }
      options.tor_port = port;
      options.tor = true;   // naming a port implies wanting Tor
      continue;
    }
    if (name == "profile") {
      if (!need_value("profile")) continue;
      options.profile_dir = value;
      continue;
    }
    if (name == "cache-dir") {
      if (!need_value("cache-dir")) continue;
      options.cache_dir = value;
      continue;
    }
    if (name == "downloads") {
      if (!need_value("downloads")) continue;
      options.downloads_dir = value;
      continue;
    }
    if (name == "renderers") {
      if (!need_value("renderers")) continue;
      int64_t parsed = 0;
      if (!ParseIntArg(value, 1, 256, &parsed)) {
        options.parse_error = "--renderers must be between 1 and 256";
        continue;
      }
      options.renderer_limit = static_cast<int>(parsed);
      continue;
    }
    if (name == "cache-mb") {
      if (!need_value("cache-mb")) continue;
      int64_t parsed = 0;
      if (!ParseIntArg(value, 0, 1024 * 1024, &parsed)) {
        options.parse_error = "--cache-mb must be a non-negative number";
        continue;
      }
      options.cache_mb = static_cast<int>(parsed);
      continue;
    }
    if (name == "fps") {
      if (!need_value("fps")) continue;
      int64_t parsed = 0;
      if (!ParseIntArg(value, 0, 1000, &parsed)) {
        options.parse_error = "--fps must be between 0 and 1000";
        continue;
      }
      options.target_fps = static_cast<double>(parsed);
      continue;
    }
    if (name == "gl") {
      if (!need_value("gl")) continue;
      options.gl_backend = value;
      continue;
    }
    if (name == "log-level") {
      if (!need_value("log-level")) continue;
      options.log_level = value;
      continue;
    }
    if (name == "tab") {
      if (!need_value("tab")) continue;
      options.tabs.push_back(value);  // resolved at launch, like positional input
      continue;
    }
    if (name == "new-window") {
      continue;  // accepted for compatibility; we always open a window
    }
    if (name == "no-features-check") {
      continue;  // developer switch: silences the plan/catalogue guard
    }

    // Unknown: hand it to Chromium verbatim, keeping "--name=value" intact.
    if (name.empty()) {
      options.parse_error = "malformed option '" + token + "'";
      continue;
    }
    options.passthrough.push_back(token);
  }

  if (options.lab) options.advanced = true;
  return options;
}

}  // namespace app
}  // namespace browserx
