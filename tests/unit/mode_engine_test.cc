// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// Mode-engine tests. The most important one is NoPlannedSwitchIsUnverified:
// it makes it impossible for a mode to ever apply an unproven flag.

#include "core/mode_engine.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "core/chromium_catalog.h"
#include "test_harness.h"

using namespace browserx::core;

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
  // Slots are allocated for every mode, in AllModes() order (Ctrl+N / --mode=N).
  EXPECT_TRUE(ParseMode("9", &mode));
  EXPECT_TRUE(mode == Mode::kPrivacy);
  EXPECT_TRUE(ParseMode("15", &mode));
  EXPECT_TRUE(mode == Mode::kKiosk);
  EXPECT_FALSE(ParseMode("16", &mode)) << "there is no 16th mode";

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

// ---------------------------------------------------------------------------
// Specialist modes: SUPER INTERNET, DOWNLOAD, STREAMING.
//
// These are the modes where the honest line matters most: a browser cannot make
// a link faster, so each test below checks that the mode removes the throttles it
// really can reach, and that the plan tells the user where the ceiling is.
// ---------------------------------------------------------------------------

TEST(ModeEngine, SuperInternetMaximisesTheNetworkPath) {
  const SystemProfile profile = MakeProfile(16384, 8, 16);
  EngineContext context = MakeContext(profile);
  const ModePlan plan = BuildPlan(Mode::kSuperInternet, context);

  // Every connection-latency lever that exists upstream, and nothing invented:
  // --enable-quic is catalogued as removed, so it must NOT appear.
  EXPECT_TRUE(plan.boot.Has("disable-ipc-flooding-protection"));
  for (const char* feature : {"HappyEyeballsV3", "OptimisticDnsForTcp", "OptimisticDnsForQuic",
                              "EnableTLS13EarlyData", "ConnectionKeepAliveForHttp2",
                              "AddressSorterConnectCache", "NetworkContextPrefetch",
                              "NetworkServiceInProcess2"}) {
    EXPECT_TRUE(std::find(plan.boot.enabled_features().begin(), plan.boot.enabled_features().end(),
                          feature) != plan.boot.enabled_features().end())
        << feature << " should be enabled in SUPER INTERNET";
  }
  EXPECT_FALSE(plan.boot.Has("enable-quic")) << "enable-quic does not exist upstream any more";

  // Runtime policy: nothing throttled, nothing unloaded, cache kept warm.
  EXPECT_FALSE(plan.runtime.background_throttling);
  EXPECT_EQ(plan.runtime.sleep_after_seconds, 0) << "a transfer must not be unloaded";
  EXPECT_EQ(plan.runtime.discard_after_seconds, 0);
  EXPECT_GE(plan.runtime.cache_size_mb, 1024);
  EXPECT_EQ(plan.runtime.metrics_interval_ms, 500) << "tight telemetry to watch throughput";
  EXPECT_TRUE(plan.runtime.block_trackers) << "trackers cost bandwidth";
  EXPECT_FALSE(plan.runtime.lazy_new_tabs) << "start the transfer now, not when the tab is shown";

  // The plan must state the ceiling instead of implying magic.
  std::string notes;
  for (const std::string& note : plan.notes) notes += note + " ";
  EXPECT_TRUE(notes.find("cannot create bandwidth") != std::string::npos);
  EXPECT_TRUE(notes.find("HTTP-3") != std::string::npos) << "should explain the QUIC situation";
}

TEST(ModeEngine, DownloadModeUsesParallelRangesAndNeverUnloads) {
  const SystemProfile profile = MakeProfile(16384, 8, 16);
  EngineContext context = MakeContext(profile);
  const ModePlan plan = BuildPlan(Mode::kDownload, context);

  const std::vector<std::string>& features = plan.boot.enabled_features();
  EXPECT_TRUE(std::find(features.begin(), features.end(), "ParallelDownloading") != features.end())
      << "parallel range downloads are the one real browser-side download lever";
  EXPECT_TRUE(plan.boot.Has("disable-ipc-flooding-protection"));

  EXPECT_EQ(plan.runtime.sleep_after_seconds, 0);
  EXPECT_EQ(plan.runtime.discard_after_seconds, 0);
  EXPECT_FALSE(plan.runtime.background_throttling);
  EXPECT_GE(plan.runtime.cache_size_mb, 2048) << "largest cache budget of any mode";
  EXPECT_TRUE(plan.runtime.keep_display_awake);
  EXPECT_FALSE(plan.runtime.bfcache) << "RAM belongs to transfer buffers, not back/forward";
}

