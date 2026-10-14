// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/settings_schema.h"

#include "test_harness.h"

using namespace browserx::core;

TEST(SettingsSchema, DefaultsAreValidAndComplete) {
  const Settings settings = Settings::Defaults();
  EXPECT_TRUE(settings.Validate().empty());
  EXPECT_TRUE(settings.startup_mode == Mode::kUltra);
  EXPECT_TRUE(settings.auto_pilot == AutoPilotMode::kAsk);
  EXPECT_FALSE(settings.allow_lab) << "lab flags must be opt-in";
  EXPECT_TRUE(settings.shield_enabled);
  EXPECT_TRUE(settings.search_template.find("%s") != std::string::npos);
}

TEST(SettingsSchema, RoundTripsThroughJson) {
  Settings original = Settings::Defaults();
  original.startup_mode = Mode::kGaming;
  original.auto_pilot = AutoPilotMode::kAuto;
  original.overrides.cache_size_mb = 512;
  original.overrides.block_images = true;
  original.overrides.target_fps = 144.0;
  original.per_mode["low-ram"].renderer_process_limit = 2;
  original.per_mode["gaming"].show_perf_hud = true;
  original.shield_allowlist = {"example.com", "shop.example"};
  original.theme = "light";
  original.gl_backend = "vulkan";

  const std::string text = json::Serialize(original.ToJson(), 2);
  auto parsed = json::Parse(text);
  ASSERT_TRUE_OR_RETURN(parsed);

  Settings restored;
  std::string error;
  EXPECT_TRUE(restored.FromJson(*parsed, &error)) << error;

  EXPECT_TRUE(restored.startup_mode == Mode::kGaming);
  EXPECT_TRUE(restored.auto_pilot == AutoPilotMode::kAuto);
  EXPECT_EQ(restored.overrides.cache_size_mb.value_or(0), 512);
  EXPECT_TRUE(restored.overrides.block_images.value_or(false));
  EXPECT_NEAR(restored.overrides.target_fps.value_or(0.0), 144.0, 1e-9);
  EXPECT_EQ(restored.per_mode.at("low-ram").renderer_process_limit.value_or(0), 2);
  EXPECT_TRUE(restored.per_mode.at("gaming").show_perf_hud.value_or(false));
  EXPECT_EQ(restored.shield_allowlist.size(), 2u);
  EXPECT_EQ(restored.theme, std::string("light"));
  EXPECT_EQ(restored.gl_backend, std::string("vulkan"));
}

TEST(SettingsSchema, RejectsWrongRootType) {
  Settings settings;
  std::string error;
  EXPECT_FALSE(settings.FromJson(json::Value(json::Value::Array{}), &error));
  EXPECT_FALSE(error.empty());
}

TEST(SettingsSchema, RejectsNewerSchemaVersion) {
  auto value = Settings::Defaults().ToJson();
  value["schema_version"] = json::Value(99);
  Settings settings;
  std::string error;
  EXPECT_FALSE(settings.FromJson(value, &error));
  EXPECT_TRUE(error.find("newer") != std::string::npos);
}

TEST(SettingsSchema, RejectsDangerousValues) {
  auto value = Settings::Defaults().ToJson();
  value["search_template"] = json::Value("javascript:alert(1)");
  Settings settings;
  std::string error;
  EXPECT_FALSE(settings.FromJson(value, &error)) << "a javascript: search template is unusable";
  EXPECT_TRUE(error.find("search_template") != std::string::npos);
}

TEST(SettingsSchema, ClampsOutOfRangeNumbers) {
  auto value = Settings::Defaults().ToJson();
  json::Value overrides = value["overrides"];
  overrides["cache_size_mb"] = json::Value(-5000);
  overrides["metrics_interval_ms"] = json::Value(1);
  overrides["renderer_process_limit"] = json::Value(99999);
  value["overrides"] = overrides;

  Settings settings;
  std::string error;
  EXPECT_TRUE(settings.FromJson(value, &error)) << error;
  EXPECT_GE(settings.overrides.cache_size_mb.value_or(0), 16);
  EXPECT_GE(settings.overrides.metrics_interval_ms.value_or(0), 250);
  EXPECT_LE(settings.overrides.renderer_process_limit.value_or(0), 64);
}

TEST(SettingsSchema, IgnoresUnknownModesAndFields) {
  auto value = Settings::Defaults().ToJson();
  json::Value per_mode = value["per_mode"];
  per_mode["super-turbo"] = json::Value(json::Value::Object{});
  value["per_mode"] = per_mode;
  value["some_future_option"] = json::Value(true);
  value["startup_mode"] = json::Value("not-a-mode");

  Settings settings;
  std::string error;
  EXPECT_TRUE(settings.FromJson(value, &error)) << error;
  EXPECT_EQ(settings.per_mode.size(), 0u) << "unknown modes must be ignored";
  EXPECT_TRUE(settings.startup_mode == Mode::kUltra) << "invalid mode falls back to the default";
}

TEST(SettingsSchema, PerModeOverridesWinOverGlobal) {
  Settings settings = Settings::Defaults();
  settings.overrides.cache_size_mb = 256;
  settings.overrides.show_perf_hud = false;
  settings.per_mode["gaming"].cache_size_mb = 1024;
  settings.per_mode["gaming"].show_perf_hud = true;

  const RuntimeOverrides gaming = settings.OverridesFor(Mode::kGaming);
  EXPECT_EQ(gaming.cache_size_mb.value_or(0), 1024);
  EXPECT_TRUE(gaming.show_perf_hud.value_or(false));

  const RuntimeOverrides ultra = settings.OverridesFor(Mode::kUltra);
  EXPECT_EQ(ultra.cache_size_mb.value_or(0), 256);
  EXPECT_FALSE(ultra.show_perf_hud.value_or(true));
}

TEST(SettingsSchema, ContextCarriesConsentFlags) {
  Settings settings = Settings::Defaults();
  settings.allow_advanced = false;
  settings.allow_lab = true;
  settings.gl_backend = "vulkan";

  const EngineContext context = settings.MakeContext(Mode::kGaming, nullptr);
  EXPECT_FALSE(context.overrides.allow_advanced);
  EXPECT_TRUE(context.overrides.allow_lab);
  EXPECT_EQ(context.gl_backend, std::string("vulkan"));

  // The engine must respect "no advanced" even in GAME mode.
  const ModePlan plan = BuildPlan(Mode::kGaming, context);
  bool found_blocklist_flag = false;
  for (const PlannedSwitch& planned : plan.boot.switches()) {
    if (planned.id == "ignore-gpu-blocklist") found_blocklist_flag = true;
  }
  EXPECT_FALSE(found_blocklist_flag);
}

TEST(SettingsSchema, OverrideJsonHelpers) {
  RuntimeOverrides overrides;
  overrides.block_images = true;
  overrides.target_fps = 60.0;
  overrides.sleep_after_seconds = 0;

  const RuntimeOverrides restored = OverridesFromJson(OverridesToJson(overrides));
  EXPECT_TRUE(restored.block_images.value_or(false));
  EXPECT_NEAR(restored.target_fps.value_or(-1.0), 60.0, 1e-9);
  EXPECT_EQ(restored.sleep_after_seconds.value_or(-1), 0);
  EXPECT_FALSE(restored.block_media.has_value());

  // Garbage input must yield an empty set of overrides, not a crash.
  const RuntimeOverrides garbage = OverridesFromJson(json::Value("nonsense"));
  EXPECT_FALSE(garbage.block_images.has_value());
}
