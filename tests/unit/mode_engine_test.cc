// Mode-engine tests. The most important one is NoPlannedSwitchIsUnverified:
// it makes it impossible for a mode to ever apply an unproven flag.

#include "core/mode_engine.h"

#include <set>
#include <string>
#include <vector>

#include "core/chromium_catalog.h"
#include "test_harness.h"

using namespace mybrowser::core;

namespace {

SystemProfile MakeProfile(uint64_t ram_mb, int physical_cores, int logical_cores) {
  SystemProfile profile;
  profile.os = OS::kLinux;
  profile.os_name = "Test Linux";
  profile.arch = "x86_64";
  profile.cpu_physical = physical_cores;
  profile.cpu_logical = logical_cores;
  profile.total_ram_mb = ram_mb;
  profile.available_ram_mb = ram_mb / 2;
  profile.shm_total_mb = 1024;
  profile.battery = BatteryState::kNotPresent;
  profile.session = SessionType::kX11;
  profile.gpu_tier = GpuTier::kIntegrated;
  profile.low_ram_hint = ram_mb <= 4096;
  return profile;
}

EngineContext MakeContext(const SystemProfile& profile) {
  EngineContext context;
  context.profile = &profile;
  context.overrides.allow_advanced = false;  // deterministic by default
  return context;
}

bool PlanHas(const ModePlan& plan, const std::string& id) {
  for (const PlannedSwitch& entry : plan.boot.switches()) {
    if (entry.id == id) return true;
  }
  for (const PlannedSwitch& entry : plan.boot.child_switches()) {
    if (entry.id == id) return true;
  }
  return false;
}

bool PlanHasFeature(const ModePlan& plan, const std::string& name) {
  for (const std::string& feature : plan.boot.enabled_features()) {
    // features may carry "/value" parameters
    const size_t slash = feature.find('/');
    const std::string base = slash == std::string::npos ? feature : feature.substr(0, slash);
    if (base == name) return true;
  }
  return false;
}

}  // namespace

TEST(ModeEngine, ParseAndCycleModes) {
  Mode mode = Mode::kUltra;
  EXPECT_TRUE(ParseMode("gaming", &mode));
  EXPECT_TRUE(mode == Mode::kGaming);
  EXPECT_TRUE(ParseMode("GAME", &mode));
  EXPECT_TRUE(mode == Mode::kGaming);
  EXPECT_TRUE(ParseMode("low-ram", &mode));
  EXPECT_TRUE(mode == Mode::kLowRam);
  EXPECT_TRUE(ParseMode("LOW RAM", &mode));
  EXPECT_TRUE(mode == Mode::kLowRam);
  EXPECT_TRUE(ParseMode("high-performance", &mode));
  EXPECT_TRUE(mode == Mode::kHighPerformance);
  EXPECT_TRUE(ParseMode("3", &mode));
  EXPECT_TRUE(mode == Mode::kHighPerformance);
  EXPECT_TRUE(ParseMode("turbo", &mode));
  EXPECT_TRUE(mode == Mode::kHighPerformance);
  EXPECT_FALSE(ParseMode("nonsense", &mode));
  EXPECT_FALSE(ParseMode("9", &mode));

  // Cycling visits every mode exactly once before returning to the start.
  std::set<int> seen;
  Mode cursor = Mode::kUltra;
  for (size_t i = 0; i < AllModes().size(); ++i) {
    seen.insert(static_cast<int>(cursor));
    cursor = NextMode(cursor);
  }
  EXPECT_EQ(seen.size(), AllModes().size());
  EXPECT_TRUE(cursor == Mode::kUltra);
}

