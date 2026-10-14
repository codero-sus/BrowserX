// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — the mode engine.
//
// A "mode" is a complete, coherent performance profile. The engine turns
//   (mode, detected system profile, user overrides)
// into
//   (boot switches, child-process switches, live runtime policy, notes)
//
// The engine is pure: no I/O, no CEF, no globals. That makes every mode
// unit-testable, printable from a CLI (`make bench`), shown in the UI with the
// same data the browser actually applies, and diffable between releases.

#ifndef BROWSERX_CORE_MODE_ENGINE_H_
#define BROWSERX_CORE_MODE_ENGINE_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/system_profile.h"

namespace browserx {
namespace core {

enum class Mode {
  kUltra,           // balanced default
  kGaming,          // frame rate + input latency
  kHighPerformance, // maximum throughput (plugged in)
  kLowRam,          // smallest possible footprint
  kEco,             // battery life
  // --- specialist modes: one subsystem pushed as far as a browser can take it ---
  kSuperInternet,   // network throughput + connection latency
  kDownload,        // bulk transfers, nothing unloaded mid-transfer
  kStreaming,       // media playback: decode, no frame cap, screen awake
};

/// True for the modes that exist because the user asked for one specific job to
/// be done as well as a browser can do it (network, transfers, media). Auto-Pilot
/// will not switch away from these on its own: doing so would break the transfer
/// or the playback the user selected the mode for. It still escapes to LOW RAM on
/// a real memory emergency.
bool IsSpecialistMode(Mode mode);

struct ModeInfo {
  Mode mode;
  const char* id;        // stable identifier used in settings/CLI, e.g. "gaming"
  const char* label;     // UI label, e.g. "GAME"
  const char* icon;      // emoji/symbol for terminals and the status bar
  const char* tagline;   // one-liner for tooltips
  const char* detail;    // multi-sentence explanation for the settings panel
};

const std::vector<ModeInfo>& AllModes();
const ModeInfo& GetModeInfo(Mode mode);
bool ParseMode(std::string_view text, Mode* out);  // accepts id, label, or number
Mode NextMode(Mode mode);                          // cycling order used by the UI
Mode PrevMode(Mode mode);

// ---------------------------------------------------------------------------
// Switch plan
// ---------------------------------------------------------------------------
struct PlannedSwitch {
  std::string id;          // e.g. "disable-gpu-vsync"
  std::string value;       // empty for boolean switches
  std::string reason;      // why this mode applies it
  std::string provenance;  // "verified (ui/gl/gl_switches.cc)" etc.
  bool restart_required = true;  // boot switches need a restart to take effect

  std::string ToFlag() const;  // "--id" or "--id=value"
};

class SwitchPlan {
 public:
  void Add(std::string id, std::string value, std::string reason, bool child_only = false);
  void EnableFeature(std::string name, std::string reason);
  void DisableFeature(std::string name, std::string reason);
  void EnableFeatureIf(bool condition, std::string name, std::string reason);
  void AddIf(bool condition, std::string id, std::string value, std::string reason);

  bool Has(const std::string& id) const;
  std::string ValueOf(const std::string& id) const;
  bool Empty() const;
  bool ChildEmpty() const;
  /// Updates the value/reason of an already-planned switch (used when a setting
  /// overrides a mode default, so reports never drift from reality).
  bool SetSwitchValue(const std::string& id, std::string value, std::string reason);

  const std::vector<PlannedSwitch>& switches() const { return switches_; }
  const std::vector<PlannedSwitch>& child_switches() const { return child_switches_; }
  const std::vector<std::string>& enabled_features() const { return enabled_features_; }
  const std::vector<std::string>& disabled_features() const { return disabled_features_; }
  const std::vector<PlannedSwitch>& feature_reasons() const { return feature_reasons_; }

  /// One-line command line, e.g. "--disable-gpu-vsync --disk-cache-size=1234
  /// --enable-features=A,B"
  std::string ToCommandLine() const;
  /// Human-readable report with the reason and provenance for every entry.
  std::string ToReport() const;
  /// Stable, comparable summary (used by `make bench --diff`).
  std::vector<std::string> ToSortedFlagList() const;

 private:
  std::vector<PlannedSwitch> switches_;
  std::vector<PlannedSwitch> child_switches_;
  /// Resolves one feature's state (last write wins) and keeps a single reason.
  void SetFeatureState(const std::string& name, std::string reason, bool enabled);