TEST(ModeEngine, StreamingModeKeepsPlaybackAndScreenAlive) {
  SystemProfile profile = MakeProfile(8192, 4, 8);
  profile.has_accelerated_video = true;
  EngineContext context = MakeContext(profile);
  const ModePlan plan = BuildPlan(Mode::kStreaming, context);

  const std::vector<std::string>& features = plan.boot.enabled_features();
  for (const char* feature : {"AcceleratedVideoDecoder", "AcceleratedVideoEncoder"}) {
    EXPECT_TRUE(std::find(features.begin(), features.end(), feature) != features.end())
        << feature << " should be requested by STREAMING";
  }
  EXPECT_TRUE(plan.boot.Has("autoplay-policy"));
  EXPECT_TRUE(plan.boot.Has("overlay-fullscreen-video"));
  EXPECT_TRUE(plan.runtime.keep_display_awake);
  EXPECT_EQ(plan.runtime.target_fps, 0.0) << "match the display, do not cap it";
  EXPECT_EQ(plan.runtime.sleep_after_seconds, 0);
  EXPECT_EQ(plan.runtime.discard_after_seconds, 0);
  EXPECT_TRUE(plan.warnings.empty()) << "hardware decode is available on this profile";

  // Without a hardware decoder the user must be told, not surprised by CPU load.
  profile.has_accelerated_video = false;
  EngineContext no_hw = MakeContext(profile);
  const ModePlan software = BuildPlan(Mode::kStreaming, no_hw);
  // ASSERT_TRUE expands to a void expression, so it cannot be streamed onto.
  EXPECT_TRUE(software.warnings.size() > 0) << "no-decoder warning missing";
  if (!software.warnings.empty()) {
    EXPECT_TRUE(software.warnings.front().find("software decoding") != std::string::npos);
  }
}

TEST(ModeEngine, SpecialistModesAreOptInOnly) {
  EXPECT_TRUE(IsSpecialistMode(Mode::kSuperInternet));
  EXPECT_TRUE(IsSpecialistMode(Mode::kDownload));
  EXPECT_TRUE(IsSpecialistMode(Mode::kStreaming));
  for (const Mode mode : {Mode::kUltra, Mode::kGaming, Mode::kHighPerformance, Mode::kLowRam,
                          Mode::kEco}) {
    EXPECT_FALSE(IsSpecialistMode(mode));
  }

  // DefaultModeFor() may recommend a general mode, never a specialist one: those
  // are the user saying "do this one thing now".
  for (uint64_t ram : {2048u, 4096u, 8192u, 32768u}) {
    SystemProfile profile = MakeProfile(ram, 4, 8);
    EXPECT_FALSE(IsSpecialistMode(DefaultModeFor(profile)));
    profile.battery = BatteryState::kDischarging;
    profile.battery_percent = 10;
    EXPECT_FALSE(IsSpecialistMode(DefaultModeFor(profile)));
    profile.battery = BatteryState::kNotPresent;
    profile.gpu_tier = GpuTier::kDiscrete;
    EXPECT_FALSE(IsSpecialistMode(DefaultModeFor(profile)));
  }
}

TEST(ModeEngine, LowRamDeviceCapsApplyToSpecialistModesToo) {
  // A 4 GB phone that the OS flags as low-memory cannot honour a 2 GB cache, even
  // if the user picked DOWNLOAD. The caps are documented, not silent.
  SystemProfile phone = MakeProfile(4096, 8, 8);
  phone.low_ram_hint = true;
  EngineContext context = MakeContext(phone);

  for (const Mode mode : {Mode::kSuperInternet, Mode::kDownload, Mode::kStreaming}) {
    const ModePlan plan = BuildPlan(mode, context);
    EXPECT_LE(plan.runtime.cache_size_mb, 128) << "cache cap must survive a 4 GB device";
    EXPECT_FALSE(plan.runtime.bfcache);
    EXPECT_LE(plan.runtime.sleep_after_seconds, 600);
    EXPECT_LE(plan.runtime.discard_after_seconds, 1800);
    bool explained = false;
    for (const std::string& warning : plan.warnings) {
      if (warning.find("memory-constrained") != std::string::npos) explained = true;
    }
    EXPECT_TRUE(explained) << "the cap must be explained to the user";
  }
}