TEST(ModeEngine, NoPlannedSwitchIsUnverified) {
  const SystemProfile profile = MakeProfile(8192, 4, 8);
  for (const ModeInfo& info : AllModes()) {
    EngineContext context = MakeContext(profile);
    context.overrides.allow_lab = true;  // even the riskiest path must stay catalogued
    const ModePlan plan = BuildPlan(info.mode, context);
    for (const PlannedSwitch& planned : plan.boot.switches()) {
      const SwitchEntry* entry = FindSwitch(planned.id);
      EXPECT_TRUE(entry != nullptr) << info.id << " plans unknown switch --" << planned.id;
      if (entry != nullptr) {
        EXPECT_TRUE(IsApplicable(*entry))
            << info.id << " plans non-applicable switch --" << planned.id << " ("
            << ProvenanceLabel(entry->provenance) << ")";
        EXPECT_TRUE(entry->provenance != Provenance::kRemoved) << planned.id;
        if (entry->requires_value) {
          EXPECT_TRUE(!planned.value.empty()) << info.id << " plans --" << planned.id
                                             << " without a value";
        }
        EXPECT_TRUE(!planned.reason.empty()) << planned.id << " has no reason";
        EXPECT_TRUE(planned.provenance.find("unverified") == std::string::npos)
            << planned.id << " has unverified provenance";
      }
    }
    for (const std::string& feature : plan.boot.enabled_features()) {
      const size_t slash = feature.find('/');
      const std::string base = slash == std::string::npos ? feature : feature.substr(0, slash);
      const FeatureEntry* entry = FindFeature(base);
      EXPECT_TRUE(entry != nullptr) << info.id << " enables unknown feature " << base;
      if (entry != nullptr) {
        EXPECT_TRUE(entry->provenance != Provenance::kRemoved) << base << " is removed";
      }
    }
  }
}

TEST(ModeEngine, GamingModeTargetsFrameRateAndLatency) {
  const SystemProfile profile = MakeProfile(16384, 8, 16);
  EngineContext context = MakeContext(profile);
  const ModePlan plan = BuildPlan(Mode::kGaming, context);

  EXPECT_TRUE(PlanHas(plan, "disable-gpu-vsync"));
  EXPECT_TRUE(PlanHas(plan, "disable-frame-rate-limit"));
  EXPECT_TRUE(PlanHas(plan, "disable-background-timer-throttling"));
  EXPECT_TRUE(PlanHas(plan, "disable-renderer-backgrounding"));
  EXPECT_TRUE(PlanHas(plan, "disable-backgrounding-occluded-windows"));
  EXPECT_TRUE(PlanHas(plan, "disable-ipc-flooding-protection"));
  EXPECT_TRUE(PlanHas(plan, "audio-process-high-priority"));
  EXPECT_TRUE(PlanHas(plan, "autoplay-policy"));
  EXPECT_FALSE(plan.runtime.background_throttling);
  EXPECT_EQ(static_cast<int>(plan.runtime.target_fps), 0);
  EXPECT_TRUE(plan.runtime.keep_display_awake);
  EXPECT_EQ(plan.runtime.sleep_after_seconds, 0) << "games must never sleep the active tab";
  EXPECT_TRUE(plan.runtime.show_perf_hud);
  // Advanced-only flags must be absent when advanced options are off.
  EXPECT_FALSE(PlanHas(plan, "ignore-gpu-blocklist"));
  EXPECT_FALSE(PlanHas(plan, "disable-gpu-watchdog"));
}

TEST(ModeEngine, LowRamModeMinimisesFootprint) {
  const SystemProfile profile = MakeProfile(2048, 2, 4);
  EngineContext context = MakeContext(profile);
  const ModePlan plan = BuildPlan(Mode::kLowRam, context);

  EXPECT_TRUE(PlanHas(plan, "enable-low-end-device-mode"));
  EXPECT_TRUE(PlanHas(plan, "renderer-process-limit"));
  EXPECT_TRUE(PlanHas(plan, "process-per-site"));
  EXPECT_TRUE(PlanHas(plan, "disable-back-forward-cache"));
  EXPECT_TRUE(PlanHas(plan, "skia-font-cache-limit-mb"));
  EXPECT_TRUE(PlanHas(plan, "mse-video-buffer-size-limit-mb"));
  EXPECT_EQ(plan.boot.ValueOf("renderer-process-limit"), std::string("2"))
      << "a 2 GB machine must not get more than 2 renderers";
  EXPECT_TRUE(plan.runtime.cache_size_mb <= 64);
  EXPECT_TRUE(plan.runtime.sleep_after_seconds <= 120);
  EXPECT_TRUE(plan.runtime.discard_after_seconds <= 600);
  EXPECT_LT(plan.runtime.discard_pressure, 0.85);
  EXPECT_FALSE(plan.runtime.bfcache);
  // Advanced flags stay off unless the user opts in.
  EXPECT_FALSE(PlanHas(plan, "disable-site-isolation-trials"));
  EXPECT_FALSE(PlanHasFeature(plan, "NetworkServiceInProcess2"));
}

