// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — the CEF application: mode plan -> real Chromium command lines.
//
// Lifecycle of one launch:
//
//   main()                       parse argv, load settings, build the mode plan
//        |
//   OnBeforeCommandLineProcessing()   <-- boot switches (this is the only place
//                                          Chromium reads them from)
//        |
//   CefInitialize()
//        |
//   OnContextInitialized()       create the request context + first window
//        |
//   CefRunMessageLoop()          UI runs; telemetry + memory policy tick along
//        |
//   OnBeforeChildProcessLaunch() <-- per-child switches (renderer/GPU/utility)
//
// Anything the mode engine plans is applied here and nothing is applied behind
// its back, which is what makes `browserx --flag-plan` an honest proof
// artifact: the switches it prints are exactly the switches this file appends.

#ifndef BROWSERX_APP_CEF_HOST_H_
#define BROWSERX_APP_CEF_HOST_H_

#include "app/library_store.h"
#include <memory>
#include <string>
#include <vector>

#include "app/app_paths.h"
#include "app/cli.h"
#include "app/extensions_store.h"
#include "app/settings_store.h"
#include "app/tor_probe.h"
#include "app/ai_controller.h"
#include "core/blocklist.h"
#include "core/mode_engine.h"
#include "core/tor.h"
#include "core/system_profile.h"

// CEF types are forward-declared so this header stays CEF-include-free.
class CefCommandLine;

namespace browserx {

/// Implemented by the browser layer (src/browser): owns tabs, windows and the
/// per-launch runtime policy. The app layer never reaches into UI code directly.
class BrowserShell {
 public:
  virtual ~BrowserShell() = default;
  /// Called once the browser process and the request context are ready.
  virtual void OnContextInitialized() = 0;
  /// Called on the UI thread when the message loop is about to end.
  virtual void OnShuttingDown() = 0;
  /// Current session state as JSON, for the internal browserx:// pages.
  /// Empty when no window exists yet; the pages then say so.
  virtual std::string SessionJson() const { return std::string(); }
  /// Applies a mode chosen outside the app layer (an internal page requesting
  /// browserx://modes?set=..., the command palette). The shell owns the live
  /// policy, so it is the one that must do this.
  virtual void ApplyMode(core::Mode mode) { (void)mode; }
  /// The live tracker shield, so browserx://shield can print its real rule count rather
  /// than a number the page made up. Null before a shell exists.
  virtual const core::Blocklist* blocklist() const { return nullptr; }
  /// browserx://ai actions, applied on the UI thread (the scheme handler posts them here).
  /// `payload` is the request's JSON object. Returns false with `error` for a bad action.
  virtual bool AiAction(const std::string& action, const std::string& payload,
                        std::string* error) {
    (void)action;
    (void)payload;
    (void)error;
    return false;
  }
  /// The AI panel's state as JSON, for the page's poll. "{}" when there is no shell.
  virtual std::string AiStateJson() const { return "{}"; }
  /// The live AI session, for the page's read-only view. Null before a shell exists.
  /// The session owns the key; callers may read the config but not print the secret.
  virtual const app::AiSession* ai_session() const { return nullptr; }
  /// The same state the page's poll sees, for the server-rendered first paint.
  virtual app::AiState ai_state() const { return app::AiState{}; }
};

namespace app {

/// Owns everything that must live for the whole process lifetime.
class CefHost {
 public:
  CefHost();

  void Initialize(const LaunchOptions& options, const Paths& paths);

  /// What the user's own rule file contained (see BrowserShellImpl, which does the
  /// import). Kept here because the boolean is owned by process lifetime, not by a window.
  struct RuleImport {
    bool file_present = false;
    std::string file;
    core::Blocklist::ImportReport report;
  };

  const LaunchOptions& options() const { return options_; }
  const Paths& paths() const { return paths_; }
  SettingsStore& settings() { return settings_; }
  const SettingsStore& settings() const { return settings_; }

