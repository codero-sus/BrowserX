// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX mobile — JNI bridge to the shared performance core.
//
// Every function below is a thin translation between Java types and the C++
// core that the desktop edition also uses (../src/core). No policy lives here:
// if a rule is worth having, it is in the core, unit tested, and identical on
// both editions.
//
// JNI surface (keep in sync with PerformanceCore.kt):
//   nativeCoreVersion()                       -> String
//   nativeModeIds()                           -> String[]   stable ids, order = kModes
//   nativeModeLabels()                        -> String[]
//   nativeBuildPlanJson(modeId, profileJson,
//                       overridesJson, glBackend) -> String (plan as JSON)
//   nativeMemoryPolicyJson(modeId, profileJson,
//                          overridesJson)      -> String (thresholds/actions)
//   nativeAutoPilotTickJson(stateJson)        -> String (decision as JSON)
//   nativeResolveUrl(input, searchTemplate)   -> String
//   nativeShieldDecideJson(documentHost, requestHost, path, type)
//                                             -> String (block decision)
//
// All JSON is hand-built with core::json, so the Kotlin side can parse it with
// org.json without any extra dependency, and a mismatch is caught in review.

#include <jni.h>

#include <string>
#include <vector>

#include "core/auto_pilot.h"
#include "core/blocklist.h"
#include "core/json.h"
#include "core/memory_policy.h"
#include "core/mode_engine.h"
#include "core/settings_schema.h"
#include "core/system_profile.h"
#include "core/url_utils.h"