TEST(ModeEngine, LowRamAdvancedOptInAddsProcessSharing) {
  const SystemProfile profile = MakeProfile(4096, 4, 4);
  EngineContext context = MakeContext(profile);
  context.overrides.allow_advanced = true;
  const ModePlan plan = BuildPlan(Mode::kLowRam, context);
  EXPECT_TRUE(PlanHas(plan, "disable-site-isolation-trials"));
  EXPECT_TRUE(PlanHasFeature(plan, "NetworkServiceInProcess2"));
  EXPECT_TRUE(PlanHasFeature(plan, "ProcessPerSiteMainFrameThreshold"));
}

TEST(ModeEngine, HighPerformanceMode) {
  const SystemProfile profile = MakeProfile(32768, 16, 32);
  EngineContext context = MakeContext(profile);
  const ModePlan plan = BuildPlan(Mode::kHighPerformance, context);

  EXPECT_TRUE(PlanHas(plan, "disable-layer-tree-host-memory-pressure"));
  EXPECT_TRUE(PlanHas(plan, "enable-gpu-benchmarking"));
  EXPECT_TRUE(PlanHas(plan, "disable-low-end-device-mode"));
  EXPECT_TRUE(PlanHasFeature(plan, "AcceleratedVideoDecoder"));
  EXPECT_TRUE(PlanHasFeature(plan, "NetworkContextPrefetch"));
  EXPECT_FALSE(plan.runtime.background_throttling);
  EXPECT_EQ(plan.runtime.sleep_after_seconds, 0);
  EXPECT_TRUE(plan.runtime.cache_size_mb >= 512);
  EXPECT_FALSE(plan.runtime.lazy_new_tabs);
}

TEST(ModeEngine, EcoModeProtectsBattery) {
  const SystemProfile profile = MakeProfile(8192, 4, 8);
  EngineContext context = MakeContext(profile);
  const ModePlan plan = BuildPlan(Mode::kEco, context);

  EXPECT_TRUE(PlanHas(plan, "disable-v8-idle-tasks"));
  EXPECT_TRUE(PlanHas(plan, "animation-duration-scale"));
  EXPECT_TRUE(PlanHas(plan, "force-prefers-reduced-motion"));
  EXPECT_FALSE(PlanHas(plan, "disable-gpu-vsync")) << "ECO must not trade power for latency";
  EXPECT_EQ(static_cast<int>(plan.runtime.target_fps), 30);
  EXPECT_TRUE(plan.runtime.reduce_motion);
  EXPECT_FALSE(plan.runtime.keep_display_awake);
  EXPECT_TRUE(plan.runtime.metrics_interval_ms >= 2000) << "fewer wake-ups on battery";
}

TEST(ModeEngine, UltraAdaptsToDetectedHardware) {
  const SystemProfile small = MakeProfile(2048, 2, 2);
  const SystemProfile big = MakeProfile(65536, 32, 64);
  EngineContext small_context = MakeContext(small);
  EngineContext big_context = MakeContext(big);

  const ModePlan small_plan = BuildPlan(Mode::kUltra, small_context);
  const ModePlan big_plan = BuildPlan(Mode::kUltra, big_context);

  EXPECT_TRUE(PlanHas(small_plan, "renderer-process-limit"));
  const int small_limit = std::atoi(small_plan.boot.ValueOf("renderer-process-limit").c_str());
  const int big_limit = std::atoi(big_plan.boot.ValueOf("renderer-process-limit").c_str());
  EXPECT_LT(small_limit, big_limit);
  EXPECT_TRUE(small_plan.runtime.cache_size_mb < big_plan.runtime.cache_size_mb);
}

