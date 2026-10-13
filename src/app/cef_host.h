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

#include <memory>
#include <string>
#include <vector>

#include "app/app_paths.h"
#include "app/cli.h"
#include "app/settings_store.h"
#include "core/mode_engine.h"
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
};

namespace app {

/// Owns everything that must live for the whole process lifetime.
class CefHost {
 public:
  CefHost();

  void Initialize(const LaunchOptions& options, const Paths& paths);

  const LaunchOptions& options() const { return options_; }
  const Paths& paths() const { return paths_; }
  SettingsStore& settings() { return settings_; }
  const SettingsStore& settings() const { return settings_; }

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
  core::SystemProfile profile_;
  core::ModePlan plan_;
  BrowserShell* shell_ = nullptr;

  std::vector<std::string> applied_boot_flags_;   // incl. --enable-features=...
  std::vector<std::string> applied_child_flags_;
  std::vector<std::string> passthrough_;
  bool restart_pending_ = false;
  std::string restart_reason_;
  bool primary_instance_ = false;
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