namespace {

using browserx::core::AutoPilot;
using browserx::core::AutoPilotDecision;
using browserx::core::AutoPilotInput;
using browserx::core::AutoPilotMode;
using browserx::core::BlockDecision;
using browserx::core::Blocklist;
using browserx::core::EngineContext;
using browserx::core::GpuTier;
using browserx::core::MemoryPolicy;
using browserx::core::MemorySnapshot;
using browserx::core::Mode;
using browserx::core::ModePlan;
using browserx::core::ResourceType;
using browserx::core::RuntimeOverrides;
using browserx::core::SystemProfile;

std::string JStringToString(JNIEnv* env, jstring value) {
  if (value == nullptr) return std::string();
  const char* chars = env->GetStringUTFChars(value, nullptr);
  std::string text = chars != nullptr ? chars : "";
  if (chars != nullptr) env->ReleaseStringUTFChars(value, chars);
  return text;
}

jstring StringToJString(JNIEnv* env, const std::string& text) {
  return env->NewStringUTF(text.c_str());
}

bool BoolOf(const browserx::core::json::Value& object, const char* key, bool fallback) {
  const browserx::core::json::Value& found = object.Get(key);
  return found.IsBool() ? found.AsBool() : fallback;
}

int IntOf(const browserx::core::json::Value& object, const char* key, int fallback) {
  const browserx::core::json::Value& found = object.Get(key);
  return found.IsNumber() ? found.AsInt(fallback) : fallback;
}

double DoubleOf(const browserx::core::json::Value& object, const char* key, double fallback) {
  const browserx::core::json::Value& found = object.Get(key);
  return found.IsNumber() ? found.AsDouble(fallback) : fallback;
}

std::string StringOf(const browserx::core::json::Value& object, const char* key,
                     const std::string& fallback) {
  const browserx::core::json::Value& found = object.Get(key);
  return found.IsString() ? found.AsString() : fallback;
}

/// Reads the subset of SystemProfile that exists on Android. The device values
/// come from ActivityManager/BatteryManager, not /proc, so the desktop detector
/// is not used here - only the struct is shared.
SystemProfile ProfileFromJson(const browserx::core::json::Value& object) {
  SystemProfile profile;
  profile.os = browserx::core::OS::kLinux;  // Android is Linux
  profile.os_name = StringOf(object, "osName", "Android");
  profile.arch = StringOf(object, "arch", "arm64-v8a");
  profile.cpu_model = StringOf(object, "cpuModel", "");
  profile.cpu_logical = IntOf(object, "cpuLogical", 8);
  profile.cpu_physical = IntOf(object, "cpuPhysical", profile.cpu_logical);
  profile.total_ram_mb = static_cast<uint64_t>(IntOf(object, "totalRamMb", 4096));
  profile.available_ram_mb = static_cast<uint64_t>(IntOf(object, "availableRamMb", 2048));
  profile.low_ram_hint = IntOf(object, "isLowRamDevice", 0) != 0;
  profile.low_shm = true;  // Android has no /dev/shm contract; never rely on it
  const std::string tier = StringOf(object, "gpuTier", "unknown");
  if (tier == "software") profile.gpu_tier = GpuTier::kSoftware;
  else if (tier == "integrated") profile.gpu_tier = GpuTier::kIntegrated;
  else if (tier == "discrete") profile.gpu_tier = GpuTier::kDiscrete;
  profile.has_accelerated_video = BoolOf(object, "hasAcceleratedVideo", true);
  profile.battery_percent = DoubleOf(object, "batteryPercent", -1.0);
  profile.battery_seconds_left = IntOf(object, "batterySecondsLeft", -1);
  const std::string battery = StringOf(object, "batteryState", "unknown");
  if (battery == "discharging") profile.battery = browserx::core::BatteryState::kDischarging;
  else if (battery == "charging") profile.battery = browserx::core::BatteryState::kCharging;
  else if (battery == "full") profile.battery = browserx::core::BatteryState::kFull;
  else if (battery == "absent") profile.battery = browserx::core::BatteryState::kNotPresent;
  profile.display_hz = IntOf(object, "refreshRateHz", 60);
  profile.display_scale_percent = IntOf(object, "densityPercent", 100);
  return profile;
}

/// Reads the override subset the mobile settings screen can change.
RuntimeOverrides OverridesFromJson(const browserx::core::json::Value& object) {
  RuntimeOverrides overrides;
  if (object.Get("cacheMb").IsNumber()) {
    overrides.cache_size_mb = static_cast<int>(object.Get("cacheMb").AsInt(0));
  }
  if (object.Get("rendererLimit").IsNumber()) {
    overrides.renderer_process_limit = static_cast<int>(object.Get("rendererLimit").AsInt(0));
  }
  if (object.Get("targetFps").IsNumber()) {
    overrides.target_fps = object.Get("targetFps").AsDouble(0.0);
  }
  overrides.allow_advanced = BoolOf(object, "allowAdvanced", true);
  overrides.allow_lab = BoolOf(object, "allowLab", false);
  return overrides;
}

Mode ModeFromId(const std::string& mode_id) {
  Mode mode = Mode::kUltra;
  browserx::core::ParseMode(mode_id, &mode);
  return mode;
}

std::string PlanToJson(const ModePlan& plan) {
  using browserx::core::json::Value;
  Value::Object root;
  root["mode"] = Value(browserx::core::GetModeInfo(plan.mode).id);
  root["label"] = Value(browserx::core::GetModeInfo(plan.mode).label);
  root["restartRequired"] = Value(plan.restart_required);
  root["memoryBudgetMb"] = Value(static_cast<int64_t>(plan.estimated_memory_budget_mb));

  Value::Array switches;
  for (const auto& entry : plan.boot.switches()) {
    Value::Object item;
    item["id"] = Value(entry.id);
    item["value"] = Value(entry.value);
    item["reason"] = Value(entry.reason);
    switches.emplace_back(item);
  }
  root["bootSwitches"] = Value(switches);

  Value::Array features;
  for (const std::string& name : plan.boot.enabled_features()) {
    features.emplace_back(Value(name));
  }
  root["enableFeatures"] = Value(features);

  Value::Array disabled;
  for (const std::string& name : plan.boot.disabled_features()) {
    disabled.emplace_back(Value(name));
  }
  root["disableFeatures"] = Value(disabled);

  Value::Object runtime;
  runtime["backgroundThrottling"] = Value(plan.runtime.background_throttling);
  runtime["targetFps"] = Value(plan.runtime.target_fps);
  runtime["blockImages"] = Value(plan.runtime.block_images);
  runtime["blockMedia"] = Value(plan.runtime.block_media);
  runtime["blockFonts"] = Value(plan.runtime.block_fonts);
  runtime["blockThirdParty"] = Value(plan.runtime.block_third_party);
  runtime["blockTrackers"] = Value(plan.runtime.block_trackers);
  runtime["sleepAfterSeconds"] = Value(static_cast<int64_t>(plan.runtime.sleep_after_seconds));
  runtime["discardAfterSeconds"] = Value(static_cast<int64_t>(plan.runtime.discard_after_seconds));
  runtime["warnPressure"] = Value(plan.runtime.warn_pressure);
  runtime["discardPressure"] = Value(plan.runtime.discard_pressure);
  runtime["keepDisplayAwake"] = Value(plan.runtime.keep_display_awake);
  runtime["metricsIntervalMs"] = Value(static_cast<int64_t>(plan.runtime.metrics_interval_ms));
  runtime["cacheSizeMb"] = Value(static_cast<int64_t>(plan.runtime.cache_size_mb));
  runtime["lazyNewTabs"] = Value(plan.runtime.lazy_new_tabs);
  runtime["reduceMotion"] = Value(plan.runtime.reduce_motion);
  root["runtime"] = Value(runtime);

  Value::Array notes;
  for (const std::string& note : plan.notes) notes.emplace_back(Value(note));
  root["notes"] = Value(notes);
  Value::Array warnings;
  for (const std::string& warning : plan.warnings) warnings.emplace_back(Value(warning));
  root["warnings"] = Value(warnings);
  return browserx::core::json::Serialize(Value(root), 0);
}

std::string DecisionToJson(const AutoPilotDecision& decision) {
  using browserx::core::json::Value;
  Value::Object root;
  root["change"] = Value(decision.change);
  root["askOnly"] = Value(decision.ask_only);
  root["targetMode"] = Value(browserx::core::GetModeInfo(decision.target).id);
  root["reason"] = Value(decision.reason);
  root["rule"] = Value(decision.rule);
  return browserx::core::json::Serialize(Value(root), 0);
}

}  // namespace