TEST(ModeEngine, OverridesFromSettingsWin) {
  const SystemProfile profile = MakeProfile(8192, 4, 8);
  EngineContext context = MakeContext(profile);
  context.overrides.cache_size_mb = 777;
  context.overrides.renderer_process_limit = 5;
  context.overrides.sleep_after_seconds = 42;
  context.overrides.block_images = true;
  context.overrides.target_fps = 120.0;
  context.overrides.show_perf_hud = true;

  const ModePlan plan = BuildPlan(Mode::kUltra, context);
  EXPECT_EQ(plan.runtime.cache_size_mb, 777);
  EXPECT_EQ(plan.runtime.sleep_after_seconds, 42);
  EXPECT_TRUE(plan.runtime.block_images);
  EXPECT_EQ(static_cast<int>(plan.runtime.target_fps), 120);
  EXPECT_TRUE(plan.runtime.show_perf_hud);
  EXPECT_EQ(std::atoi(plan.boot.ValueOf("renderer-process-limit").c_str()), 5);
  // The cache switch must agree with the policy value.
  EXPECT_EQ(plan.boot.ValueOf("disk-cache-size"), std::to_string(777LL * 1024 * 1024));
}

TEST(ModeEngine, SanitisesNonsenseSettings) {
  const SystemProfile profile = MakeProfile(8192, 4, 8);
  EngineContext context = MakeContext(profile);
  context.overrides.target_fps = 3.0;                 // below the floor -> 15
  context.overrides.warn_pressure = 0.95;
  context.overrides.discard_pressure = 0.5;           // below warn -> clamped up
  context.overrides.sleep_after_seconds = 900;
  context.overrides.discard_after_seconds = 60;       // earlier than sleep -> raised
  context.overrides.metrics_interval_ms = 10;         // too aggressive -> clamped
  context.overrides.cache_size_mb = 0;                // meaningless -> clamped

  const ModePlan plan = BuildPlan(Mode::kUltra, context);
  EXPECT_EQ(static_cast<int>(plan.runtime.target_fps), 15);
  EXPECT_GE(plan.runtime.discard_pressure, plan.runtime.warn_pressure);
  EXPECT_GE(plan.runtime.discard_after_seconds, plan.runtime.sleep_after_seconds);
  EXPECT_GE(plan.runtime.metrics_interval_ms, 250);
  EXPECT_GE(plan.runtime.cache_size_mb, 16);
}

TEST(ModeEngine, LabFlagsRequireExplicitOptIn) {
  const SystemProfile profile = MakeProfile(16384, 8, 16);
  EngineContext context = MakeContext(profile);
  const ModePlan safe_plan = BuildPlan(Mode::kGaming, context);
  EXPECT_FALSE(PlanHas(safe_plan, "disable-gpu-watchdog"));
  EXPECT_FALSE(safe_plan.warnings.size() > 0 && safe_plan.warnings[0].find("Lab") != std::string::npos);

  context.overrides.allow_lab = true;
  const ModePlan lab_plan = BuildPlan(Mode::kGaming, context);
  EXPECT_TRUE(PlanHas(lab_plan, "disable-gpu-watchdog"));
  EXPECT_GT(lab_plan.warnings.size(), 0u) << "lab flags must come with a warning";
}

