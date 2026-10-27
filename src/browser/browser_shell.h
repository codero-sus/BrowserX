// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — the shell: owns windows, the runtime policy and the ticker.
//
// Responsibilities (and nothing else):
//   * create the first window once CEF is ready,
//   * keep the telemetry tick running at the interval the mode asks for,
//   * turn the memory policy's decisions into actions (trim caches, warn, sleep),
//   * apply Auto-Pilot mode switches,
//   * tell every window what the current numbers are.
//
// Keeping this file free of view code means the policy can be tested by driving
// a fake shell; the views only render what the shell reports.

#ifndef BROWSERX_BROWSER_BROWSER_SHELL_H_
#define BROWSERX_BROWSER_BROWSER_SHELL_H_

#include <memory>
#include <cstdint>
#include <string>
#include <vector>

#include "include/views/cef_browser_view.h"

#include <fstream>

#include "app/cef_host.h"
#include "app/metrics_sampler.h"
#include "browser/browser_window.h"
#include "core/auto_pilot.h"
#include "app/ai_controller.h"
#include "core/blocklist.h"
#include "core/memory_policy.h"
#include "core/request_guard.h"

namespace browserx {
namespace browser {

class BrowserShellImpl : public BrowserShell {
 public:
  explicit BrowserShellImpl(app::CefHost* host);
  ~BrowserShellImpl() override;

  // --- BrowserShell --------------------------------------------------------
  void OnContextInitialized() override;
  void OnShuttingDown() override;
  std::string SessionJson() const override;
  void ApplyMode(core::Mode mode) override { SetMode(mode); }

  // --- window management ---------------------------------------------------
  void OpenWindow(const std::string& url);
  /// Used by target=_blank / window.open(): opens a window (single-tab builds)
  /// or a new tab once the tab strip exists.
  void OpenInNewTabOrWindow(const std::string& url) { OpenWindow(url); }
  void OnWindowDestroyed(BrowserWindow* window);
  bool CanCloseWindow(BrowserWindow* window);
  void AdoptPopupBrowserView(CefRefPtr<CefBrowserView> popup_browser_view);
  int window_count() const { return static_cast<int>(windows_.size()); }

  // --- mode / auto-pilot ---------------------------------------------------
  void SetMode(core::Mode mode);
  void CycleMode(int delta);
  void CycleAutoPilot();
  void NoteFullscreenChanged(bool fullscreen);
  void NoteLoadState(BrowserWindow* window, bool loading);

  /// The plan the browser is running right now.
  const core::ModePlan& current_plan() const;
  /// The plan this machine would use for `mode` (same context as launch: the detected
  /// hardware profile plus the user's settings). Used by the UI to say what switching
  /// would change, so the menu and the modes page cannot describe it differently.
  core::ModePlan PlanFor(core::Mode mode) const;
  /// Records a finished navigation in history. Called once per load, so the visit count
  /// means "how many times this page was opened" rather than "how many events fired".
  void NoteVisit(BrowserWindow* window, const std::string& url, const std::string& title);
  /// Refines the title of a page already in history (no extra visit). Pages frequently
  /// report their real title after the load finished, and a history entry called
  /// "Loading…" would be useless.
  void NoteTitle(const std::string& url, const std::string& title);
  void NoteNavigationStarted(BrowserWindow* window, const std::string& url);

  // --- shield --------------------------------------------------------------
  bool shield_enabled() const { return shield_.enabled(); }
  void ToggleShield() { shield_.SetEnabled(!shield_.enabled()); }
  int blocked_request_count() const { return static_cast<int>(shield_.blocked_total()); }
  const core::Blocklist& shield() const { return shield_; }
  const core::Blocklist* blocklist() const override { return &shield_; }
  bool AiAction(const std::string& action, const std::string& payload,
                std::string* error) override;
  std::string AiStateJson() const override;
  const app::AiSession* ai_session() const override { return &ai_.session(); }
  /// Stores the visible text of the page the AI panel is about. Called from the window's
  /// console handler when the injected collector answers; never on its own.
  void NoteAiPageText(const std::string& url, const std::string& title,
                      std::vector<std::string> lines) {
    ai_.session().SetPage(url, title, std::move(lines));
  }
  /// Asks the visible page for its text by injecting the collector. The answer arrives
  /// through the console handler above, which is the only path that stores it.
  void RequestAiPageText(BrowserWindow* window);
  app::AiState ai_state() const override { return ai_.Snapshot(); }
  /// The resource policy of the mode running right now: the request guard reads it
  /// on every load, so it must be the live policy, not the one captured at launch.
  const core::RuntimePolicy& runtime_policy() const { return host_->plan().runtime; }
  /// Called by the request guard when a request is cancelled, so the shield's
  /// counters and the log describe what actually happened on the wire.
  void NoteBlockedRequest(const core::RequestOutcome& outcome, const std::string& host,
                          uint64_t estimated_bytes);

  // --- settings / appearance helpers used by the window --------------------
  const std::string& search_template() const;
  const std::string& home_page() const;
  bool frameless() const { return host_ != nullptr && host_->options().frameless; }
  CefSize initial_size() const;
  CefPoint NextWindowOrigin();

  void Quit();

  // --- telemetry -----------------------------------------------------------
  const app::MetricsSampler& metrics() const { return metrics_; }
  const core::MemoryPolicy& policy() const { return policy_; }
  const core::AutoPilot& auto_pilot() const { return auto_pilot_; }
  /// Runs one telemetry tick now (also used by the tests and the panels).
  void Tick();
  void set_tick_interval_override(int ms) { tick_interval_override_ms_ = ms; }

 private:
  void ScheduleTick();
  void ApplyPolicyDecisions(const core::MemoryDecision& decision);
  /// Writes profile/sessions/last-session.json (called on every tab change and
  /// on shutdown, so a crash costs at most one tab).
  void SaveSession();
  /// Reads the session file back; returns false when there is nothing to restore.
  bool LoadSession(std::string* json_out) const;
  void RefreshWindows();
  void SyncModeToWindows();
  core::MemorySnapshot BuildSnapshot() const;

  app::CefHost* host_ = nullptr;
  std::vector<CefRefPtr<BrowserWindow>> windows_;
  CefRefPtr<CefBrowserView> last_popup_view_;

  app::MetricsSampler metrics_;
  core::Blocklist shield_;
  app::AiController ai_;
  core::MemoryPolicy policy_;
  core::AutoPilot auto_pilot_;
  core::MemoryDecision last_decision_;

  std::string search_template_;
  std::string home_page_;
  std::string restored_session_json_;
  int ticks_since_session_save_ = 0;
  int tick_interval_override_ms_ = 0;
  int next_origin_step_ = 0;
  int seconds_since_last_action_ = 0;
  int load_count_ = 0;
  bool fullscreen_active_ = false;
  bool shutting_down_ = false;
};

}  // namespace browser
}  // namespace browserx

#endif  // BROWSERX_BROWSER_BROWSER_SHELL_H_