  std::vector<std::string> enabled_features_;
  std::vector<std::string> disabled_features_;
  std::vector<PlannedSwitch> feature_reasons_;
};

// ---------------------------------------------------------------------------
// Live runtime policy (applied without restarting)
// ---------------------------------------------------------------------------
struct RuntimePolicy {
  /// Chromium-style background throttling for hidden/occluded tabs.
  bool background_throttling = true;
  /// Target frame rate hint for the compositor (0 = leave untouched).
  double target_fps = 0.0;
  /// Resource blocking (applied by the request interceptor).
  bool block_images = false;
  bool block_media = false;
  bool block_fonts = false;
  bool block_third_party = false;
  bool block_trackers = true;
  /// Tab lifecycle (seconds; 0 disables the action).
  int sleep_after_seconds = 1800;
  int discard_after_seconds = 3600;
  /// Memory pressure thresholds (0..1) used by the memory guard.
  double warn_pressure = 0.80;
  double discard_pressure = 0.88;
  /// Keep the screen awake (gaming / long-running work).
  bool keep_display_awake = false;
  /// Window stays on top (games in windowed mode).
  bool always_on_top = false;
  /// Telemetry sampling interval.
  int metrics_interval_ms = 1000;
  /// Session behaviour.
  bool bfcache = true;
  bool preconnect = false;
  bool code_cache = true;
  bool reduce_motion = false;
  /// UI behaviour.
  bool show_perf_hud = false;
  bool show_status_counters = true;
  /// HTTP cache budget in MB (also passed as a switch; kept here for the
  /// settings UI and runtime trimming).
  int cache_size_mb = 256;
  /// Shell integration.
  bool process_priority_boost = false;
  /// Load new tabs lazily (about:blank first, navigate on activation).
  bool lazy_new_tabs = true;
};

// ---------------------------------------------------------------------------
// Overrides coming from settings.json / CLI
// ---------------------------------------------------------------------------
struct RuntimeOverrides {
  std::optional<bool> background_throttling;
  std::optional<bool> block_images;
  std::optional<bool> block_media;
  std::optional<bool> block_fonts;
  std::optional<bool> block_third_party;
  std::optional<bool> block_trackers;
  std::optional<bool> bfcache;
  std::optional<bool> preconnect;
  std::optional<bool> code_cache;
  std::optional<bool> reduce_motion;
  std::optional<bool> show_perf_hud;
  std::optional<bool> keep_display_awake;
  std::optional<bool> always_on_top;
  std::optional<bool> lazy_new_tabs;

  std::optional<int> sleep_after_seconds;
  std::optional<int> discard_after_seconds;
  std::optional<int> metrics_interval_ms;
  std::optional<int> cache_size_mb;
  std::optional<int> renderer_process_limit;

  std::optional<double> target_fps;
  std::optional<double> warn_pressure;
  std::optional<double> discard_pressure;

  /// Consent gates for progressively riskier switches.
  bool allow_advanced = true;  // RUNTIME / UI-level advanced options
  bool allow_lab = false;      // lab-only flags (never on by default)
};

struct EngineContext {
  const SystemProfile* profile = nullptr;
  RuntimeOverrides overrides;
  /// User-selected ANGLE/GL backend (empty = leave Chromium's default).
  std::string gl_backend;
};

struct ModePlan {
  Mode mode = Mode::kUltra;
  SwitchPlan boot;   // applied before CefInitialize()
  SwitchPlan child;  // extra switches for child processes only
  RuntimePolicy runtime;
  /// True when the resolved boot switches differ from what is currently applied.
  bool restart_required = false;
  /// Human-readable rationale per mode, shown in the UI.
  std::vector<std::string> notes;
  /// Warnings the user should see (trade-offs, unmet requirements).
  std::vector<std::string> warnings;
  /// Estimated resident memory budget in MB for this mode+profile (0 = unknown).
  int estimated_memory_budget_mb = 0;

  std::string ToReport() const;
};

/// Build the plan for `mode` on `context.profile` with `context.overrides`.
ModePlan BuildPlan(Mode mode, const EngineContext& context);

/// Pick a sensible starting mode for a freshly detected machine.
Mode DefaultModeFor(const SystemProfile& profile);

/// Short label for logs/UI ("GAME", "LOW RAM", ...).
std::string DescribePlanOneLine(const ModePlan& plan);

}  // namespace core
}  // namespace browserx

#endif  // BROWSERX_CORE_MODE_ENGINE_H_