TEST(ModeEngine, DefaultModeChoosesSensibly) {
  SystemProfile tiny = MakeProfile(1800, 2, 2);
  EXPECT_TRUE(DefaultModeFor(tiny) == Mode::kLowRam);

  SystemProfile low = MakeProfile(4096, 2, 4);
  low.battery = BatteryState::kFull;
  EXPECT_TRUE(DefaultModeFor(low) == Mode::kLowRam);

  SystemProfile laptop = MakeProfile(8192, 4, 8);
  laptop.battery = BatteryState::kDischarging;
  laptop.battery_percent = 20.0;
  EXPECT_TRUE(DefaultModeFor(laptop) == Mode::kEco);

  SystemProfile workstation = MakeProfile(32768, 16, 32);
  workstation.battery = BatteryState::kNotPresent;
  workstation.gpu_tier = GpuTier::kDiscrete;
  EXPECT_TRUE(DefaultModeFor(workstation) == Mode::kHighPerformance);

  SystemProfile normal = MakeProfile(16384, 8, 16);
  normal.battery = BatteryState::kFull;
  EXPECT_TRUE(DefaultModeFor(normal) == Mode::kUltra);
}

TEST(ModeEngine, ProfileRecommendationsScaleWithHardware) {
  const SystemProfile tiny = MakeProfile(1024, 1, 2);
  const SystemProfile huge = MakeProfile(65536, 32, 64);
  EXPECT_TRUE(tiny.RecommendedRendererLimit() >= 2);
  EXPECT_TRUE(tiny.RecommendedRendererLimit() <= 3);
  EXPECT_TRUE(huge.RecommendedRendererLimit() > tiny.RecommendedRendererLimit());
  EXPECT_TRUE(tiny.RecommendedRasterThreads() >= 1);
  EXPECT_TRUE(huge.RecommendedRasterThreads() <= 4);
  EXPECT_TRUE(tiny.RecommendedCacheSizeMb() < huge.RecommendedCacheSizeMb());
  EXPECT_TRUE(tiny.IsLowRam());
  EXPECT_FALSE(huge.IsLowRam());
}

TEST(ModeEngine, ReportsArePrintableAndStable) {
  const SystemProfile profile = MakeProfile(8192, 4, 8);
  EngineContext context = MakeContext(profile);
  const ModePlan plan = BuildPlan(Mode::kGaming, context);

  const std::string command_line = plan.boot.ToCommandLine();
  EXPECT_TRUE(command_line.find("--disable-gpu-vsync") != std::string::npos);
  if (!plan.boot.enabled_features().empty()) {
    EXPECT_TRUE(command_line.find("--enable-features=") != std::string::npos);
  }
  // The command line must contain every planned switch, verbatim.
  for (const PlannedSwitch& planned : plan.boot.switches()) {
    EXPECT_TRUE(command_line.find(planned.ToFlag()) != std::string::npos) << planned.id;
  }

  const std::string report = plan.boot.ToReport();
  EXPECT_TRUE(report.find("disable-gpu-vsync") != std::string::npos);
  EXPECT_TRUE(report.find("verified") != std::string::npos);

  const std::vector<std::string> flags = plan.boot.ToSortedFlagList();
  EXPECT_GT(flags.size(), 5u);
  for (size_t i = 1; i < flags.size(); ++i) {
    EXPECT_TRUE(flags[i - 1] <= flags[i]) << "flag list must be sorted for diffing";
  }
  EXPECT_FALSE(DescribePlanOneLine(plan).empty());
  EXPECT_TRUE(plan.restart_required);
  EXPECT_GT(plan.estimated_memory_budget_mb, 0);
}

TEST(ModeEngine, EveryModeProducesAUsablePlanWithNoProfile) {
  EngineContext context;  // no profile at all (detection failed)
  for (const ModeInfo& info : AllModes()) {
    const ModePlan plan = BuildPlan(info.mode, context);
    EXPECT_FALSE(plan.boot.Empty()) << info.id << " produced an empty plan";
    EXPECT_TRUE(plan.runtime.metrics_interval_ms >= 250);
  }
}

TEST(ModeEngine, NotesAndWarningsArePopulated) {
  const SystemProfile profile = MakeProfile(4096, 2, 4);
  EngineContext context = MakeContext(profile);
  const ModePlan plan = BuildPlan(Mode::kLowRam, context);
  EXPECT_GT(plan.notes.size(), 0u) << "the UI needs rationale text to show the user";
}
