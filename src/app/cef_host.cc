// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "app/cef_host.h"

#include <algorithm>
#include <iterator>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_scheme.h"

#include "app/app_paths.h"
#include "core/format.h"
#include "core/log.h"
#include "core/url_utils.h"
#include "web/internal_scheme.h"

namespace browserx {
namespace app {
namespace {

/// Where CEF keeps its .pak files and locales inside a distribution.
std::string ResourcesDir(const Paths& paths) {
  const std::string candidate = JoinPath(paths.executable_dir, "Resources");
  return DirectoryExists(candidate) ? candidate : std::string();
}

std::string LocalesDir(const Paths& paths) {
  const std::string resources = ResourcesDir(paths);
  if (resources.empty()) return std::string();
  const std::string candidate = JoinPath(resources, "locales");
  return DirectoryExists(candidate) ? candidate : std::string();
}

}  // namespace

// ---------------------------------------------------------------------------
// Flag helpers
// ---------------------------------------------------------------------------
std::string FormatSwitch(const std::string& name, const std::string& value) {
  if (value.empty()) return "--" + name;
  return "--" + name + "=" + value;
}

std::string FormatSwitch(const std::string& name) { return "--" + name; }

bool SplitFlag(const std::string& flag, std::string* name, std::string* value) {
  if (name == nullptr || value == nullptr) return false;
  std::string text = flag;
  while (!text.empty() && text[0] == '-') text.erase(0, 1);
  if (text.empty()) return false;
  const size_t equals = text.find('=');
  if (equals == std::string::npos) {
    *name = text;
    value->clear();
    return true;
  }
  *name = text.substr(0, equals);
  *value = text.substr(equals + 1);
  return true;
}

std::string JoinFeatureList(const std::vector<std::string>& features) {
  std::vector<std::string> unique;
  std::set<std::string> seen;
  for (const std::string& feature : features) {
    if (feature.empty()) continue;
    if (seen.insert(feature).second) unique.push_back(feature);
  }
  return core::fmt::Join(unique, ",");
}

// ---------------------------------------------------------------------------
// CefHost
// ---------------------------------------------------------------------------
CefHost::CefHost() = default;

void CefHost::Initialize(const LaunchOptions& options, const Paths& paths) {
  options_ = options;
  paths_ = paths;

  // 1. Settings: CLI beats settings.json beats defaults.
  if (options_.reset_settings) {
    core::log::Info("settings", "--reset-settings: ignoring the saved configuration");
    settings_.Load(std::string());  // no path => defaults
  } else {
    settings_.Load(paths_.settings_file());

  // History and bookmarks load after settings, so a future setting that caps the lists
  // can be applied before the files are read.
  library_.Load(paths_);
  core::log::InfoF("library", "loaded (%s)", library_.last_note().c_str());
  if (!library_.last_error().empty()) {
    core::log::WarnF("library", "%s", library_.last_error().c_str());
  }
  }
  core::Settings& values = settings_.mutable_settings();
  if (options_.mode_explicit) values.startup_mode = options_.mode;
  if (options_.auto_pilot_explicit) values.auto_pilot = options_.auto_pilot;
  if (!options_.log_level.empty()) {
    core::LogLevel level{};
    if (core::ParseLogLevel(options_.log_level, &level)) {
      core::Logger::Get().SetLevel(level);
    } else {
      core::log::WarnF("log", "unknown --log-level=%s, keeping default",
                       options_.log_level.c_str());
    }
  }

  // 2. Resolve the pages to open now that we know the user's search template.
  if (!options_.url.empty()) {
    options_.url = core::url::ResolveInput(options_.url, values.search_template);
  }
  for (std::string& tab : options_.tabs) {
    tab = core::url::ResolveInput(tab, values.search_template);
  }

  // 3. Hardware.
  profile_ = core::DetectSystemProfile();
  const core::Mode startup_mode = options_.mode_explicit ? options_.mode : values.startup_mode;
  values.startup_mode = startup_mode;
  if (!options_.auto_pilot_explicit) options_.auto_pilot = values.auto_pilot;

  // 4. Plan.
  core::EngineContext context = values.MakeContext(startup_mode, &profile_);
  context.overrides.allow_advanced = options_.advanced || context.overrides.allow_advanced;
  context.overrides.allow_lab = options_.lab;
  if (options_.renderer_limit > 0) context.overrides.renderer_process_limit = options_.renderer_limit;
  if (options_.cache_mb > 0) context.overrides.cache_size_mb = options_.cache_mb;
  if (options_.target_fps > 0) context.overrides.target_fps = options_.target_fps;
  if (!options_.gl_backend.empty()) context.gl_backend = options_.gl_backend;
  plan_ = core::BuildPlan(startup_mode, context);

  if (!options_.sandbox) {
    plan_.warnings.push_back(
        "Sandbox disabled: faster start-up, but every renderer runs unconfined. "
        "Use --no-sandbox for development only.");
  }

  // --- native features -----------------------------------------------------
  // Tor: build the switch set, then ask whether anything is on the port. The probe result
  // is only informational - the switches are applied either way, because falling back to a
  // direct connection when Tor is absent would be the one outcome this feature must never
  // produce silently.
  if (options_.tor) {
    tor_plan_ = core::MakeTorPlan(options_.tor_port);
    tor_probe_ = ProbeTor(tor_plan_.port);
    tor_probe_done_ = true;
    core::log::InfoF("tor", "port %d reachable=%d (%s)", tor_plan_.port,
                     tor_probe_.reachable ? 1 : 0, tor_probe_.detail.c_str());
    for (const std::string& warning : tor_plan_.warnings) {
      core::log::Warn("tor", warning);
    }
  }

  // Extensions: scanned once. A folder that does not exist is a normal first run.
  if (options_.extensions) {
    std::vector<std::string> roots =
        ExtensionsRoots(paths_.profile_dir, paths_.executable_dir);
    // --extensions-dir appends a root rather than replacing the profile: a switch that
    // silently stopped loading the user's own folder would be a trap.
    for (const std::string& extra : options_.extension_dirs) roots.push_back(extra);
    extension_scan_ = ScanExtensionRoots(roots);
    extension_scan_done_ = true;
  }

  core::log::InfoF("plan", "%s -> %zu boot switches, %zu child switches, cache %d MB",
                   core::DescribePlanOneLine(plan_).c_str(), plan_.boot.switches().size(),
                   plan_.child.switches().size(), plan_.runtime.cache_size_mb);
}

std::vector<std::string> CefHost::BuildBootFlags() const {
  std::vector<std::string> flags;

  // Switches planned by the mode engine, in a stable order: cache shaping first
  // (it is the one users notice), then everything else as planned.
  for (const core::PlannedSwitch& entry : plan_.boot.switches()) {
    flags.push_back(entry.ToFlag());
  }

  // Feature switches are merged because Chromium only reads the last one.
  const std::string enabled = JoinFeatureList(plan_.boot.enabled_features());
  const std::string disabled = JoinFeatureList(plan_.boot.disabled_features());
  if (!enabled.empty()) flags.push_back(FormatSwitch("enable-features", enabled));
  if (!disabled.empty()) flags.push_back(FormatSwitch("disable-features", disabled));

  // --- Tor ---------------------------------------------------------------
  // Appended after the mode's switches and before the sandbox note, so a mode can never
  // overwrite the proxy: these are the ones that decide whether traffic leaves this
  // machine directly, and they win.
  if (options_.tor) {
    for (const core::PlannedSwitch& entry : tor_plan().switches) {
      flags.push_back(entry.ToFlag());
    }
  }

  // --- unpacked extensions -----------------------------------------------
  // Both switches are needed: --load-extension adds ours, and --disable-extensions-except
  // stops Chromium from also loading whatever it remembers from a previous profile, which
  // would make the folder the user chose a suggestion rather than the list.
  if (options_.extensions && extension_scan_done_) {
    const std::string load = core::LoadExtensionValue(extension_scan_.extensions);
    if (!load.empty()) {
      flags.push_back(FormatSwitch("load-extension", load));
      flags.push_back(FormatSwitch("disable-extensions-except",
                                   core::DisableExceptValue(extension_scan_.extensions)));
    }
  }

  // Sandbox state is CefSettings' business, but the flag keeps the command line
  // self-documenting (and matches what `--flag-plan` prints).
  if (!options_.sandbox) flags.push_back(FormatSwitch("no-sandbox"));

  // Headless runs (smoke test / benchmark / CI) must never try to open a window.
  if (options_.headless) {
    flags.push_back(FormatSwitch("headless=new"));
    flags.push_back(FormatSwitch("disable-gpu-compositing"));
  }
  if (options_.disable_gpu) flags.push_back(FormatSwitch("disable-gpu"));

  return flags;
}

std::vector<std::string> CefHost::BuildChildFlags() const {
  std::vector<std::string> flags;
  for (const core::PlannedSwitch& entry : plan_.child.switches()) flags.push_back(entry.ToFlag());
  const std::string enabled = JoinFeatureList(plan_.child.enabled_features());
  const std::string disabled = JoinFeatureList(plan_.child.disabled_features());
  if (!enabled.empty()) flags.push_back(FormatSwitch("enable-features", enabled));
  if (!disabled.empty()) flags.push_back(FormatSwitch("disable-features", disabled));
  return flags;
}

void CefHost::ApplyBootSwitches(CefCommandLine* command_line) {
  if (command_line == nullptr) return;
  applied_boot_flags_.clear();

  const std::vector<std::string> flags = BuildBootFlags();
  for (const std::string& flag : flags) {
    std::string name;
    std::string value;
    if (!SplitFlag(flag, &name, &value)) continue;

    if (name == "enable-features" || name == "disable-features") {
      // Merge with anything the user passed on the command line.
      if (command_line->HasSwitch(name)) {
        const std::string existing = command_line->GetSwitchValue(name).ToString();
        const std::string merged = JoinFeatureList({existing, value});
        command_line->AppendSwitchWithValue(name, merged);
        applied_boot_flags_.push_back(FormatSwitch(name, merged));
      } else {
        command_line->AppendSwitchWithValue(name, value);
        applied_boot_flags_.push_back(flag);
      }
      continue;
    }

    // An explicit user switch always wins over the mode's default; the mode is a
    // suggestion, the command line is an instruction.
    if (command_line->HasSwitch(name)) continue;
    if (value.empty()) {
      command_line->AppendSwitch(name);
    } else {
      command_line->AppendSwitchWithValue(name, value);
    }
    applied_boot_flags_.push_back(flag);
  }

  // Pass-through switches (anything we did not own) are already on the command
  // line because CEF builds it from argv; nothing to do beyond logging them.
  core::log::InfoF("plan", "applied %zu boot switches (mode %s)", applied_boot_flags_.size(),
                   core::GetModeInfo(plan_.mode).id);
  core::log::Debug("plan", "command line: " + core::fmt::Join(applied_boot_flags_, " "));
}

void CefHost::ApplyChildSwitches(CefCommandLine* command_line) {
  if (command_line == nullptr) return;
  applied_child_flags_.clear();
  for (const std::string& flag : BuildChildFlags()) {
    std::string name;
    std::string value;
    if (!SplitFlag(flag, &name, &value)) continue;
    if (command_line->HasSwitch(name)) continue;
    if (value.empty()) {
      command_line->AppendSwitch(name);
    } else {
      command_line->AppendSwitchWithValue(name, value);
    }
    applied_child_flags_.push_back(flag);
  }
}

bool CefHost::ReplanFor(core::Mode mode, std::string* summary) {
  const core::EngineContext context =
      settings_.settings().MakeContext(mode, &profile_);
  core::ModePlan plan = core::BuildPlan(mode, context);
  const bool changed = plan.boot.ToSortedFlagList() != plan_.boot.ToSortedFlagList();
  plan.restart_required = changed;
  if (changed) {
    restart_pending_ = true;
    std::vector<std::string> added;
    std::vector<std::string> removed;
    const std::vector<std::string> before = plan_.boot.ToSortedFlagList();
    const std::vector<std::string> after = plan.boot.ToSortedFlagList();
    std::set_difference(after.begin(), after.end(), before.begin(), before.end(),
                        std::back_inserter(added));
    std::set_difference(before.begin(), before.end(), after.begin(), after.end(),
                        std::back_inserter(removed));
    restart_reason_ = "boot switches change: +" + std::to_string(added.size()) + " -" +
                      std::to_string(removed.size());
  } else {
    restart_pending_ = false;
    restart_reason_.clear();
  }
  plan_ = plan;
  if (summary != nullptr) {
    *summary = core::DescribePlanOneLine(plan_) +
               (changed ? " - restart required for the boot switches" : " - applied live");
  }
  return changed;
}

CefHost& Host() {
  static CefHost host;
  return host;
}

}  // namespace app
}  // namespace browserx