TEST(ModeEngine, ModesStayDistinctAndParseable) {
  // Every mode must be reachable by id and by number, and no two may share an id.
  std::set<std::string> ids;
  int index = 1;
  for (const ModeInfo& info : AllModes()) {
    ids.insert(info.id);
    Mode parsed = Mode::kUltra;
    EXPECT_TRUE(ParseMode(info.id, &parsed)) << "id " << info.id << " must parse";
    EXPECT_TRUE(parsed == info.mode);
    EXPECT_TRUE(ParseMode(std::to_string(index), &parsed)) << "numeric parse failed at " << index;
    EXPECT_TRUE(parsed == info.mode);
    ++index;
  }
  EXPECT_EQ(ids.size(), AllModes().size()) << "duplicate mode id";
  EXPECT_EQ(AllModes().size(), 15u) << "modes were added or removed: update the docs and tests";
}

TEST(ModeEngine, ModeSlotsAreAStableContract) {
  // The numeric alias (--mode=N, Ctrl+N) and the menu order are both derived from
  // AllModes(). Inserting a mode in the middle therefore silently renumbers the
  // keyboard shortcuts, so the slots are pinned here: the original five keep their
  // numbers and new modes are appended.
  const std::vector<ModeInfo>& modes = AllModes();
  const Mode expected[] = {Mode::kUltra,     Mode::kGaming,        Mode::kHighPerformance,
                           Mode::kLowRam,    Mode::kEco,           Mode::kSuperInternet,
                           Mode::kDownload,  Mode::kStreaming,     Mode::kPrivacy,
                           Mode::kDataSaver, Mode::kWeakNetwork,   Mode::kLowLatency,
                           Mode::kReader,    Mode::kBenchmark,     Mode::kKiosk};
  const char* expected_ids[] = {"ultra",       "gaming",     "high-performance", "low-ram",
                                "eco",         "super-internet", "download",    "streaming",
                                "privacy",     "data-saver", "weak-network",     "low-latency",
                                "reader",      "benchmark",  "kiosk"};
  EXPECT_EQ(modes.size(), sizeof(expected) / sizeof(expected[0]))
      << "a mode was added or removed: pin its slot here and in the docs";
  for (size_t i = 0; i < modes.size() && i < 15; ++i) {
    EXPECT_TRUE(modes[i].mode == expected[i])
        << "slot " << (i + 1) << " is " << modes[i].id << ", expected " << expected_ids[i];
    EXPECT_EQ(std::string(modes[i].id), std::string(expected_ids[i]));
    Mode parsed = Mode::kUltra;
    EXPECT_TRUE(ParseMode(std::to_string(i + 1), &parsed));
    EXPECT_TRUE(parsed == expected[i]) << "--mode=" << (i + 1) << " must stay " << expected_ids[i];
  }
}

// ---------------------------------------------------------------------------
// The second family of specialist modes: PRIVACY, DATA SAVER, WEAK NETWORK,
// LOW LATENCY, READER, BENCHMARK, KIOSK.
//
// Each of these exists because a user asked for one specific thing. The tests
// check the thing is really there, and that the plan states its ceiling: these
// modes are the easiest place in the product to overclaim, so the honest-limit
// notes are part of the contract, not decoration.
// ---------------------------------------------------------------------------

TEST(ModeEngine, PrivacyModeLivesOnTheDiskNotInTheSpeedClaims) {
  const SystemProfile profile = MakeProfile(16384, 8, 16);
  EngineContext context = MakeContext(profile);
  const ModePlan plan = BuildPlan(Mode::kPrivacy, context);

  // Never sold as speed: no vsync/frame-rate switches, no renderer count games.
  EXPECT_FALSE(plan.boot.Has("disable-gpu-vsync"));
  EXPECT_FALSE(plan.boot.Has("disable-frame-rate-limit"));

  // The disk half of the promise.
  EXPECT_TRUE(plan.boot.Has("disable-gpu-shader-disk-cache"));
  EXPECT_TRUE(plan.boot.Has("disable-remote-fonts"));
  EXPECT_TRUE(plan.boot.Has("disk-cache-size"));
  EXPECT_LE(plan.runtime.cache_size_mb, 16) << "a disk cache is a record of where you have been";
  EXPECT_FALSE(plan.runtime.bfcache);
  EXPECT_FALSE(plan.runtime.code_cache);
  EXPECT_FALSE(plan.runtime.preconnect) << "no connection the page did not ask for";
  EXPECT_TRUE(plan.runtime.block_trackers);
  EXPECT_TRUE(plan.runtime.block_third_party);

  std::string notes;
  for (const std::string& note : plan.notes) notes += note + " ";
  EXPECT_TRUE(notes.find("not an incognito window") != std::string::npos)
      << "the mode must not let the user believe it is incognito";
  EXPECT_TRUE(notes.find("scripts") != std::string::npos)
      << "the third-party script trade-off must be stated";
}

