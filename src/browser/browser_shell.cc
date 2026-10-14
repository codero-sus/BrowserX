// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "browser/browser_shell.h"

#include <algorithm>
#include <memory>
#include <string>

#include "include/cef_app.h"
#include "include/cef_request_context.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#include "core/format.h"
#include "core/log.h"
#include "core/report.h"

namespace browserx {
namespace browser {
namespace {

constexpr int kMinTickMs = 250;

/// Runs one shell tick on the UI thread and re-arms itself. Using the message
/// loop instead of a separate thread keeps every policy decision on the thread
/// that owns the browsers, and costs no context switches.
class TickTask : public CefTask {
 public:
  explicit TickTask(BrowserShellImpl* shell) : shell_(shell) {}
  void Execute() override;
 private:
  BrowserShellImpl* shell_ = nullptr;
  IMPLEMENT_REFCOUNTING(TickTask);
};

}  // namespace

BrowserShellImpl::BrowserShellImpl(app::CefHost* host)
    : host_(host),
      policy_(host != nullptr ? host->plan().runtime : core::RuntimePolicy{}),
      auto_pilot_(host != nullptr ? host->current_mode() : core::Mode::kUltra,
                  host != nullptr ? host->options().auto_pilot : core::AutoPilotMode::kAsk) {
  shield_.AddDefaults();
  const core::Settings& settings = host_->settings().settings();
  shield_.SetEnabled(settings.shield_enabled);
  search_template_ = settings.search_template;
  home_page_ = settings.homepage;
  metrics_.SetSystemProfile(host_->system_profile());
}

BrowserShellImpl::~BrowserShellImpl() = default;

// ---------------------------------------------------------------------------
// Start-up
// ---------------------------------------------------------------------------
void BrowserShellImpl::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();
  core::log::InfoF("shell", "context ready: %s",
                   core::DescribePlanOneLine(host_->plan()).c_str());

  for (const std::string& warning : host_->plan().warnings) {
    core::log::Warn("modes", warning);
  }

  SyncModeToWindows();
  // Restore first (unless the user asked for a specific page): a session file is
  // only used when it is present and the launch had no explicit URL.
  std::string restored;
  const bool has_explicit_url = !host_->options().url.empty();
  if (!has_explicit_url && LoadSession(&restored)) {
    restored_session_json_ = restored;
    core::log::InfoF("session", "restoring %d byte(s) of tab state", static_cast<int>(restored.size()));
  }
  OpenWindow(restored_session_json_.empty() && host_->options().url.empty() ? home_page_
                                                                           : host_->options().url);
  ScheduleTick();
}

void BrowserShellImpl::ScheduleTick() {
  const core::RuntimePolicy& policy = host_->plan().runtime;
  int interval = tick_interval_override_ms_ > 0 ? tick_interval_override_ms_
                                                : policy.metrics_interval_ms;
  interval = std::max(kMinTickMs, interval);
  CefPostDelayedTask(TID_UI, new TickTask(this), interval);
}

void TickTask::Execute() {
  if (shell_ != nullptr) shell_->Tick();
}

void BrowserShellImpl::OnShuttingDown() {
  shutting_down_ = true;
  core::log::Info("shell", "shutting down - flushing settings and session");
  SaveSession();  // tab state first: it is what the next launch shows
  host_->settings().SaveIfDirty();
}

// ---------------------------------------------------------------------------
// Session persistence
// ---------------------------------------------------------------------------
std::string BrowserShellImpl::SessionJson() const {
  // Same document SaveSession() writes, so browserx://session shows exactly what
  // the next launch would restore - not a pretty-printed approximation.
  for (const CefRefPtr<BrowserWindow>& window : windows_) {
    if (window.get() == nullptr) continue;
    return window->session().ToJson();
  }
  return std::string();
}

void BrowserShellImpl::SaveSession() {
  const std::string path = host_->paths().session_file();
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    core::log::WarnF("session", "cannot write %s", path.c_str());
    return;
  }
  // v1 keeps it simple and honest: the file is one tab session (the first
  // window). Multi-window sessions get an array wrapper in v2, and the format
  // version in the document already tells a future reader which is which.
  std::string document;
  for (const CefRefPtr<BrowserWindow>& window : windows_) {
    if (window.get() == nullptr) continue;
    document = window->session().ToJson();
    break;
  }
  out << document;
  if (!out.good()) {
    core::log::WarnF("session", "write failed for %s", path.c_str());
    return;
  }
  core::log::Debug("session", "saved tab state");
}

