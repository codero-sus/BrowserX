// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — persistent settings: schema, defaults, validation, migration.
//
// Settings live in a JSON file (user profile). This module is pure so the file
// format can be unit-tested: broken or hand-edited files must never be able to
// crash the browser or silently switch on a dangerous option.

#ifndef BROWSERX_CORE_SETTINGS_SCHEMA_H_
#define BROWSERX_CORE_SETTINGS_SCHEMA_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/json.h"
#include "core/mode_engine.h"

namespace browserx {
namespace core {

/// How BrowserX decides to change modes on its own.
enum class AutoPilotMode { kOff, kAsk, kAuto };

const char* AutoPilotModeId(AutoPilotMode mode);
bool ParseAutoPilotMode(std::string_view text, AutoPilotMode* out);

struct Settings {
  /// Bump when the on-disk format changes; FromJson migrates older files.
  int schema_version = 1;

  // --- modes ---------------------------------------------------------------
  Mode startup_mode = Mode::kUltra;
  AutoPilotMode auto_pilot = AutoPilotMode::kAsk;
  /// Global runtime overrides applied on top of every mode.
  RuntimeOverrides overrides;
  /// Per-mode overrides, keyed by mode id ("gaming", "low-ram", ...).
  std::map<std::string, RuntimeOverrides> per_mode;
  bool allow_advanced = true;
  bool allow_lab = false;
  /// Remembers the switch plan that was applied at launch, so the UI can tell
  /// the user "restart to apply" accurately.
  std::vector<std::string> applied_flags_at_launch;
  std::string applied_mode_at_launch;

  // --- startup / session ---------------------------------------------------
  bool restore_session = true;
  bool start_maximized = false;
  bool confirm_close_with_multiple_tabs = false;
  std::string startup_url = "browserx://newtab";

  // --- navigation ----------------------------------------------------------
  std::string search_template = "https://duckduckgo.com/?q=%s";
  std::string homepage = "browserx://newtab";

  // --- privacy / shield ----------------------------------------------------
  bool shield_enabled = true;
  bool block_third_party_cookies = true;
  bool block_known_trackers = true;
  std::vector<std::string> shield_allowlist;  // hosts that skip the shield
  bool do_not_track_header = true;
  bool enable_crash_reporter = false;
  bool send_telemetry = false;  // BrowserX never sends anything; kept explicit

  // --- appearance ----------------------------------------------------------
  std::string theme = "dark";  // "dark" | "light" | "system"
  bool show_status_bar = true;
  bool show_mode_bar = true;
  bool use_system_titlebar = true;
  bool show_bookmark_bar = true;
  bool animations_enabled = true;

  // --- graphics ------------------------------------------------------------
  std::string gl_backend;      // "", "default", "gl", "d3d11", "vulkan", "swiftshader"
  bool ignore_gpu_blocklist = false;
  bool vsync_disabled = false;  // persists the GAME-mode choice across restarts
  int display_hz_override = 0;

  // --- downloads / storage -------------------------------------------------
  std::string download_dir;  // empty = OS default
  bool ask_where_to_save = false;
  int history_limit = 20000;

  // --- built-in AI ---------------------------------------------------------
  // Stored here because this is the one file the user already knows about, and because a
  // separate key file would be one more thing to lose. `ai_api_key` is plain text, and the
  // AI page says so: pretending otherwise with an obfuscation layer would be security
  // theatre on a file this browser writes itself.
  std::string ai_provider = "ollama";              // "cortex" | "ollama" | "cloud"
  std::string ai_endpoint;                        // empty = the provider's default
  std::string ai_model;                           // empty = the provider picks
  std::string ai_api_key;                         // cloud only
  double ai_temperature = 0.2;
  int ai_max_tokens = 512;
  bool ai_send_page_context = false;              // attach visible page text?
  int ai_max_context_chars = 4000;
  bool ai_stream = false;                         // stored for the next protocol change

  // --- diagnostics ---------------------------------------------------------
  std::string log_level = "info";
  bool verbose_startup_timing = false;

  static Settings Defaults();

  /// Serialises the whole object (stable key order for readable diffs).
  json::Value ToJson() const;
  /// Parses + validates + migrates. Returns false and fills `error` when the
  /// input is unusable (callers then fall back to Defaults()).
  bool FromJson(const json::Value& value, std::string* error);

  /// Human-readable problems (empty = valid).
  std::vector<std::string> Validate() const;

  /// Merged overrides for a specific mode (global + per-mode, per-mode wins).
  RuntimeOverrides OverridesFor(Mode mode) const;
  /// Effective EngineContext for building a plan for `mode`.
  EngineContext MakeContext(Mode mode, const SystemProfile* profile) const;
};

/// Parse/serialise the small per-mode override objects.
RuntimeOverrides OverridesFromJson(const json::Value& value);
json::Value OverridesToJson(const RuntimeOverrides& overrides);

}  // namespace core
}  // namespace browserx

#endif  // BROWSERX_CORE_SETTINGS_SCHEMA_H_