TEST(ModeEngine, DataSaverStopsRequestsInsteadOfDroppingResponses) {
  const SystemProfile profile = MakeProfile(8192, 4, 8);
  EngineContext context = MakeContext(profile);
  const ModePlan plan = BuildPlan(Mode::kDataSaver, context);

  EXPECT_TRUE(plan.runtime.block_images);
  EXPECT_TRUE(plan.runtime.block_media);
  EXPECT_TRUE(plan.runtime.block_fonts);
  EXPECT_TRUE(plan.runtime.block_third_party);
  EXPECT_TRUE(plan.boot.Has("disable-remote-fonts"));
  EXPECT_FALSE(plan.runtime.preconnect) << "prefetch spends data on pages you may never open";
  EXPECT_GE(plan.runtime.cache_size_mb, 1024) << "the cache is what makes a revisit cheap";
  EXPECT_TRUE(plan.runtime.bfcache);

  std::string notes;
  for (const std::string& note : plan.notes) notes += note + " ";
  EXPECT_TRUE(notes.find("empty") != std::string::npos)
      << "the user must be warned that image-only content will not render";
}

TEST(ModeEngine, WeakNetworkEnablesTheEstimatorBeforeForcingTheType) {
  const SystemProfile profile = MakeProfile(8192, 4, 8);
  EngineContext context = MakeContext(profile);
  const ModePlan plan = BuildPlan(Mode::kWeakNetwork, context);

  // The order matters: with the estimator off (Chromium's default) the forced
  // connection type is inert, so the mode must enable both or claim nothing.
  const std::vector<std::string>& features = plan.boot.enabled_features();
  EXPECT_TRUE(std::find(features.begin(), features.end(), "NetworkQualityEstimator") !=
              features.end());
  ASSERT_TRUE(plan.boot.Has("force-effective-connection-type"));
  EXPECT_EQ(plan.boot.switches().empty(), false);

  EXPECT_TRUE(plan.runtime.block_images);
  EXPECT_TRUE(plan.runtime.block_third_party);
  EXPECT_FALSE(plan.runtime.preconnect);

  std::string notes;
  for (const std::string& note : plan.notes) notes += note + " ";
  EXPECT_TRUE(notes.find("assumes") != std::string::npos)
      << "forcing the reported type must be described as an assumption, not a speed-up";
  EXPECT_TRUE(notes.find("faster") != std::string::npos);
}

TEST(ModeEngine, LowLatencyPrefersTheFirstByteOverPrefetching) {
  const SystemProfile profile = MakeProfile(16384, 8, 16);
  EngineContext context = MakeContext(profile);
  const ModePlan plan = BuildPlan(Mode::kLowLatency, context);

  EXPECT_FALSE(plan.runtime.preconnect)
      << "prefetch competes with the request the user is waiting for";
  EXPECT_TRUE(plan.boot.Has("disable-ipc-flooding-protection"));
  EXPECT_TRUE(plan.boot.Has("audio-process-high-priority"));
  EXPECT_EQ(plan.runtime.metrics_interval_ms, 250);
  EXPECT_EQ(plan.runtime.sleep_after_seconds, 0);

  const std::vector<std::string>& features = plan.boot.enabled_features();
  for (const char* feature : {"NetworkServiceInProcess2", "ConnectionKeepAliveForHttp2",
                              "EnableTLS13EarlyData", "AddressSorterConnectCache",
                              "HappyEyeballsV3"}) {
    EXPECT_TRUE(std::find(features.begin(), features.end(), feature) != features.end())
        << feature << " belongs to LOW LATENCY";
  }
  EXPECT_TRUE(std::find(features.begin(), features.end(), "NetworkContextPrefetch") ==
              features.end())
      << "prefetch must not sneak in through the network feature set";
}

TEST(ModeEngine, ReaderKeepsTextAndDropsMotion) {
  const SystemProfile profile = MakeProfile(8192, 4, 8);
  EngineContext context = MakeContext(profile);
  const ModePlan plan = BuildPlan(Mode::kReader, context);

  EXPECT_TRUE(plan.boot.Has("force-prefers-reduced-motion"));
  EXPECT_TRUE(plan.boot.Has("disable-smooth-scrolling"));
  EXPECT_TRUE(plan.boot.Has("cc-scroll-animation-duration-in-seconds"))
      << "the plan must carry the shortened scroll animation, not only the runtime flag";
  EXPECT_TRUE(plan.boot.Has("cc-scroll-animation-duration-in-seconds"))
      << "the plan must carry the shortened scroll animation, not just the runtime flag";
  EXPECT_TRUE(plan.runtime.reduce_motion);
  EXPECT_TRUE(plan.runtime.block_media) << "an autoplaying video interrupts reading";
  EXPECT_FALSE(plan.runtime.block_images) << "a reading mode still shows pictures";
  EXPECT_FALSE(plan.runtime.block_fonts) << "and still uses the site's typography";
  EXPECT_TRUE(plan.runtime.bfcache);
  EXPECT_GE(plan.runtime.sleep_after_seconds, 3600)
      << "an article part-way read is not an idle tab";
}