bool BrowserShellImpl::LoadSession(std::string* json_out) const {
  const std::string path = host_->paths().session_file();
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  const std::string text = buffer.str();
  if (text.empty()) return false;
  if (json_out != nullptr) *json_out = text;
  return true;
}

// ---------------------------------------------------------------------------
// Windows
// ---------------------------------------------------------------------------
void BrowserShellImpl::OpenWindow(const std::string& url) {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<BrowserWindow> window = new BrowserWindow(this);
  windows_.push_back(window);
  // Only the first window of a launch inherits the restored session.
  const std::string restored = restored_session_json_;
  restored_session_json_.clear();
  window->Create(url.empty() ? home_page_ : url, restored);
  core::log::InfoF("tabs", "opening window (%d open)", window_count());
}

void BrowserShellImpl::OnWindowDestroyed(BrowserWindow* window) {
  CEF_REQUIRE_UI_THREAD();
  windows_.erase(std::remove_if(windows_.begin(), windows_.end(),
                                [window](const CefRefPtr<BrowserWindow>& candidate) {
                                  return candidate.get() == window;
                                }),
                 windows_.end());
  core::log::InfoF("tabs", "window destroyed (%d open)", window_count());
  if (windows_.empty() && !shutting_down_) {
    core::log::Info("shell", "last window closed - quitting");
    Quit();
  }
}

bool BrowserShellImpl::CanCloseWindow(BrowserWindow* window) { return true; }

void BrowserShellImpl::AdoptPopupBrowserView(CefRefPtr<CefBrowserView> popup_browser_view) {
  // Popup windows are created by CEF Views with us as the delegate; we only keep
  // a reference so the shell can close them when a mode change requires it.
  last_popup_view_ = popup_browser_view;
}

CefSize BrowserShellImpl::initial_size() const {
  // Cascaded windows: each new window is offset a little, and the size respects
  // the mode (ECO/LOW RAM get a smaller default so the first paint is cheaper).
  const int width = 1280;
  const int height = 800;
  return CefSize(width, height);
}

CefPoint BrowserShellImpl::NextWindowOrigin() {
  const int step = (next_origin_step_++ % 6) * 28;
  return CefPoint(60 + step, 60 + step);
}

void BrowserShellImpl::Quit() {
  shutting_down_ = true;
  host_->settings().SaveIfDirty();
  CefQuitMessageLoop();
}

// ---------------------------------------------------------------------------
// Modes and Auto-Pilot
// ---------------------------------------------------------------------------
void BrowserShellImpl::SetMode(core::Mode mode) {
  std::string summary;
  const bool restart = host_->ReplanFor(mode, &summary);
  const core::ModePlan& plan = host_->plan();
  policy_.UpdatePolicy(plan.runtime);
  auto_pilot_.SetCurrentMode(mode);
  auto_pilot_.NoteManualOverride();
  SyncModeToWindows();

  const std::string label = core::GetModeInfo(mode).label;
  for (const CefRefPtr<BrowserWindow>& window : windows_) {
    if (window.get() == nullptr) continue;
    window->UpdateModeIndicators(label, summary);
    window->Flash(restart ? label + " selected - restart BrowserX to apply the boot switches"
                          : label + " active");
  }
  core::log::InfoF("modes", "%s", summary.c_str());
}

void BrowserShellImpl::CycleMode(int delta) {
  const core::Mode current = auto_pilot_.current_mode();
  const core::Mode next = delta >= 0 ? core::NextMode(current) : core::PrevMode(current);
  SetMode(next);
}

