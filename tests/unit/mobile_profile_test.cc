// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// Unit tests for the shared mode engine on memory-tight devices.
//
// This is the mobile edition's policy test: Android's `isLowRamDevice` maps onto
// `SystemProfile::low_ram_hint`, and the same mode engine that drives the desktop
// browser must then produce plans a phone can actually survive. Because both
// editions compile src/core, anything asserted here is true of both.

#include <algorithm>
#include <string>
#include <vector>

#include "core/mode_engine.h"
#include "test_harness.h"

namespace {

using browserx::core::AllModes;
using browserx::core::BatteryState;
using browserx::core::BuildPlan;
using browserx::core::DefaultModeFor;
using browserx::core::EngineContext;
using browserx::core::GetModeInfo;
using browserx::core::GpuTier;
using browserx::core::Mode;
using browserx::core::ModePlan;
using browserx::core::OS;
using browserx::core::RuntimeOverrides;
using browserx::core::SystemProfile;

/// A typical mid-range Android phone: 8 cores, 4 GB, flagged low-RAM by the OS.
SystemProfile AndroidPhone() {
  SystemProfile profile;
  profile.os = OS::kLinux;
  profile.os_name = "Android 14";
  profile.arch = "arm64-v8a";
  profile.cpu_logical = 8;
  profile.cpu_physical = 8;
  profile.total_ram_mb = 4096;
  profile.available_ram_mb = 1600;
  profile.low_ram_hint = true;  // ActivityManager.isLowRamDevice()
  profile.gpu_tier = GpuTier::kIntegrated;
  profile.has_accelerated_video = true;
  profile.battery = BatteryState::kDischarging;
  profile.battery_percent = 30;
  profile.display_hz = 120;
  return profile;
}

/// A cheap device where every megabyte matters.
SystemProfile TinyAndroidPhone() {
  SystemProfile profile = AndroidPhone();
  profile.total_ram_mb = 2048;
  profile.available_ram_mb = 700;
  profile.cpu_logical = 8;
  profile.cpu_physical = 4;
  return profile;
}

/// A plugged-in desktop that the OS does *not* flag as low-RAM.
SystemProfile Workstation() {
  SystemProfile profile;
  profile.os = OS::kLinux;
  profile.os_name = "Debian GNU/Linux 12";
  profile.arch = "x86_64";
  profile.cpu_logical = 32;
  profile.cpu_physical = 16;
  profile.total_ram_mb = 32768;
  profile.available_ram_mb = 24000;
  profile.gpu_tier = GpuTier::kDiscrete;
  profile.battery = BatteryState::kNotPresent;
  profile.display_hz = 144;
  return profile;
}

ModePlan PlanFor(Mode mode, const SystemProfile& profile, RuntimeOverrides overrides = {}) {
  EngineContext context;
  context.profile = &profile;
  context.overrides = overrides;
  return BuildPlan(mode, context);
}

int SwitchInt(const ModePlan& plan, const char* id) {
  if (!plan.boot.Has(id)) return -1;
  return std::stoi(plan.boot.ValueOf(id));
}

bool HasNoteContaining(const ModePlan& plan, const std::string& needle) {
  for (const std::string& note : plan.notes) {
    if (note.find(needle) != std::string::npos) return true;
  }
  return false;
}

bool HasWarningContaining(const ModePlan& plan, const std::string& needle) {
  for (const std::string& warning : plan.warnings) {
    if (warning.find(needle) != std::string::npos) return true;
  }
  return false;
}

TEST(MobileProfile, LowRamDeviceCapsEveryMode) {
  const SystemProfile phone = AndroidPhone();
  for (const browserx::core::ModeInfo& info : AllModes()) {
    const ModePlan plan = PlanFor(info.mode, phone);
    // 4 GB cannot host four renderers or a 512 MB HTTP cache, whatever the mode.
    if (info.mode != Mode::kLowRam) {
      EXPECT_LE(SwitchInt(plan, "renderer-process-limit"), 3);
    }
    EXPECT_LE(SwitchInt(plan, "num-raster-threads"), 2);
    EXPECT_LE(plan.runtime.cache_size_mb, 128);
    EXPECT_FALSE(plan.runtime.bfcache);
    EXPECT_LE(plan.runtime.sleep_after_seconds, 600);
    EXPECT_LE(plan.runtime.discard_after_seconds, 1800);
    EXPECT_GE(plan.runtime.sleep_after_seconds, 1);  // an idle tab still sleeps eventually
  }
}

TEST(MobileProfile, VeryLowRamDeviceCapsHarder) {
  const SystemProfile tiny = TinyAndroidPhone();
  for (const browserx::core::ModeInfo& info : AllModes()) {
    const ModePlan plan = PlanFor(info.mode, tiny);
    EXPECT_LE(SwitchInt(plan, "renderer-process-limit"), 3);
    EXPECT_LE(plan.runtime.cache_size_mb, 128);
  }
  // ULTRA on a 2 GB device must be closer to LOW RAM than to its desktop self.
  const ModePlan ultra = PlanFor(Mode::kUltra, tiny);
  EXPECT_LE(SwitchInt(ultra, "renderer-process-limit"), 2);
  EXPECT_LE(SwitchInt(ultra, "num-raster-threads"), 1);
  EXPECT_LE(ultra.runtime.cache_size_mb, 64);
}

TEST(MobileProfile, CapsAreExplainedToTheUser) {
  const ModePlan ultra = PlanFor(Mode::kUltra, AndroidPhone());
  EXPECT_TRUE(HasNoteContaining(ultra, "Low-RAM device"));
  EXPECT_TRUE(HasWarningContaining(ultra, "memory-constrained"));
  // LOW RAM mode *is* the low-RAM answer, so it must not claim to be capped.
  const ModePlan low = PlanFor(Mode::kLowRam, AndroidPhone());
  EXPECT_FALSE(HasNoteContaining(low, "Low-RAM device detected"));
}

TEST(MobileProfile, LowRamModeIsTheSmallestFootprint) {
  const SystemProfile phone = AndroidPhone();
  const ModePlan low = PlanFor(Mode::kLowRam, phone);
  const ModePlan ultra = PlanFor(Mode::kUltra, phone);

  EXPECT_TRUE(low.boot.Has("enable-low-end-device-mode"));
  EXPECT_TRUE(low.boot.Has("process-per-site"));
  EXPECT_TRUE(low.boot.Has("skia-font-cache-limit-mb"));
  EXPECT_TRUE(low.boot.Has("mse-video-buffer-size-limit-mb"));
  EXPECT_EQ(low.runtime.cache_size_mb, 48);
  EXPECT_NEAR(low.runtime.warn_pressure, 0.70, 0.001);
  EXPECT_NEAR(low.runtime.discard_pressure, 0.78, 0.001);
  EXPECT_EQ(low.runtime.sleep_after_seconds, 60);
  EXPECT_LT(low.estimated_memory_budget_mb, ultra.estimated_memory_budget_mb);
  // Never more renderers than LOW RAM claims.
  EXPECT_LE(SwitchInt(low, "renderer-process-limit"), 3);
}

TEST(MobileProfile, GamingKeepsItsCharacterEvenWhenCapped) {
  const ModePlan game = PlanFor(Mode::kGaming, AndroidPhone());
  // The point of GAME mode survives the caps...
  EXPECT_TRUE(game.runtime.keep_display_awake);
  EXPECT_FALSE(game.runtime.background_throttling);
  EXPECT_TRUE(game.boot.Has("disable-gpu-vsync"));
  EXPECT_TRUE(game.boot.Has("disable-frame-rate-limit"));
  EXPECT_TRUE(game.boot.Has("autoplay-policy"));
  EXPECT_EQ(game.runtime.metrics_interval_ms, 500);
  // ...but a 4 GB phone still must not keep 512 MB of HTTP cache.
  EXPECT_LE(game.runtime.cache_size_mb, 128);
}

TEST(MobileProfile, EcoTradesFramesAndMotionForBattery) {
  const ModePlan eco = PlanFor(Mode::kEco, AndroidPhone());
  EXPECT_NEAR(eco.runtime.target_fps, 30.0, 0.001);
  EXPECT_TRUE(eco.runtime.reduce_motion);
  EXPECT_TRUE(eco.runtime.background_throttling);
  EXPECT_EQ(eco.runtime.metrics_interval_ms, 3000);
  EXPECT_TRUE(eco.boot.Has("animation-duration-scale"));
  EXPECT_TRUE(eco.boot.Has("disable-v8-idle-tasks"));
  EXPECT_FALSE(eco.runtime.keep_display_awake);
}

TEST(MobileProfile, WorkstationPlansAreNotCapped) {
  const SystemProfile desktop = Workstation();
  const ModePlan ultra = PlanFor(Mode::kUltra, desktop);
  const ModePlan high = PlanFor(Mode::kHighPerformance, desktop);
  const ModePlan game = PlanFor(Mode::kGaming, desktop);

  // No low-RAM language, and the generous numbers stay generous.
  EXPECT_FALSE(HasNoteContaining(ultra, "Low-RAM device"));
  EXPECT_TRUE(ultra.warnings.empty());
  EXPECT_GE(SwitchInt(ultra, "renderer-process-limit"), 4);
  EXPECT_GE(ultra.runtime.cache_size_mb, 256);
  EXPECT_TRUE(ultra.runtime.bfcache);
  EXPECT_GE(high.runtime.cache_size_mb, 1024);
  // Desktop GAME mode never sleeps tabs, no matter how long the session lasts.
  EXPECT_EQ(game.runtime.sleep_after_seconds, 0);
  EXPECT_EQ(game.runtime.discard_after_seconds, 0);
}

TEST(MobileProfile, ExplicitLimitsAreHonouredUntilTheyBecomeDangerous) {
  RuntimeOverrides overrides;
  overrides.renderer_process_limit = 8;

  const ModePlan desktop = PlanFor(Mode::kUltra, Workstation(), overrides);
  EXPECT_EQ(SwitchInt(desktop, "renderer-process-limit"), 8);  // the user asked for 8

  const ModePlan phone = PlanFor(Mode::kUltra, AndroidPhone(), overrides);
  EXPECT_EQ(SwitchInt(phone, "renderer-process-limit"), 3);  // the device said no
  EXPECT_TRUE(HasWarningContaining(phone, "memory-constrained"));
}

TEST(MobileProfile, DefaultModeMatchesTheDevice) {
  // A low-RAM device on 30% battery is both memory- and power-constrained; the
  // battery rule wins because ECO still sleeps tabs, and the user can pick LOW RAM
  // in one tap if memory matters more.
  EXPECT_EQ(GetModeInfo(DefaultModeFor(AndroidPhone())).id, std::string("eco"));

  SystemProfile plugged_in = AndroidPhone();
  plugged_in.battery = BatteryState::kCharging;
  plugged_in.battery_percent = 80;
  EXPECT_EQ(GetModeInfo(DefaultModeFor(plugged_in)).id, std::string("low-ram"));
  EXPECT_EQ(GetModeInfo(DefaultModeFor(TinyAndroidPhone())).id, std::string("eco"));

  SystemProfile tiny_plugged = TinyAndroidPhone();
  tiny_plugged.battery = BatteryState::kFull;
  tiny_plugged.battery_percent = 100;
  EXPECT_EQ(GetModeInfo(DefaultModeFor(tiny_plugged)).id, std::string("low-ram"));

  std::string desktop_mode = GetModeInfo(DefaultModeFor(Workstation())).id;
  EXPECT_EQ(desktop_mode, std::string("high-performance"));

  SystemProfile low_battery = Workstation();
  low_battery.battery = BatteryState::kDischarging;
  low_battery.battery_percent = 20;
  EXPECT_EQ(GetModeInfo(DefaultModeFor(low_battery)).id, std::string("eco"));
}

}  // namespace