TEST(ModeEngine, BenchmarkIsHonestAboutBeingADiagnostic) {
  const SystemProfile profile = MakeProfile(16384, 8, 16);
  EngineContext context = MakeContext(profile);
  const ModePlan plan = BuildPlan(Mode::kBenchmark, context);

  EXPECT_TRUE(plan.boot.Has("disable-gpu-vsync"));
  EXPECT_TRUE(plan.boot.Has("disable-frame-rate-limit"));
  EXPECT_TRUE(plan.boot.Has("show-fps-counter"));
  EXPECT_TRUE(plan.boot.Has("enable-gpu-benchmarking"));
  EXPECT_TRUE(plan.runtime.show_perf_hud);
  EXPECT_EQ(plan.runtime.metrics_interval_ms, 250);
  EXPECT_EQ(plan.runtime.sleep_after_seconds, 0) << "a discarded tab invalidates the run";

  std::string notes;
  for (const std::string& note : plan.notes) notes += note + " ";
  EXPECT_TRUE(notes.find("diagnostic") != std::string::npos);
  EXPECT_TRUE(notes.find("power") != std::string::npos);
}

TEST(ModeEngine, KioskKeepsTheScreenAndTheTabAlive) {
  const SystemProfile profile = MakeProfile(8192, 4, 8);
  EngineContext context = MakeContext(profile);
  const ModePlan plan = BuildPlan(Mode::kKiosk, context);

  EXPECT_TRUE(plan.runtime.keep_display_awake);
  EXPECT_TRUE(plan.runtime.always_on_top);
  EXPECT_EQ(plan.runtime.sleep_after_seconds, 0);
  EXPECT_EQ(plan.runtime.discard_after_seconds, 0);
  EXPECT_TRUE(plan.boot.Has("hide-scrollbars"));
  EXPECT_TRUE(plan.boot.Has("autoplay-policy"));
  EXPECT_TRUE(plan.boot.Has("disable-background-timer-throttling"));

  std::string notes;
  for (const std::string& note : plan.notes) notes += note + " ";
  EXPECT_TRUE(notes.find("operating system") != std::string::npos)
      << "the OS can still sleep the machine: say so instead of implying otherwise";
}

TEST(ModeEngine, EverySpecialistModeIsStickyAndOptIn) {
  int specialists = 0;
  for (const ModeInfo& info : AllModes()) {
    const bool expected = info.mode != Mode::kUltra && info.mode != Mode::kGaming &&
                          info.mode != Mode::kHighPerformance && info.mode != Mode::kLowRam &&
                          info.mode != Mode::kEco;
    EXPECT_EQ(IsSpecialistMode(info.mode), expected)
        << info.id << " specialist classification changed";
    if (expected) {
      ++specialists;
      // Every specialist mode must carry at least one note: the note is where the
      // honest limit lives, and a mode without one has not thought about it.
      const ModePlan plan = BuildPlan(info.mode, MakeContext(MakeProfile(8192, 4, 8)));
      EXPECT_GT(plan.notes.size(), 0u) << info.id << " has no notes - where is its honest limit?";
    }
  }
  EXPECT_EQ(specialists, 10) << "five general modes and ten specialists make fifteen";
  EXPECT_EQ(AllModes().size(), 15u);
}

TEST(ModeEngine, PrivacyRequestsTheSmallestCacheTheEngineAllows) {
  // 8 MB is the request; the engine clamps the cache to a 16 MB floor so a mode
  // cannot starve the browser, and the plan must report the value that applies.
  const ModePlan privacy = BuildPlan(Mode::kPrivacy, MakeContext(MakeProfile(16384, 8, 16)));
  EXPECT_EQ(privacy.runtime.cache_size_mb, 16);

  // Smaller than every other mode's budget on the same machine: that is the point.
  for (const ModeInfo& info : AllModes()) {
    if (info.mode == Mode::kPrivacy) continue;
    const ModePlan other = BuildPlan(info.mode, MakeContext(MakeProfile(16384, 8, 16)));
    EXPECT_GT(other.runtime.cache_size_mb, privacy.runtime.cache_size_mb)
        << info.id << " should cache more than PRIVACY does";
  }
}