void BrowserShellImpl::CycleAutoPilot() {
  core::Settings& settings = host_->settings().mutable_settings();
  switch (settings.auto_pilot) {
    case core::AutoPilotMode::kOff: settings.auto_pilot = core::AutoPilotMode::kAsk; break;
    case core::AutoPilotMode::kAsk: settings.auto_pilot = core::AutoPilotMode::kAuto; break;
    case core::AutoPilotMode::kAuto: settings.auto_pilot = core::AutoPilotMode::kOff; break;
  }
  host_->settings().MarkDirty();
  auto_pilot_.SetPolicy(settings.auto_pilot);
  const std::string label = core::AutoPilotModeId(settings.auto_pilot);
  for (const CefRefPtr<BrowserWindow>& window : windows_) {
    if (window.get() != nullptr) window->Flash("Auto-Pilot: " + label);
  }
  core::log::InfoF("modes", "auto-pilot set to %s", label.c_str());
}

void BrowserShellImpl::NoteFullscreenChanged(bool fullscreen) {
  fullscreen_active_ = fullscreen;
  core::log::InfoF("modes", "fullscreen %s", fullscreen ? "entered" : "left");
}

void BrowserShellImpl::NoteLoadState(BrowserWindow* window, bool loading) {
  if (loading) ++load_count_;
  // A page that is still loading after 10 seconds counts as heavy: that is what
  // Auto-Pilot uses to decide whether a game is running.
  if (!loading) seconds_since_last_action_ = 0;
}

void BrowserShellImpl::NoteNavigationStarted(BrowserWindow* window, const std::string& url) {
  seconds_since_last_action_ = 0;
}

// ---------------------------------------------------------------------------
// Telemetry tick
// ---------------------------------------------------------------------------
core::MemorySnapshot BrowserShellImpl::BuildSnapshot() const {
  core::MemorySnapshot snapshot;
  snapshot.total_mb = host_->system_profile().total_ram_mb;
  snapshot.available_mb = host_->system_profile().available_ram_mb;
  snapshot.tab_count = 0;
  snapshot.hidden_tabs = 0;
  snapshot.sleeping_tabs = 0;
  snapshot.seconds_since_last_action = seconds_since_last_action_;
  for (const CefRefPtr<BrowserWindow>& window : windows_) {
    if (window.get() == nullptr || window->browser().get() == nullptr) continue;
    ++snapshot.tab_count;
  }
  return snapshot;
}

void BrowserShellImpl::Tick() {
  if (shutting_down_) return;
  ++seconds_since_last_action_;

  const core::MemorySnapshot snapshot = BuildSnapshot();
  metrics_.set_shield_totals(shield_.bytes_saved_estimate(),
                            static_cast<int>(shield_.blocked_total()));
  metrics_.Tick(snapshot, core::GetModeInfo(auto_pilot_.current_mode()).id);

  const core::MemoryDecision decision = policy_.Evaluate(snapshot);
  last_decision_ = decision;
  if (decision.act) {
    ApplyPolicyDecisions(decision);
    policy_.NoteActionPerformed(decision.action, decision.reason);
  }

  // Auto-Pilot: ask or switch, never both.
  core::AutoPilotInput input;
  input.memory_pressure = decision.pressure;
  input.seconds_since_user_input = seconds_since_last_action_;
  input.open_tabs = snapshot.tab_count;
  input.fullscreen_page = fullscreen_active_;
  input.heavy_page_active = load_count_ > 0;
  input.on_battery = host_->system_profile().battery == core::BatteryState::kDischarging;
  input.battery_percent = host_->system_profile().battery_percent;
  const core::AutoPilotDecision autopilot = auto_pilot_.Evaluate(input);
  if (autopilot.change && !autopilot.ask_only) {
    auto_pilot_.NoteAutomaticSwitch(autopilot.rule);
    SetMode(autopilot.target);
  } else if (autopilot.ask_only) {
    for (const CefRefPtr<BrowserWindow>& window : windows_) {
      if (window.get() != nullptr) window->Flash(autopilot.reason);
    }
  }

  RefreshWindows();
  // Every ~30 s, and after any tab change (see OnTabsChanged hooks below), the
  // session file is rewritten. It is a few hundred bytes, so this is free.
  if (++ticks_since_session_save_ >= 30) {
    ticks_since_session_save_ = 0;
    SaveSession();
  }
  ScheduleTick();
}