// ---------------------------------------------------------------------------
// The CEF application object
// ---------------------------------------------------------------------------
namespace {

/// CefApp + CefBrowserProcessHandler in one object: the browser process is the
/// only process that owns both.
class BrowserXApp : public CefApp, public CefBrowserProcessHandler {
 public:
  BrowserXApp() = default;

  // --- CefApp ---------------------------------------------------------------
  void OnBeforeCommandLineProcessing(const CefString& process_type,
                                     CefRefPtr<CefCommandLine> command_line) override {
    // Runs in every process *before* Chromium parses the command line: the only
    // place where boot switches can be injected.
    auto& host = browserx::app::Host();
    if (process_type.empty()) {
      host.ApplyBootSwitches(command_line.get());
    } else {
      host.ApplyChildSwitches(command_line.get());
    }
  }

  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override {
    // browserx:// is a real (standard, secure, fetch-capable, display-isolated)
    // scheme so our internal pages can use modules, fetch() and their own origin.
    const int options = CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE |
                        CEF_SCHEME_OPTION_FETCH_ENABLED | CEF_SCHEME_OPTION_DISPLAY_ISOLATED |
                        CEF_SCHEME_OPTION_CORS_ENABLED;
    registrar->AddCustomScheme("browserx", options);
  }

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }

  // --- CefBrowserProcessHandler --------------------------------------------
  void OnContextInitialized() override {
    auto& host = browserx::app::Host();
    // browserx:// must be servable before the first window loads it: the new-tab
    // page is where an empty session starts, so a missing handler would mean a
    // blank window on first run.
    if (!browserx::web::RegisterInternalSchemeHandler(&host)) {
      browserx::core::log::Warn("web", "browserx:// handler registration failed");
    }
    browserx::BrowserShell* shell = host.shell();
    if (shell != nullptr) shell->OnContextInitialized();
  }

  void OnBeforeChildProcessLaunch(CefRefPtr<CefCommandLine> command_line) override {
    browserx::app::Host().ApplyChildSwitches(command_line.get());
  }

 private:
  IMPLEMENT_REFCOUNTING(BrowserXApp);
};

}  // namespace

namespace browserx {
namespace app {

CefRefPtr<CefApp> CreateApplication() { return new BrowserXApp(); }

}  // namespace app
}  // namespace browserx
