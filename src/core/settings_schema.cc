// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/settings_schema.h"

#include <algorithm>
#include <set>

#include "core/format.h"
#include "core/mode_engine.h"
#include "core/url_utils.h"

namespace browserx {
namespace core {
namespace {

// --- optional binding helpers ------------------------------------------------
std::optional<bool> OptionalBool(const json::Value& value) {
  if (value.IsBool()) return value.AsBool();
  if (value.IsNumber()) return value.AsDouble() != 0.0;
  if (value.IsString()) {
    bool parsed = false;
    if (fmt::ParseBool(value.AsString(), &parsed)) return parsed;
  }
  return std::nullopt;
}

std::optional<int> OptionalInt(const json::Value& value, int min_value, int max_value) {
  if (!value.IsNumber() && !value.IsString()) return std::nullopt;
  int64_t parsed = 0;
  if (value.IsNumber()) {
    parsed = value.AsInt64();
  } else if (!fmt::ParseInt(value.AsString(), &parsed)) {
    return std::nullopt;
  }
  if (parsed < min_value) parsed = min_value;
  if (parsed > max_value) parsed = max_value;
  return static_cast<int>(parsed);
}

std::optional<double> OptionalDouble(const json::Value& value, double min_value, double max_value) {
  if (!value.IsNumber() && !value.IsString()) return std::nullopt;
  double parsed = 0.0;
  if (value.IsNumber()) {
    parsed = value.AsDouble();
  } else if (!fmt::ParseDouble(value.AsString(), &parsed)) {
    return std::nullopt;
  }
  if (parsed < min_value) parsed = min_value;
  if (parsed > max_value) parsed = max_value;
  return parsed;
}

void WriteOptionalBool(json::Value::Object& out, const char* key,
                       const std::optional<bool>& value) {
  if (value.has_value()) out[key] = json::Value(*value);
}

void WriteOptionalInt(json::Value::Object& out, const char* key,
                      const std::optional<int>& value) {
  if (value.has_value()) out[key] = json::Value(*value);
}

void WriteOptionalDouble(json::Value::Object& out, const char* key,
                         const std::optional<double>& value) {
  if (value.has_value()) out[key] = json::Value(*value);
}

/// Every mode id the engine actually defines.
///
/// This used to be a handwritten list, which silently dropped a per-mode override
/// whenever a mode was added ("ignore unknown modes" below turns a typo into a
/// no-op, so the list is the validation). It is derived from AllModes() now, and
/// one extra id is accepted: "default", the override that applies when no mode was
/// chosen explicitly.
const std::set<std::string>& KnownModeIds() {
  static const std::set<std::string> kIds = [] {
    std::set<std::string> ids;
    for (const ModeInfo& info : AllModes()) ids.insert(info.id);
    ids.insert("default");
    return ids;
  }();
  return kIds;
}

}  // namespace

const char* AutoPilotModeId(AutoPilotMode mode) {
  switch (mode) {
    case AutoPilotMode::kOff: return "off";
    case AutoPilotMode::kAsk: return "ask";
    case AutoPilotMode::kAuto: return "auto";
  }
  return "ask";
}

bool ParseAutoPilotMode(std::string_view text, AutoPilotMode* out) {
  const std::string value = fmt::ToLower(fmt::Trim(text));
  if (value == "off" || value == "disabled" || value == "no") {
    *out = AutoPilotMode::kOff;
    return true;
  }
  if (value == "ask" || value == "suggest" || value == "prompt") {
    *out = AutoPilotMode::kAsk;
    return true;
  }
  if (value == "auto" || value == "automatic" || value == "on" || value == "yes") {
    *out = AutoPilotMode::kAuto;
    return true;
  }
  return false;
}

Settings Settings::Defaults() {
  Settings settings;
  settings.schema_version = 1;
  settings.startup_mode = Mode::kUltra;
  settings.auto_pilot = AutoPilotMode::kAsk;
  settings.allow_advanced = true;
  settings.allow_lab = false;
  return settings;
}

json::Value OverridesToJson(const RuntimeOverrides& overrides) {
  json::Value::Object out;
  WriteOptionalBool(out, "background_throttling", overrides.background_throttling);
  WriteOptionalBool(out, "block_images", overrides.block_images);
  WriteOptionalBool(out, "block_media", overrides.block_media);
  WriteOptionalBool(out, "block_fonts", overrides.block_fonts);
  WriteOptionalBool(out, "block_third_party", overrides.block_third_party);
  WriteOptionalBool(out, "block_trackers", overrides.block_trackers);
  WriteOptionalBool(out, "bfcache", overrides.bfcache);
  WriteOptionalBool(out, "preconnect", overrides.preconnect);
  WriteOptionalBool(out, "code_cache", overrides.code_cache);
  WriteOptionalBool(out, "reduce_motion", overrides.reduce_motion);
  WriteOptionalBool(out, "show_perf_hud", overrides.show_perf_hud);
  WriteOptionalBool(out, "keep_display_awake", overrides.keep_display_awake);
  WriteOptionalBool(out, "always_on_top", overrides.always_on_top);
  WriteOptionalBool(out, "lazy_new_tabs", overrides.lazy_new_tabs);
  WriteOptionalInt(out, "sleep_after_seconds", overrides.sleep_after_seconds);
  WriteOptionalInt(out, "discard_after_seconds", overrides.discard_after_seconds);
  WriteOptionalInt(out, "metrics_interval_ms", overrides.metrics_interval_ms);
  WriteOptionalInt(out, "cache_size_mb", overrides.cache_size_mb);
  WriteOptionalInt(out, "renderer_process_limit", overrides.renderer_process_limit);
  WriteOptionalDouble(out, "target_fps", overrides.target_fps);
  WriteOptionalDouble(out, "warn_pressure", overrides.warn_pressure);
  WriteOptionalDouble(out, "discard_pressure", overrides.discard_pressure);
  return json::Value(std::move(out));
}

RuntimeOverrides OverridesFromJson(const json::Value& value) {
  RuntimeOverrides overrides;
  if (!value.IsObject()) return overrides;
  overrides.background_throttling = OptionalBool(value.Get("background_throttling"));
  overrides.block_images = OptionalBool(value.Get("block_images"));
  overrides.block_media = OptionalBool(value.Get("block_media"));
  overrides.block_fonts = OptionalBool(value.Get("block_fonts"));
  overrides.block_third_party = OptionalBool(value.Get("block_third_party"));
  overrides.block_trackers = OptionalBool(value.Get("block_trackers"));
  overrides.bfcache = OptionalBool(value.Get("bfcache"));
  overrides.preconnect = OptionalBool(value.Get("preconnect"));
  overrides.code_cache = OptionalBool(value.Get("code_cache"));
  overrides.reduce_motion = OptionalBool(value.Get("reduce_motion"));
  overrides.show_perf_hud = OptionalBool(value.Get("show_perf_hud"));
  overrides.keep_display_awake = OptionalBool(value.Get("keep_display_awake"));
  overrides.always_on_top = OptionalBool(value.Get("always_on_top"));
  overrides.lazy_new_tabs = OptionalBool(value.Get("lazy_new_tabs"));
  overrides.sleep_after_seconds = OptionalInt(value.Get("sleep_after_seconds"), 0, 86400);
  overrides.discard_after_seconds = OptionalInt(value.Get("discard_after_seconds"), 0, 86400 * 7);
  overrides.metrics_interval_ms = OptionalInt(value.Get("metrics_interval_ms"), 250, 60000);
  overrides.cache_size_mb = OptionalInt(value.Get("cache_size_mb"), 16, 4096);
  overrides.renderer_process_limit = OptionalInt(value.Get("renderer_process_limit"), 1, 64);
  overrides.target_fps = OptionalDouble(value.Get("target_fps"), 0.0, 480.0);
  overrides.warn_pressure = OptionalDouble(value.Get("warn_pressure"), 0.4, 0.98);
  overrides.discard_pressure = OptionalDouble(value.Get("discard_pressure"), 0.4, 0.99);
  return overrides;
}

json::Value Settings::ToJson() const {
  json::Value::Object out;
  out["schema_version"] = json::Value(schema_version);
  out["startup_mode"] = json::Value(GetModeInfo(startup_mode).id);
  out["auto_pilot"] = json::Value(AutoPilotModeId(auto_pilot));
  out["allow_advanced"] = json::Value(allow_advanced);
  out["allow_lab"] = json::Value(allow_lab);
  out["applied_mode_at_launch"] = json::Value(applied_mode_at_launch);

  json::Value::Array applied;
  for (const std::string& flag : applied_flags_at_launch) applied.emplace_back(flag);
  out["applied_flags_at_launch"] = json::Value(std::move(applied));

  out["overrides"] = OverridesToJson(overrides);
  json::Value::Object per_mode_json;
  for (const auto& entry : per_mode) per_mode_json[entry.first] = OverridesToJson(entry.second);
  out["per_mode"] = json::Value(std::move(per_mode_json));

  out["restore_session"] = json::Value(restore_session);
  out["start_maximized"] = json::Value(start_maximized);
  out["confirm_close_with_multiple_tabs"] = json::Value(confirm_close_with_multiple_tabs);
  out["startup_url"] = json::Value(startup_url);
  out["search_template"] = json::Value(search_template);
  out["homepage"] = json::Value(homepage);
  out["shield_enabled"] = json::Value(shield_enabled);
  out["block_third_party_cookies"] = json::Value(block_third_party_cookies);
  out["block_known_trackers"] = json::Value(block_known_trackers);
  json::Value::Array allowlist;
  for (const std::string& host : shield_allowlist) allowlist.emplace_back(host);
  out["shield_allowlist"] = json::Value(std::move(allowlist));
  out["do_not_track_header"] = json::Value(do_not_track_header);
  out["enable_crash_reporter"] = json::Value(enable_crash_reporter);
  out["send_telemetry"] = json::Value(send_telemetry);
  out["theme"] = json::Value(theme);
  out["show_status_bar"] = json::Value(show_status_bar);
  out["show_mode_bar"] = json::Value(show_mode_bar);
  out["use_system_titlebar"] = json::Value(use_system_titlebar);
  out["show_bookmark_bar"] = json::Value(show_bookmark_bar);
  out["animations_enabled"] = json::Value(animations_enabled);
  out["gl_backend"] = json::Value(gl_backend);
  out["ignore_gpu_blocklist"] = json::Value(ignore_gpu_blocklist);
  out["vsync_disabled"] = json::Value(vsync_disabled);
  out["display_hz_override"] = json::Value(display_hz_override);
  out["download_dir"] = json::Value(download_dir);
  out["ask_where_to_save"] = json::Value(ask_where_to_save);
  out["history_limit"] = json::Value(history_limit);
  out["log_level"] = json::Value(log_level);
  out["verbose_startup_timing"] = json::Value(verbose_startup_timing);
  return json::Value(std::move(out));
}

bool Settings::FromJson(const json::Value& value, std::string* error) {
  if (!value.IsObject()) {
    if (error != nullptr) *error = "settings root must be a JSON object";
    return false;
  }
  const int version = value.Get("schema_version").AsInt(1);
  if (version > 1) {
    if (error != nullptr) {
      *error = "settings were written by a newer BrowserX (schema_version " +
               std::to_string(version) + "); refusing to guess";
    }
    return false;
  }

  // v1 is the first format; migration hook kept explicit for future bumps.
  schema_version = 1;

  Mode mode = Mode::kUltra;
  if (ParseMode(value.Get("startup_mode").AsString("ultra"), &mode)) startup_mode = mode;

  AutoPilotMode auto_pilot_mode = AutoPilotMode::kAsk;
  if (ParseAutoPilotMode(value.Get("auto_pilot").AsString("ask"), &auto_pilot_mode)) {
    auto_pilot = auto_pilot_mode;
  }

  if (const auto parsed = OptionalBool(value.Get("allow_advanced")); parsed.has_value()) {
    allow_advanced = *parsed;
  }
  if (const auto parsed = OptionalBool(value.Get("allow_lab")); parsed.has_value()) {
    allow_lab = *parsed;
  }

  overrides = OverridesFromJson(value.Get("overrides"));
  overrides.allow_advanced = allow_advanced;
  overrides.allow_lab = allow_lab;

  per_mode.clear();
  if (value.Get("per_mode").IsObject()) {
    for (const auto& entry : value.Get("per_mode").AsObject()) {
      if (KnownModeIds().count(entry.first) == 0) continue;  // ignore unknown modes
      per_mode[entry.first] = OverridesFromJson(entry.second);
    }
  }

  if (const auto parsed = OptionalBool(value.Get("restore_session")); parsed.has_value()) {
    restore_session = *parsed;
  }
  if (const auto parsed = OptionalBool(value.Get("start_maximized")); parsed.has_value()) {
    start_maximized = *parsed;
  }
  if (const auto parsed = OptionalBool(value.Get("confirm_close_with_multiple_tabs"));
      parsed.has_value()) {
    confirm_close_with_multiple_tabs = *parsed;
  }
  if (value.Get("startup_url").IsString()) startup_url = value.Get("startup_url").AsString();
  if (value.Get("search_template").IsString()) search_template = value.Get("search_template").AsString();
  if (value.Get("homepage").IsString()) homepage = value.Get("homepage").AsString();

  if (const auto parsed = OptionalBool(value.Get("shield_enabled")); parsed.has_value()) {
    shield_enabled = *parsed;
  }
  if (const auto parsed = OptionalBool(value.Get("block_third_party_cookies"));
      parsed.has_value()) {
    block_third_party_cookies = *parsed;
  }
  if (const auto parsed = OptionalBool(value.Get("block_known_trackers")); parsed.has_value()) {
    block_known_trackers = *parsed;
  }
  shield_allowlist.clear();
  for (const json::Value& host : value.Get("shield_allowlist").AsArray()) {
    if (host.IsString()) shield_allowlist.push_back(fmt::ToLower(host.AsString()));
  }
  if (const auto parsed = OptionalBool(value.Get("do_not_track_header")); parsed.has_value()) {
    do_not_track_header = *parsed;
  }
  if (const auto parsed = OptionalBool(value.Get("enable_crash_reporter")); parsed.has_value()) {
    enable_crash_reporter = *parsed;
  }
  if (const auto parsed = OptionalBool(value.Get("send_telemetry")); parsed.has_value()) {
    send_telemetry = *parsed;
  }

  if (value.Get("theme").IsString()) theme = fmt::ToLower(value.Get("theme").AsString());
  if (const auto parsed = OptionalBool(value.Get("show_status_bar")); parsed.has_value()) {
    show_status_bar = *parsed;
  }
  if (const auto parsed = OptionalBool(value.Get("show_mode_bar")); parsed.has_value()) {
    show_mode_bar = *parsed;
  }
  if (const auto parsed = OptionalBool(value.Get("use_system_titlebar")); parsed.has_value()) {
    use_system_titlebar = *parsed;
  }
  if (const auto parsed = OptionalBool(value.Get("show_bookmark_bar")); parsed.has_value()) {
    show_bookmark_bar = *parsed;
  }
  if (const auto parsed = OptionalBool(value.Get("animations_enabled")); parsed.has_value()) {
    animations_enabled = *parsed;
  }
  if (value.Get("gl_backend").IsString()) gl_backend = fmt::ToLower(value.Get("gl_backend").AsString());
  if (const auto parsed = OptionalBool(value.Get("ignore_gpu_blocklist")); parsed.has_value()) {
    ignore_gpu_blocklist = *parsed;
  }
  if (const auto parsed = OptionalBool(value.Get("vsync_disabled")); parsed.has_value()) {
    vsync_disabled = *parsed;
  }
  if (const auto parsed = OptionalInt(value.Get("display_hz_override"), 0, 1000);
      parsed.has_value()) {
    display_hz_override = *parsed;
  }
  if (value.Get("download_dir").IsString()) download_dir = value.Get("download_dir").AsString();
  if (const auto parsed = OptionalBool(value.Get("ask_where_to_save")); parsed.has_value()) {
    ask_where_to_save = *parsed;
  }
  if (const auto parsed = OptionalInt(value.Get("history_limit"), 0, 1000000); parsed.has_value()) {
    history_limit = *parsed;
  }
  if (value.Get("log_level").IsString()) log_level = fmt::ToLower(value.Get("log_level").AsString());
  if (const auto parsed = OptionalBool(value.Get("verbose_startup_timing")); parsed.has_value()) {
    verbose_startup_timing = *parsed;
  }

  const std::vector<std::string> problems = Validate();
  if (!problems.empty()) {
    if (error != nullptr) *error = problems.front();
    return false;
  }
  return true;
}

std::vector<std::string> Settings::Validate() const {
  std::vector<std::string> problems;
  if (search_template.find("%s") == std::string::npos && search_template.empty()) {
    problems.push_back("search_template must be a non-empty URL");
  }
  if (!url::HasScheme(search_template)) {
    problems.push_back("search_template must contain a scheme (https://...)");
  } else {
    const std::string scheme = url::SchemeOf(search_template);
    if (scheme != "http" && scheme != "https") {
      problems.push_back("search_template must use http or https (got '" + scheme + "')");
    }
  }
  if (!url::LooksLikeUrl(homepage) && !url::IsInternal(homepage)) {
    problems.push_back("homepage is not a valid URL");
  }
  if (!url::IsInternal(startup_url) && !url::LooksLikeUrl(startup_url)) {
    problems.push_back("startup_url is not a valid URL");
  }
  if (theme != "dark" && theme != "light" && theme != "system") {
    problems.push_back("theme must be dark, light or system");
  }
  if (history_limit < 0) problems.push_back("history_limit must not be negative");
  if (display_hz_override < 0 || display_hz_override > 1000) {
    problems.push_back("display_hz_override must be between 0 and 1000");
  }
  static const std::set<std::string> kBackends = {"", "default", "gl", "d3d11", "d3d9",
                                                  "vulkan", "swiftshader", "metal"};
  if (kBackends.count(gl_backend) == 0) {
    problems.push_back("gl_backend is not a known ANGLE backend");
  }
  return problems;
}

RuntimeOverrides Settings::OverridesFor(Mode mode) const {
  RuntimeOverrides merged = overrides;
  const std::string id = GetModeInfo(mode).id;
  const auto it = per_mode.find(id);
  if (it != per_mode.end()) {
    const RuntimeOverrides& specific = it->second;
    // Per-mode values win over global ones, field by field.
    auto take = [](auto& target, const auto& source) {
      if (source.has_value()) target = source;
    };
    take(merged.background_throttling, specific.background_throttling);
    take(merged.block_images, specific.block_images);
    take(merged.block_media, specific.block_media);
    take(merged.block_fonts, specific.block_fonts);
    take(merged.block_third_party, specific.block_third_party);
    take(merged.block_trackers, specific.block_trackers);
    take(merged.bfcache, specific.bfcache);
    take(merged.preconnect, specific.preconnect);
    take(merged.code_cache, specific.code_cache);
    take(merged.reduce_motion, specific.reduce_motion);
    take(merged.show_perf_hud, specific.show_perf_hud);
    take(merged.keep_display_awake, specific.keep_display_awake);
    take(merged.always_on_top, specific.always_on_top);
    take(merged.lazy_new_tabs, specific.lazy_new_tabs);
    take(merged.sleep_after_seconds, specific.sleep_after_seconds);
    take(merged.discard_after_seconds, specific.discard_after_seconds);
    take(merged.metrics_interval_ms, specific.metrics_interval_ms);
    take(merged.cache_size_mb, specific.cache_size_mb);
    take(merged.renderer_process_limit, specific.renderer_process_limit);
    take(merged.target_fps, specific.target_fps);
    take(merged.warn_pressure, specific.warn_pressure);
    take(merged.discard_pressure, specific.discard_pressure);
  }
  merged.allow_advanced = allow_advanced;
  merged.allow_lab = allow_lab;
  return merged;
}

EngineContext Settings::MakeContext(Mode mode, const SystemProfile* profile) const {
  EngineContext context;
  context.profile = profile;
  context.overrides = OverridesFor(mode);
  context.gl_backend = gl_backend;
  return context;
}

}  // namespace core
}  // namespace browserx