void BrowserShellImpl::ApplyPolicyDecisions(const core::MemoryDecision& decision) {
  switch (decision.action) {
    case core::MemoryAction::kTrimCaches: {
      // The one action that actually gives memory back immediately: drop the
      // HTTP cache and code caches, then let Chromium rebuild them on demand.
      CefRefPtr<CefRequestContext> context = CefRequestContext::GetGlobalContext();
      if (context.get() != nullptr) context->ClearHttpCache(nullptr);
      core::log::InfoF("memory", "trimmed caches (%s)", decision.reason.c_str());
      break;
    }
    case core::MemoryAction::kWarnUser:
      for (const CefRefPtr<BrowserWindow>& window : windows_) {
        if (window.get() != nullptr) window->Flash(decision.reason);
      }
      core::log::WarnF("memory", "%s", decision.reason.c_str());
      break;
    case core::MemoryAction::kSleepHiddenTabs: {
      // A background tab that has not been looked at for five minutes is worth
      // more as disk state than as a renderer process. Five per pass, oldest
      // first, never pinned, audible or visible.
      constexpr int kIdleMs = 5 * 60 * 1000;
      int slept = 0;
      for (const CefRefPtr<BrowserWindow>& window : windows_) {
        if (window.get() != nullptr) slept += window->SleepIdleTabs(5, kIdleMs);
      }
      core::log::InfoF("memory", "%s (%d tab(s) slept)", decision.reason.c_str(), slept);
      break;
    }
    case core::MemoryAction::kDiscardLruTab: {
      int slept = 0;
      for (const CefRefPtr<BrowserWindow>& window : windows_) {
        if (window.get() != nullptr && window->DiscardLruTab()) ++slept;
      }
      core::log::InfoF("memory", "%s (%d tab(s) slept)", decision.reason.c_str(), slept);
      break;
    }
    case core::MemoryAction::kBlockNewHeavyTab:
      // Nothing to block yet: v1 has no media-heavy tab heuristic, so this is a
      // notice rather than a refusal (never block the user without a way out).
      for (const CefRefPtr<BrowserWindow>& window : windows_) {
        if (window.get() != nullptr) window->Flash(decision.reason);
      }
      core::log::InfoF("memory", "%s", decision.reason.c_str());
      break;
    case core::MemoryAction::kNone:
      break;
  }
}

void BrowserShellImpl::RefreshWindows() {
  const std::string right = core::report::RenderRuntimePolicySummary(host_->plan().runtime);
  const std::string left = metrics_.RenderOneLineStatus();
  for (const CefRefPtr<BrowserWindow>& window : windows_) {
    if (window.get() != nullptr) window->UpdateStatus(left, right);
  }
}

void BrowserShellImpl::SyncModeToWindows() {
  const core::Mode mode = auto_pilot_.current_mode();
  const core::ModeInfo& info = core::GetModeInfo(mode);
  for (const CefRefPtr<BrowserWindow>& window : windows_) {
    if (window.get() != nullptr) {
      window->UpdateModeIndicators(info.label, info.tagline);
    }
  }
}

const std::string& BrowserShellImpl::search_template() const { return search_template_; }
const std::string& BrowserShellImpl::home_page() const { return home_page_; }

// ---------------------------------------------------------------------------
// Factory used by the app layer
// ---------------------------------------------------------------------------
std::unique_ptr<BrowserShell> CreateBrowserShell(app::CefHost* host) {
  return std::make_unique<BrowserShellImpl>(host);
}

}  // namespace browser
}  // namespace browserx