  /// History and bookmarks. Owned here because a profile has exactly one of each, and the
  /// internal pages, the window and the CLI all read the same instance.
  LibraryStore& library() { return library_; }
  const LibraryStore& library() const { return library_; }

  const core::SystemProfile& system_profile() const { return profile_; }
  const core::ModePlan& plan() const { return plan_; }
  core::ModePlan& mutable_plan() { return plan_; }
  core::Mode current_mode() const { return plan_.mode; }

  void SetShell(BrowserShell* shell) { shell_ = shell; }
  BrowserShell* shell() const { return shell_; }

  /// Rebuilds the plan for `mode` (used by Auto-Pilot and the mode switcher).
  /// Returns true when the boot switch set changed, i.e. a restart is needed.
  bool ReplanFor(core::Mode mode, std::string* summary);

  // --- switch application (called from the CEF callbacks) -------------------
  void ApplyBootSwitches(CefCommandLine* command_line);
  void ApplyChildSwitches(CefCommandLine* command_line);

  const std::vector<std::string>& applied_boot_flags() const { return applied_boot_flags_; }
  const std::vector<std::string>& applied_child_flags() const { return applied_child_flags_; }
  bool restart_pending() const { return restart_pending_; }
  const std::string& restart_reason() const { return restart_reason_; }

  /// True when the running process must not start a second instance.
  bool IsPrimaryInstance() const { return primary_instance_; }
  void set_primary_instance(bool value) { primary_instance_ = value; }

 private:
  std::vector<std::string> BuildBootFlags() const;
  std::vector<std::string> BuildChildFlags() const;
  void ApplyPassthroughFlags();

  LaunchOptions options_;
  Paths paths_;
  SettingsStore settings_;
  LibraryStore library_;
  core::SystemProfile profile_;
  core::ModePlan plan_;
  BrowserShell* shell_ = nullptr;

  std::vector<std::string> applied_boot_flags_;   // incl. --enable-features=...
  std::vector<std::string> applied_child_flags_;
  std::vector<std::string> passthrough_;
  bool restart_pending_ = false;
  std::string restart_reason_;
  bool primary_instance_ = false;

  // --- native features -------------------------------------------------------
  // Computed once in Initialize(), because both answers are needed at start-up only and
  // both can touch the outside world (a socket, a directory). Caching them also means the
  // UI, the boot flags and the pages report the same result rather than three probes that
  // could disagree.
  core::TorPlan tor_plan_;
  bool tor_probe_done_ = false;
  TorProbeResult tor_probe_;
  ExtensionScan extension_scan_;
  bool extension_scan_done_ = false;
  RuleImport rule_import_;

 public:
  /// Tor mode: the plan that was applied, and what the probe found. Valid only when
  /// options().tor is true; the probe runs once, at start-up.
  const core::TorPlan& tor_plan() const { return tor_plan_; }
  const TorProbeResult& tor_probe() const { return tor_probe_; }
  const ExtensionScan& extension_scan() const { return extension_scan_; }

  const RuleImport& rule_import() const { return rule_import_; }
  void set_rule_import(RuleImport value) { rule_import_ = std::move(value); }
};

/// Process-wide singleton: CEF's C API is one-per-process by design.
CefHost& Host();

/// "--name=value" / "--name" helpers, so every reporter can print exactly what
/// the browser applies.
std::string FormatSwitch(const std::string& name, const std::string& value);
std::string FormatSwitch(const std::string& name);
/// Splits "--a=b" into name/value ("a", "b"); "--a" gives ("a", "").
bool SplitFlag(const std::string& flag, std::string* name, std::string* value);

/// Feature switches are merged into a single --enable-features / --disable-features
/// entry: Chromium only honours the last one it sees on the command line.
std::string JoinFeatureList(const std::vector<std::string>& features);

}  // namespace app

namespace browser {
/// Factory implemented by src/browser/browser_shell.cc.
std::unique_ptr<BrowserShell> CreateBrowserShell(app::CefHost* host);
}  // namespace browser

}  // namespace browserx

#endif  // BROWSERX_APP_CEF_HOST_H_