// ---------------------------------------------------------------------------
// Version / inventory
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT jstring JNICALL
Java_com_browserx_mobile_PerformanceCore_nativeCoreVersion(JNIEnv* env, jobject) {
  return StringToJString(env, "browserx-core/1");
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_browserx_mobile_PerformanceCore_nativeModeIds(JNIEnv* env, jobject) {
  const std::vector<browserx::core::ModeInfo>& modes = browserx::core::AllModes();
  jclass string_class = env->FindClass("java/lang/String");
  jobjectArray array = env->NewObjectArray(static_cast<jsize>(modes.size()), string_class, nullptr);
  for (jsize i = 0; i < static_cast<jsize>(modes.size()); ++i) {
    env->SetObjectArrayElement(array, i, StringToJString(env, modes[static_cast<size_t>(i)].id));
  }
  return array;
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_browserx_mobile_PerformanceCore_nativeModeLabels(JNIEnv* env, jobject) {
  const std::vector<browserx::core::ModeInfo>& modes = browserx::core::AllModes();
  jclass string_class = env->FindClass("java/lang/String");
  jobjectArray array = env->NewObjectArray(static_cast<jsize>(modes.size()), string_class, nullptr);
  for (jsize i = 0; i < static_cast<jsize>(modes.size()); ++i) {
    const browserx::core::ModeInfo& info = modes[static_cast<size_t>(i)];
    std::string label = std::string(info.label) + " - " + info.tagline;
    env->SetObjectArrayElement(array, i, StringToJString(env, label));
  }
  return array;
}

// ---------------------------------------------------------------------------
// Plans
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT jstring JNICALL
Java_com_browserx_mobile_PerformanceCore_nativeBuildPlanJson(JNIEnv* env, jobject, jstring mode_id,
                                                             jstring profile_json,
                                                             jstring overrides_json,
                                                             jstring gl_backend) {
  using browserx::core::json::Value;
  const std::string mode_text = JStringToString(env, mode_id);
  const std::string profile_text = JStringToString(env, profile_json);
  const std::string overrides_text = JStringToString(env, overrides_json);

  auto profile_value = browserx::core::json::Parse(profile_text);
  auto overrides_value = browserx::core::json::Parse(overrides_text);
  SystemProfile profile = ProfileFromJson(profile_value.has_value() ? *profile_value : Value());
  RuntimeOverrides overrides =
      OverridesFromJson(overrides_value.has_value() ? *overrides_value : Value());

  EngineContext context;
  context.profile = &profile;
  context.overrides = overrides;
  context.gl_backend = JStringToString(env, gl_backend);

  const ModePlan plan = browserx::core::BuildPlan(ModeFromId(mode_text), context);
  return StringToJString(env, PlanToJson(plan));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_browserx_mobile_PerformanceCore_nativeMemoryPolicyJson(JNIEnv* env, jclass,
                                                                jstring mode_id, jstring profile_json,
                                                                jstring overrides_json) {
  using browserx::core::json::Value;
  const std::string mode_text = JStringToString(env, mode_id);
  auto profile_value = browserx::core::json::Parse(JStringToString(env, profile_json));
  auto overrides_value = browserx::core::json::Parse(JStringToString(env, overrides_json));
  SystemProfile profile = ProfileFromJson(profile_value.has_value() ? *profile_value : Value());
  RuntimeOverrides overrides =
      OverridesFromJson(overrides_value.has_value() ? *overrides_value : Value());

  EngineContext context;
  context.profile = &profile;
  context.overrides = overrides;
  const ModePlan plan = browserx::core::BuildPlan(ModeFromId(mode_text), context);

  Value::Object root;
  root["warnPressure"] = Value(plan.runtime.warn_pressure);
  root["discardPressure"] = Value(plan.runtime.discard_pressure);
  root["sleepAfterSeconds"] = Value(static_cast<int64_t>(plan.runtime.sleep_after_seconds));
  root["discardAfterSeconds"] = Value(static_cast<int64_t>(plan.runtime.discard_after_seconds));
  root["cacheSizeMb"] = Value(static_cast<int64_t>(plan.runtime.cache_size_mb));
  root["blockImages"] = Value(plan.runtime.block_images);
  root["blockMedia"] = Value(plan.runtime.block_media);
  root["blockThirdParty"] = Value(plan.runtime.block_third_party);
  root["memoryBudgetMb"] = Value(static_cast<int64_t>(plan.estimated_memory_budget_mb));
  return StringToJString(env, browserx::core::json::Serialize(Value(root), 0));
}

// ---------------------------------------------------------------------------
// Auto-Pilot / memory policy ticks
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT jstring JNICALL
Java_com_browserx_mobile_PerformanceCore_nativeMemoryEvaluateJson(JNIEnv* env, jclass,
                                                                 jstring mode_id, jstring profile_json,
                                                                 jstring overrides_json,
                                                                 jstring snapshot_json) {
  using browserx::core::json::Value;
  auto profile_value = browserx::core::json::Parse(JStringToString(env, profile_json));
  auto overrides_value = browserx::core::json::Parse(JStringToString(env, overrides_json));
  auto snapshot_value = browserx::core::json::Parse(JStringToString(env, snapshot_json));
  SystemProfile profile = ProfileFromJson(profile_value.has_value() ? *profile_value : Value());
  RuntimeOverrides overrides =
      OverridesFromJson(overrides_value.has_value() ? *overrides_value : Value());
  const Value snapshot = snapshot_value.has_value() ? *snapshot_value : Value();

  EngineContext context;
  context.profile = &profile;
  context.overrides = overrides;
  const ModePlan plan = browserx::core::BuildPlan(ModeFromId(JStringToString(env, mode_id)), context);

  MemorySnapshot state;
  state.total_mb = static_cast<uint64_t>(IntOf(snapshot, "totalRamMb",
                                               static_cast<int>(profile.total_ram_mb)));
  state.available_mb = static_cast<uint64_t>(IntOf(snapshot, "availableRamMb",
                                                   static_cast<int>(profile.available_ram_mb)));
  state.browser_rss_mb = static_cast<uint64_t>(IntOf(snapshot, "browserRssMb", 0));
  state.largest_tab_mb = static_cast<uint64_t>(IntOf(snapshot, "largestTabMb", 0));
  state.tab_count = IntOf(snapshot, "openTabs", 0);
  state.hidden_tabs = IntOf(snapshot, "hiddenTabs", 0);
  state.sleeping_tabs = IntOf(snapshot, "sleepingTabs", 0);
  state.audible_tabs = IntOf(snapshot, "audibleTabs", 0);
  state.pinned_tabs = IntOf(snapshot, "pinnedTabs", 0);
  state.seconds_since_last_action = IntOf(snapshot, "secondsSinceLastAction", 1000000);

  // One policy object per process is what the desktop does; JNI has no member to
  // hang it on, so it is a function-local static keyed by nothing - the runtime
  // policy is refreshed on every tick instead (UpdatePolicy), which keeps the
  // cooldowns that stop repeat actions while following mode changes.
  static MemoryPolicy policy(plan.runtime);
  policy.UpdatePolicy(plan.runtime);
  const browserx::core::MemoryDecision decision = policy.Evaluate(state);

  Value::Object root;
  root["act"] = Value(decision.act);
  root["action"] = Value(browserx::core::MemoryActionLabel(decision.action));
  root["level"] = Value(static_cast<int64_t>(static_cast<int>(decision.level)));
  root["pressure"] = Value(decision.pressure);
  root["reason"] = Value(decision.reason);
  return StringToJString(env, browserx::core::json::Serialize(Value(root), 0));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_browserx_mobile_PerformanceCore_nativeAutoPilotTickJson(JNIEnv* env, jclass,
                                                                 jstring state_json) {
  using browserx::core::json::Value;
  auto parsed = browserx::core::json::Parse(JStringToString(env, state_json));
  const Value state = parsed.has_value() ? *parsed : Value();

  const std::string policy_text = StringOf(state, "policy", "auto");
  AutoPilotMode policy = AutoPilotMode::kAuto;
  browserx::core::ParseAutoPilotMode(policy_text, &policy);

  const std::string current_text = StringOf(state, "currentMode", "ultra");
  AutoPilot auto_pilot(ModeFromId(current_text), policy);

  AutoPilotInput input;
  input.on_battery = BoolOf(state, "onBattery", false);
  input.battery_percent = DoubleOf(state, "batteryPercent", -1.0);
  input.memory_pressure = DoubleOf(state, "memoryPressure", 0.0);
  input.fullscreen_page = BoolOf(state, "fullscreen", false);
  input.pointer_locked = BoolOf(state, "pointerLocked", false);
  input.gamepad_active = BoolOf(state, "gamepadActive", false);
  input.seconds_since_user_input = IntOf(state, "secondsSinceUserInput", 0);
  input.open_tabs = IntOf(state, "openTabs", 0);
  input.heavy_page_active = BoolOf(state, "heavyPageActive", false);

  return StringToJString(env, DecisionToJson(auto_pilot.Evaluate(input)));
}

// ---------------------------------------------------------------------------
// Omnibox + shield
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT jstring JNICALL
Java_com_browserx_mobile_PerformanceCore_nativeResolveUrl(JNIEnv* env, jobject, jstring input,
                                                          jstring search_template) {
  const std::string resolved = browserx::core::url::ResolveInput(
      JStringToString(env, input), JStringToString(env, search_template));
  return StringToJString(env, resolved);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_browserx_mobile_PerformanceCore_nativeShieldDecideJson(JNIEnv* env, jclass,
                                                                jstring document_host,
                                                                jstring request_host,
                                                                jstring request_path,
                                                                jstring type_name) {
  using browserx::core::json::Value;
  // The blocklist is rebuilt per call only because JNI has no cheap place to keep
  // it; the rules themselves are a few hundred entries and Kotlin caches the
  // returned decision per (host, path) pair.
  Blocklist shield;
  shield.AddDefaults();
  const std::string type_text = JStringToString(env, type_name);
  ResourceType type = ResourceType::kOther;
  if (type_text == "image") type = ResourceType::kImage;
  else if (type_text == "media") type = ResourceType::kMedia;
  else if (type_text == "font") type = ResourceType::kFont;
  else if (type_text == "script") type = ResourceType::kScript;
  else if (type_text == "stylesheet") type = ResourceType::kStylesheet;
  else if (type_text == "document") type = ResourceType::kDocument;

  const BlockDecision decision =
      shield.Decide(JStringToString(env, document_host), JStringToString(env, request_host),
                    JStringToString(env, request_path), type);

  Value::Object root;
  root["block"] = Value(decision == BlockDecision::kBlock);
  const char* name = "allow";
  if (decision == BlockDecision::kBlock) name = "block";
  else if (decision == BlockDecision::kAllowlisted) name = "allowlisted";
  else if (decision == BlockDecision::kDisabled) name = "disabled";
  root["decision"] = Value(name);
  return StringToJString(env, browserx::core::json::Serialize(Value(root), 0));
}
