// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/mode_engine.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

#include "core/chromium_catalog.h"
#include "core/format.h"

namespace browserx {
namespace core {
namespace {

constexpr int kMb = 1 << 20;

// Human hint added to every reason so the UI can show *why* a switch is there.
std::string ProvenanceTextFor(const char* id) {
  const SwitchEntry* entry = FindSwitch(id);
  if (entry == nullptr) return "unverified - do not ship";
  return std::string(ProvenanceLabel(entry->provenance)) + " (" + entry->source + ")";
}

/// Applies runtime overrides on top of a mode's defaults (settings.json wins).
void ApplyOverrides(const RuntimeOverrides& overrides, RuntimePolicy* policy) {
  auto bind_bool = [](const std::optional<bool>& source, bool* target) {
    if (source.has_value()) *target = *source;
  };
  auto bind_int = [](const std::optional<int>& source, int* target) {
    if (source.has_value()) *target = *source;
  };
  auto bind_double = [](const std::optional<double>& source, double* target) {
    if (source.has_value()) *target = *source;
  };

  bind_bool(overrides.background_throttling, &policy->background_throttling);
  bind_bool(overrides.block_images, &policy->block_images);
  bind_bool(overrides.block_media, &policy->block_media);
  bind_bool(overrides.block_fonts, &policy->block_fonts);
  bind_bool(overrides.block_third_party, &policy->block_third_party);
  bind_bool(overrides.block_trackers, &policy->block_trackers);
  bind_bool(overrides.bfcache, &policy->bfcache);
  bind_bool(overrides.preconnect, &policy->preconnect);
  bind_bool(overrides.code_cache, &policy->code_cache);
  bind_bool(overrides.reduce_motion, &policy->reduce_motion);
  bind_bool(overrides.show_perf_hud, &policy->show_perf_hud);
  bind_bool(overrides.keep_display_awake, &policy->keep_display_awake);
  bind_bool(overrides.always_on_top, &policy->always_on_top);
  bind_bool(overrides.lazy_new_tabs, &policy->lazy_new_tabs);

  bind_int(overrides.sleep_after_seconds, &policy->sleep_after_seconds);
  bind_int(overrides.discard_after_seconds, &policy->discard_after_seconds);
  bind_int(overrides.metrics_interval_ms, &policy->metrics_interval_ms);
  bind_int(overrides.cache_size_mb, &policy->cache_size_mb);

  bind_double(overrides.target_fps, &policy->target_fps);
  bind_double(overrides.warn_pressure, &policy->warn_pressure);
  bind_double(overrides.discard_pressure, &policy->discard_pressure);
}

/// Clamps values that would otherwise be nonsense (settings can be edited by hand).
void Sanitize(RuntimePolicy* policy) {
  policy->metrics_interval_ms = std::clamp(policy->metrics_interval_ms, 250, 10000);
  policy->cache_size_mb = std::clamp(policy->cache_size_mb, 16, 4096);
  policy->sleep_after_seconds = std::max(policy->sleep_after_seconds, 0);
  policy->discard_after_seconds = std::max(policy->discard_after_seconds, 0);
  if (policy->discard_after_seconds != 0 && policy->sleep_after_seconds != 0 &&
      policy->discard_after_seconds < policy->sleep_after_seconds) {
    policy->discard_after_seconds = policy->sleep_after_seconds;
  }
  policy->warn_pressure = std::clamp(policy->warn_pressure, 0.4, 0.98);
  policy->discard_pressure = std::clamp(policy->discard_pressure, policy->warn_pressure, 0.99);
  if (policy->target_fps < 0.0) policy->target_fps = 0.0;
  if (policy->target_fps > 0.0 && policy->target_fps < 15.0) policy->target_fps = 15.0;
  if (policy->target_fps > 480.0) policy->target_fps = 480.0;
}

}  // namespace

// ---------------------------------------------------------------------------
// Mode metadata
// ---------------------------------------------------------------------------
const std::vector<ModeInfo>& AllModes() {
  static const std::vector<ModeInfo> kModes = {
      {Mode::kUltra, "ultra", "ULTRA", "\xF0\x9F\x9A\x80",
       "Balanced speed and memory - the default for everyday browsing.",
       "Adapts to your machine: renderer limits, cache sizes and tab sleeping are scaled to the "
       "RAM and core count that were detected at start-up. Background tabs are throttled, bfcache "
       "is on for instant back/forward, and idle tabs are put to sleep after 30 minutes."},
      {Mode::kGaming, "gaming", "GAME", "\xF0\x9F\x8E\xAE",
       "Maximum frame rate and minimum input latency for browser games.",
       "Removes vsync and the compositor frame-rate cap, disables every background throttle, "
       "raises audio priority, requests the discrete GPU on hybrid laptops, blocks display sleep, "
       "and offers a Boost button that puts every other tab to sleep. Pair it with the in-page "
       "FPS/frame-time HUD to see the difference."},
      {Mode::kHighPerformance, "high-performance", "HIGH PERFORMANCE", "\xE2\x9A\xA1",
       "Maximum throughput on AC power: fastest loads, no throttling.",
       "Keeps layer trees alive instead of dropping them under memory pressure, uses more raster "
       "threads, enables hardware video decode/encode and network prefetch, and gives the HTTP "
       "cache a large budget. Uses more RAM - ideal for desktops and plugged-in laptops."},
      {Mode::kLowRam, "low-ram", "LOW RAM", "\xF0\x9F\xAA\xB6",
       "Smallest possible memory footprint, even on 2-4 GB machines.",
       "Turns on Chromium's own low-end device preset, caps renderer processes, shares one "
       "renderer per site, shrinks the font/image/video caches, drops bfcache, and sleeps tabs "
       "aggressively (60 s idle, discard on LRU when memory pressure crosses 78%)."},
      {Mode::kEco, "eco", "ECO", "\xF0\x9F\x94\x8B",
       "Longest battery life: fewer wake-ups, less animation, less network.",
       "Caps the frame rate at 30, halves animation durations, asks sites for reduced motion, "
       "stops V8 idle work, avoids background networking and disk writes, and sleeps tabs "
       "quickly. Designed for laptops on battery and for background/always-on windows."},
  };
  return kModes;
}

const ModeInfo& GetModeInfo(Mode mode) {
  for (const ModeInfo& info : AllModes()) {
    if (info.mode == mode) return info;
  }
  return AllModes().front();
}

bool ParseMode(std::string_view text, Mode* out) {
  const std::string normalized = fmt::ToLower(fmt::Trim(text));
  if (normalized.empty()) return false;
  int64_t index = 0;
  if (fmt::ParseInt(normalized, &index)) {
    const int count = static_cast<int>(AllModes().size());
    if (index >= 1 && index <= count) {
      *out = AllModes()[static_cast<size_t>(index - 1)].mode;
      return true;
    }
    return false;
  }
  for (const ModeInfo& info : AllModes()) {
    if (normalized == info.id || normalized == fmt::ToLower(info.label)) {
      *out = info.mode;
      return true;
    }
    // Accept a few natural spellings people type in the CLI.
    if ((info.mode == Mode::kLowRam && (normalized == "lowram" || normalized == "low_ram" ||
                                        normalized == "low-memory" || normalized == "lite")) ||
        (info.mode == Mode::kHighPerformance && (normalized == "high" || normalized == "hp" ||
                                                 normalized == "performance" ||
                                                 normalized == "turbo")) ||
        (info.mode == Mode::kGaming && (normalized == "game" || normalized == "games")) ||
        (info.mode == Mode::kUltra && normalized == "default") ||
        (info.mode == Mode::kEco && (normalized == "battery" || normalized == "saver"))) {
      *out = info.mode;
      return true;
    }
  }
  return false;
}

Mode NextMode(Mode mode) {
  const std::vector<ModeInfo>& modes = AllModes();
  for (size_t i = 0; i < modes.size(); ++i) {
    if (modes[i].mode == mode) return modes[(i + 1) % modes.size()].mode;
  }
  return Mode::kUltra;
}

Mode PrevMode(Mode mode) {
  const std::vector<ModeInfo>& modes = AllModes();
  for (size_t i = 0; i < modes.size(); ++i) {
    if (modes[i].mode == mode) return modes[(i + modes.size() - 1) % modes.size()].mode;
  }
  return Mode::kUltra;
}

// ---------------------------------------------------------------------------
// SwitchPlan
// ---------------------------------------------------------------------------
std::string PlannedSwitch::ToFlag() const {
  if (value.empty()) return "--" + id;
  return "--" + id + "=" + value;
}

void SwitchPlan::Add(std::string id, std::string value, std::string reason, bool child_only) {
  PlannedSwitch entry;
  entry.id = std::move(id);
  entry.value = std::move(value);
  entry.reason = std::move(reason);
  entry.provenance = ProvenanceTextFor(entry.id.c_str());
  if (child_only) {
    child_switches_.push_back(std::move(entry));
  } else {
    switches_.push_back(std::move(entry));
  }
}

void SwitchPlan::EnableFeature(std::string name, std::string reason) {
  PlannedSwitch entry;
  entry.id = name;
  entry.reason = std::move(reason);
  entry.provenance = "feature";
  feature_reasons_.push_back(std::move(entry));
  enabled_features_.push_back(name);
}

void SwitchPlan::DisableFeature(std::string name, std::string reason) {
  PlannedSwitch entry;
  entry.id = name;
  entry.reason = std::move(reason);
  entry.provenance = "feature";
  feature_reasons_.push_back(std::move(entry));
  disabled_features_.push_back(name);
}

void SwitchPlan::EnableFeatureIf(bool condition, std::string name, std::string reason) {
  if (condition) EnableFeature(std::move(name), std::move(reason));
}

void SwitchPlan::AddIf(bool condition, std::string id, std::string value, std::string reason) {
  if (condition) Add(std::move(id), std::move(value), std::move(reason));
}

bool SwitchPlan::Has(const std::string& id) const {
  for (const PlannedSwitch& entry : switches_) {
    if (entry.id == id) return true;
  }
  for (const PlannedSwitch& entry : child_switches_) {
    if (entry.id == id) return true;
  }
  return false;
}

std::string SwitchPlan::ValueOf(const std::string& id) const {
  for (const PlannedSwitch& entry : switches_) {
    if (entry.id == id) return entry.value;
  }
  for (const PlannedSwitch& entry : child_switches_) {
    if (entry.id == id) return entry.value;
  }
  return std::string();
}

bool SwitchPlan::ChildEmpty() const { return child_switches_.empty(); }

bool SwitchPlan::SetSwitchValue(const std::string& id, std::string value, std::string reason) {
  for (std::vector<PlannedSwitch>* list : {&switches_, &child_switches_}) {
    for (PlannedSwitch& entry : *list) {
      if (entry.id == id) {
        entry.value = std::move(value);
        if (!reason.empty()) entry.reason = std::move(reason);
        return true;
      }
    }
  }
  return false;
}

bool SwitchPlan::Empty() const {
  return switches_.empty() && child_switches_.empty() && enabled_features_.empty() &&
         disabled_features_.empty();
}

std::string SwitchPlan::ToCommandLine() const {
  std::string out;
  for (const PlannedSwitch& entry : switches_) {
    if (!out.empty()) out.push_back(' ');
    out += entry.ToFlag();
  }
  if (!enabled_features_.empty()) {
    if (!out.empty()) out.push_back(' ');
    out += "--enable-features=" + fmt::Join(enabled_features_, ",");
  }
  if (!disabled_features_.empty()) {
    if (!out.empty()) out.push_back(' ');
    out += "--disable-features=" + fmt::Join(disabled_features_, ",");
  }
  return out;
}

std::vector<std::string> SwitchPlan::ToSortedFlagList() const {
  std::vector<std::string> flags;
  flags.reserve(switches_.size() + 2);
  for (const PlannedSwitch& entry : switches_) flags.push_back(entry.ToFlag());
  if (!enabled_features_.empty()) {
    flags.push_back("--enable-features=" + fmt::Join(enabled_features_, ","));
  }
  if (!disabled_features_.empty()) {
    flags.push_back("--disable-features=" + fmt::Join(disabled_features_, ","));
  }
  std::sort(flags.begin(), flags.end());
  return flags;
}

std::string SwitchPlan::ToReport() const {
  std::ostringstream out;
  if (switches_.empty() && enabled_features_.empty() && disabled_features_.empty()) {
    out << "  (no boot switches - Chromium defaults are already optimal here)\n";
  }
  size_t width = 0;
  for (const PlannedSwitch& entry : switches_) width = std::max(width, entry.ToFlag().size());
  for (const PlannedSwitch& entry : switches_) {
    out << "  " << fmt::PadRight(entry.ToFlag(), width) << "  " << entry.reason << "\n";
    out << "  " << std::string(width, ' ') << "    [" << entry.provenance << "]\n";
  }
  for (const PlannedSwitch& entry : feature_reasons_) {
    out << "  feature:" << entry.id << "  " << entry.reason << "\n";
  }
  if (!child_switches_.empty()) {
    out << "  (child processes only)\n";
    for (const PlannedSwitch& entry : child_switches_) {
      out << "  " << entry.ToFlag() << "  " << entry.reason << "\n";
    }
  }
  return out.str();
}

// ---------------------------------------------------------------------------
// Plan building
// ---------------------------------------------------------------------------
namespace {

void AddSharedBaseline(const EngineContext& context, const SystemProfile& profile, ModePlan* plan) {
  // Accurate telemetry is the foundation of every automated decision below.
  plan->boot.Add("enable-precise-memory-info", "",
                 "High-resolution memory counters so the pressure gauge and action log are exact");

  // Privacy-respecting crash handling default (no dump upload, no breakpad).
  plan->boot.Add("disable-breakpad", "", "Disable crash-dump handling (privacy-friendly default)");

  // Container/small-/dev/shm safety: without this Chromium crashes on busy
  // pages inside containers (default 64 MB /dev/shm).
  if (profile.ShouldUseDevShmWorkaround()) {
    plan->boot.Add("disable-dev-shm-usage", "",
                   "Place shared memory in /tmp: /dev/shm is only " +
                       std::to_string(profile.shm_total_mb) + " MB here, which crashes renderers");
  }

  // Cache budget: already resolved from mode default + settings overrides.
  const int cache_mb = plan->runtime.cache_size_mb;
  plan->boot.Add("disk-cache-size", std::to_string(static_cast<long long>(cache_mb) * kMb),
                 "HTTP disk cache budget of " + std::to_string(cache_mb) + " MB" +
                     (context.overrides.cache_size_mb.has_value()
                          ? " (set in Settings)"
                          : " (recommended for " + std::to_string(profile.total_ram_mb) + " MB RAM)"));
}

void AddLowRamProfile(ModePlan* plan, const EngineContext& context, const SystemProfile& profile) {
  RuntimeOverrides overrides = context.overrides;

  plan->boot.Add("enable-low-end-device-mode", "",
                 "Chromium's own low-memory preset: smaller caches and fewer background services");

  const int limit = overrides.renderer_process_limit.value_or(
      std::min(profile.RecommendedRendererLimit(), 3));
  plan->boot.Add("renderer-process-limit", std::to_string(limit),
                 "Cap renderers at " + std::to_string(limit) +
                     " - each avoided renderer saves roughly 60-150 MB");
  plan->boot.Add("process-per-site", "",
                 "One renderer per site instead of per tab: fewer processes, less duplicated memory");
  plan->boot.Add("disable-back-forward-cache", "",
                 "Drop bfcache: instant back/forward is nice, but it keeps whole pages resident");
  plan->boot.Add("skia-font-cache-limit-mb", "4", "Shrink the Skia font cache (typography RAM)");
  plan->boot.Add("skia-resource-cache-limit-mb", "16", "Shrink the Skia image/resource cache");
  plan->boot.Add("mse-video-buffer-size-limit-mb", "8",
                 "Cap per-video-stream buffering: the biggest audio/video memory saving");
  plan->boot.Add("max-decoded-image-size-mb", "16",
                 "Refuse oversized image decodes that would spike memory by hundreds of MB");
  plan->boot.Add("disable-gpu-memory-buffer-video-frames", "",
                 "Decode video into system memory instead of scarce GPU buffers");
  plan->boot.Add("num-raster-threads", "1", "Single raster thread: less CPU and scratch memory");
  plan->boot.Add("gpu-disk-cache-size-kb", "1024", "Cap the shader disk cache");
  plan->boot.Add("enable-aggressive-domstorage-flushing", "",
                 "Flush DOM storage early: less RAM held by pending local-storage writes");
  plan->boot.Add("cookie-access-cache-size", "16", "Bound the cookie access cache");

  if (overrides.allow_advanced) {
    plan->boot.Add("disable-site-isolation-trials", "",
                   "Disable site isolation: significantly fewer renderers. ADVANCED: weakens "
                   "defence against speculative-execution attacks",
                   /*child_only=*/false);
    plan->notes.push_back(
        "Site isolation is disabled in LOW RAM mode - enable 'Allow advanced optimisations' to "
        "turn this off if you visit untrusted sites.");
  } else {
    plan->warnings.push_back(
        "Advanced optimisations are off, so LOW RAM mode keeps site isolation.");
  }
  plan->notes.push_back(
      "Recommended: keep 'block images' and 'block media' ready to toggle per-site for heavy pages.");

  // Feature-level process sharing (advanced): merge same-site frames harder.
  if (overrides.allow_advanced) {
    plan->boot.EnableFeature("ProcessPerSiteMainFrameThreshold/1",
                             "Share a process for the first same-site tab even on bigger machines");
    plan->boot.EnableFeature("ProcessPerSiteMainFrameSiteScalingFactor/1",
                             "Scale process sharing with available memory");
    plan->boot.EnableFeature("NetworkServiceInProcess2",
                             "Run the network service inside the browser process: removes an "
                             "entire process (~50-120 MB)");
  }
  plan->boot.EnableFeature(
      "BackForwardCacheMemoryControls",
      "If bfcache is re-enabled manually, keep its memory bounded");
}

void AddEcoProfile(ModePlan* plan, const EngineContext& context, const SystemProfile& profile) {

  plan->boot.Add("disable-v8-idle-tasks", "",
                 "Stop V8 idle GC/optimisation tasks: fewer CPU wake-ups, direct battery saving");
  plan->boot.Add("animation-duration-scale", "0.5",
                 "Halve web animation durations: less compositing work per page");
  plan->boot.Add("force-prefers-reduced-motion", "",
                 "Tell sites the user prefers reduced motion: less animation everywhere");
  plan->boot.Add("disable-gpu-shader-disk-cache", "",
                 "Stop writing shader caches to disk: fewer writes and less disk wake-up");
  plan->boot.Add("cc-scroll-animation-duration-in-seconds", "0.15",
                 "Shorter scroll animation: snappier and cheaper on the compositor");
  plan->boot.Add("num-raster-threads", std::to_string(std::max(1, profile.RecommendedRasterThreads() - 1)),
                 "One fewer raster thread than usual: lower sustained CPU draw");

  if (profile.IsLowRam() || profile.low_ram_hint) {
    plan->boot.Add("enable-low-end-device-mode", "",
                   "Battery mode on a low-RAM machine: also enable Chromium's low-memory preset");
  }
  plan->notes.push_back(
      "Background networking is suppressed by BrowserX's request blocker (the Chrome-only "
      "--disable-background-networking switch does not exist in the CEF content layer).");
  plan->notes.push_back("Frame rate is capped at 30 FPS and telemetry polls every 3 seconds.");
}

void AddGamingProfile(ModePlan* plan, const EngineContext& context, const SystemProfile& profile) {
  const RuntimeOverrides& overrides = context.overrides;
  const int threads = std::max(2, profile.cpu_physical);

  plan->boot.Add("disable-gpu-vsync", "",
                 "Never block on vertical sync: removes a frame of latency and allows uncapped FPS");
  plan->boot.Add("disable-frame-rate-limit", "",
                 "Remove the compositor's frame-rate cap so the render loop is bounded only by the "
                 "GPU");
  plan->boot.Add("disable-background-timer-throttling", "",
                 "Timers keep running at full rate when the game tab is not focused");
  plan->boot.Add("disable-renderer-backgrounding", "",
                 "Renderers keep normal priority so alt-tabbing does not stutter");
  plan->boot.Add("disable-backgrounding-occluded-windows", "",
                 "A covered game window is never deprioritised");
  plan->boot.Add("disable-ipc-flooding-protection", "",
                 "No IPC rate limiting: fewer dropped frames when the page floods the browser");
  plan->boot.Add("audio-process-high-priority", "",
                 "Raise audio thread priority: no dropouts under GPU load");
  plan->boot.Add("autoplay-policy", "no-user-gesture-required",
                 "Web games and media start without a click");
  plan->boot.Add("num-raster-threads", std::to_string(threads),
                 "Raster with " + std::to_string(threads) + " threads (physical cores detected)");
  plan->boot.Add("enable-gpu-benchmarking", "",
                 "Enable GPU benchmarking instrumentation that feeds the frame-time HUD");
  plan->boot.Add("overlay-fullscreen-video", "",
                 "Prefer hardware overlays for fullscreen video: smoother, lower power");

  if (profile.dual_gpu) {
    plan->boot.Add("force-high-performance-gpu", "",
                   "Hybrid GPU laptop detected: request the discrete GPU for 3D games");
  } else if (profile.gpu_tier == GpuTier::kDiscrete) {
    plan->boot.Add("force-high-performance-gpu", "",
                   "Discrete GPU detected: pin rendering to it");
  }
  if (!context.gl_backend.empty()) {
    plan->boot.Add("use-angle", context.gl_backend,
                   "Graphics backend chosen by the user: " + context.gl_backend);
  }
  if (overrides.allow_advanced) {
    plan->boot.Add("ignore-gpu-blocklist", "",
                   "Ignore the GPU driver blocklist so acceleration is used on this driver");
    plan->boot.Add("disable-lcd-text", "",
                   "Skip subpixel text anti-aliasing: cheaper text painting during play");
    plan->boot.Add("disable-composited-antialiasing", "",
                   "Skip composited anti-aliasing: a few percent more frame budget");
  }
  if (overrides.allow_lab) {
    plan->warnings.push_back(
        "Lab flags enabled: --disable-gpu-watchdog can avoid false GPU resets but may hang the GPU "
        "process instead.");
    plan->boot.Add("disable-gpu-watchdog", "",
                   "Lab: disable the GPU watchdog (avoids spurious GPU resets)");
  }

  plan->boot.EnableFeatureIf(profile.has_accelerated_video,
                             "AcceleratedVideoDecoder",
                             "Hardware video decode so cutscenes don't eat CPU");
  plan->boot.EnableFeatureIf(profile.has_accelerated_video,
                             "AcceleratedVideoEncoder",
                             "Hardware video encode for streaming/recording");
  plan->notes.push_back(
      "Boost (in the GAME panel) freezes and discards every non-game tab for the duration of your "
      "session - the single biggest in-game RAM and CPU win.");
  plan->notes.push_back(
      "Input latency: --disable-gpu-vsync removes one frame of queueing; the HUD shows frame time "
      "so you can verify it.");
}

void AddHighPerformanceProfile(ModePlan* plan, const EngineContext& context, const SystemProfile& profile) {
  const RuntimeOverrides& overrides = context.overrides;

  plan->boot.Add("disable-layer-tree-host-memory-pressure", "",
                 "Keep layer trees instead of dropping them under memory pressure: no re-raster "
                 "hiccups while scrolling");
  plan->boot.Add("num-raster-threads", std::to_string(std::max(2, profile.RecommendedRasterThreads())),
                 "More raster threads (half the physical cores): faster large-page painting");
  plan->boot.Add("enable-gpu-benchmarking", "",
                 "GPU benchmarking instrumentation for the frame-time HUD");
  plan->boot.Add("disable-low-end-device-mode", "",
                 "Explicitly disable the low-memory preset so caches stay large");
  plan->boot.Add("overlay-fullscreen-video", "",
                 "Prefer hardware overlays for fullscreen video");

  if (profile.dual_gpu || profile.gpu_tier == GpuTier::kDiscrete) {
    plan->boot.Add("force-high-performance-gpu", "",
                   "Pin rendering to the discrete GPU");
  }
  if (overrides.allow_advanced) {
    plan->boot.Add("ignore-gpu-blocklist", "",
                   "Use hardware acceleration even if the driver is blocklisted");
  }

  plan->boot.EnableFeature("AcceleratedVideoDecoder",
                           "Hardware video decode: lower CPU for all video");
  plan->boot.EnableFeature("AcceleratedVideoEncoder",
                           "Hardware video encode: lower CPU for calls and recording");
  plan->boot.EnableFeature("NetworkContextPrefetch",
                           "Speculative connection prefetch: faster navigation starts");
  plan->boot.EnableFeature("BackForwardCache",
                           "Instant back/forward (cheap when RAM is plentiful)");

  plan->notes.push_back(
      "Throttling is fully disabled: background tabs keep running at full speed, which is exactly "
      "what you want while plugged in.");
  plan->notes.push_back(
      "Automatic tab discarding is off; BrowserX only shows a warning when memory pressure gets "
      "critical.");
}

/// Caps that apply to *every* mode on a memory-tight device.
///
/// The per-mode profiles above are tuned for a desktop with 8-64 GB. A phone, a
/// tablet or a 4 GB laptop that the OS itself flags as low-RAM (`isLowRamDevice`
/// on Android, a low `MemTotal` elsewhere) cannot honour those numbers: four
/// renderers plus a 512 MB HTTP cache is how you get a killed browser tab. So the
/// mode still decides the *character* of the plan, and this step makes the
/// absolute numbers survivable. LOW RAM already is the low-RAM answer and is left
/// alone; the other modes keep their switches but lose their worst excesses.
void ApplyLowRamDeviceCaps(ModePlan* plan, const EngineContext& context, const SystemProfile& profile) {
  const bool very_low = profile.IsVeryLowRam();  // <= 2 GB
  const int max_renderers = very_low ? 2 : 3;
  const int max_raster = very_low ? 1 : 2;
  const int max_cache_mb = very_low ? 64 : 128;

  const int renderers = context.overrides.renderer_process_limit.value_or(max_renderers);
  const int capped_renderers = std::max(1, std::min(renderers, max_renderers));
  if (!plan->boot.SetSwitchValue("renderer-process-limit", std::to_string(capped_renderers),
                                 "Low-RAM device: at most " + std::to_string(capped_renderers) +
                                     " renderers (each one costs 60-150 MB)")) {
    plan->boot.Add("renderer-process-limit", std::to_string(capped_renderers),
                   "Low-RAM device: at most " + std::to_string(capped_renderers) + " renderers");
  }

  const int raster = std::min(profile.RecommendedRasterThreads(), max_raster);
  plan->boot.SetSwitchValue("num-raster-threads", std::to_string(std::max(1, raster)),
                            "Raster threads capped for a low-RAM device");

  const int cache = std::min(plan->runtime.cache_size_mb, max_cache_mb);
  if (cache < plan->runtime.cache_size_mb) {
    plan->runtime.cache_size_mb = cache;
    plan->boot.SetSwitchValue("disk-cache-size",
                              std::to_string(static_cast<long long>(cache) * kMb),
                              "HTTP cache capped at " + std::to_string(cache) +
                                  " MB on a low-RAM device");
  }

  // A memory-tight device must never keep a whole page resident for back/forward,
  // and should hand memory back sooner than a workstation would.
  if (plan->runtime.bfcache) {
    plan->runtime.bfcache = false;
    plan->boot.DisableFeature("BackForwardCache",
                              "A memory-tight device must not keep whole pages resident for "
                              "back/forward");
    plan->boot.DisableFeature("BackForwardCacheMemoryControls",
                              "No point bounding a cache that is switched off");
  }
  plan->runtime.sleep_after_seconds =
      std::min(plan->runtime.sleep_after_seconds > 0 ? plan->runtime.sleep_after_seconds : 600, 600);
  plan->runtime.discard_after_seconds =
      std::min(plan->runtime.discard_after_seconds > 0 ? plan->runtime.discard_after_seconds : 1800,
               1800);

  plan->notes.push_back(
      "Low-RAM device detected (" + std::to_string(profile.total_ram_mb) +
      " MB" + (profile.low_ram_hint ? ", the OS reports low-memory hardware" : "") +
      "): renderer count, raster threads, HTTP cache and tab-sleep timers are capped across every "
      "mode.");
  plan->warnings.push_back(
      "This device is memory-constrained, so this mode is less aggressive than it would be on a "
      "desktop. Use LOW RAM mode for the smallest possible footprint.");
}

void AddUltraProfile(ModePlan* plan, const EngineContext& context, const SystemProfile& profile) {
  const RuntimeOverrides& overrides = context.overrides;

  const int auto_limit = overrides.renderer_process_limit.value_or(profile.RecommendedRendererLimit());
  if (auto_limit > 0 && profile.total_ram_mb != 0) {
    plan->boot.Add("renderer-process-limit", std::to_string(auto_limit),
                   "Adaptive renderer cap for " + std::to_string(profile.total_ram_mb) +
                       " MB RAM (roughly one renderer per GB, capped by core count)");
  }
  plan->boot.Add("num-raster-threads", std::to_string(profile.RecommendedRasterThreads()),
                 "Raster threads scaled to " + std::to_string(profile.cpu_physical) +
                     " physical cores");
  plan->boot.EnableFeature("BackForwardCacheMemoryControls",
                           "Keep bfcache for instant back/forward, with bounded memory");
  plan->boot.EnableFeatureIf(overrides.allow_advanced, "NetworkContextPrefetch",
                             "Speculative connection prefetch when advanced options are enabled");
  plan->notes.push_back(
      "Ultra throttles background tabs, sleeps idle tabs after 30 minutes and keeps bfcache on - "
      "the best speed/memory balance for everyday browsing.");
}

}  // namespace

ModePlan BuildPlan(Mode mode, const EngineContext& context) {
  ModePlan plan;
  plan.mode = mode;

  // A missing profile should never crash the browser: fall back to "unknown".
  static const SystemProfile kUnknownProfile{};
  const SystemProfile& profile = context.profile != nullptr ? *context.profile : kUnknownProfile;

  // ---- defaults per mode -------------------------------------------------
  RuntimePolicy& policy = plan.runtime;
  switch (mode) {
    case Mode::kUltra:
      policy.background_throttling = true;
      policy.sleep_after_seconds = 30 * 60;
      policy.discard_after_seconds = 60 * 60;
      policy.warn_pressure = 0.80;
      policy.discard_pressure = 0.88;
      policy.cache_size_mb = profile.RecommendedCacheSizeMb();
      policy.metrics_interval_ms = 1000;
      policy.preconnect = true;
      policy.bfcache = true;
      policy.code_cache = true;
      break;
    case Mode::kGaming:
      policy.background_throttling = false;
      policy.target_fps = 0.0;  // uncapped
      policy.sleep_after_seconds = 0;      // never sleep while gaming
      policy.discard_after_seconds = 0;
      policy.warn_pressure = 0.90;
      policy.discard_pressure = 0.95;
      policy.keep_display_awake = true;
      policy.always_on_top = false;  // opt-in via the GAME panel
      policy.cache_size_mb = std::max(512, profile.RecommendedCacheSizeMb());
      policy.metrics_interval_ms = 500;
      policy.show_perf_hud = true;
      policy.bfcache = false;  // games do not care about back/forward
      policy.code_cache = true;
      policy.block_trackers = true;  // ads cost frames
      break;
    case Mode::kHighPerformance:
      policy.background_throttling = false;
      policy.target_fps = 0.0;
      policy.sleep_after_seconds = 0;
      policy.discard_after_seconds = 0;
      policy.warn_pressure = 0.92;
      policy.discard_pressure = 0.97;
      policy.cache_size_mb = std::max(1024, profile.RecommendedCacheSizeMb());
      policy.metrics_interval_ms = 1000;
      policy.preconnect = true;
      policy.bfcache = true;
      policy.code_cache = true;
      policy.lazy_new_tabs = false;
      break;
    case Mode::kLowRam:
      policy.background_throttling = true;
      policy.sleep_after_seconds = 60;
      policy.discard_after_seconds = 5 * 60;
      policy.warn_pressure = 0.70;
      policy.discard_pressure = 0.78;
      policy.cache_size_mb = 48;
      policy.metrics_interval_ms = 2000;
      policy.preconnect = false;
      policy.bfcache = false;
      policy.code_cache = false;  // saves disk writes and page-cache memory
      policy.lazy_new_tabs = true;
      break;
    case Mode::kEco:
      policy.background_throttling = true;
      policy.target_fps = 30.0;
      policy.sleep_after_seconds = 60;
      policy.discard_after_seconds = 4 * 60;
      policy.warn_pressure = 0.72;
      policy.discard_pressure = 0.80;
      policy.cache_size_mb = 128;
      policy.metrics_interval_ms = 3000;
      policy.preconnect = false;
      policy.bfcache = false;
      policy.code_cache = true;
      policy.reduce_motion = true;
      policy.block_third_party = false;  // blocking costs CPU on some pages
      break;
  }

  // ---- resolve the live policy first: mode defaults, then user overrides ---
  ApplyOverrides(context.overrides, &policy);
  Sanitize(&policy);

  // ---- switches per mode -------------------------------------------------
  AddSharedBaseline(context, profile, &plan);
  switch (mode) {
    case Mode::kUltra: AddUltraProfile(&plan, context, profile); break;
    case Mode::kGaming: AddGamingProfile(&plan, context, profile); break;
    case Mode::kHighPerformance: AddHighPerformanceProfile(&plan, context, profile); break;
    case Mode::kLowRam: AddLowRamProfile(&plan, context, profile); break;
    case Mode::kEco: AddEcoProfile(&plan, context, profile); break;
  }

  // ---- absolute caps for memory-tight devices -----------------------------
  // Applied after the mode profile so a mode can never talk itself out of them.
  if (mode != Mode::kLowRam && (profile.low_ram_hint || profile.IsLowRam())) {
    ApplyLowRamDeviceCaps(&plan, context, profile);
  }

  // Keep the cache switch honest if anything changed the policy value.
  plan.boot.SetSwitchValue("disk-cache-size",
                           std::to_string(static_cast<long long>(policy.cache_size_mb) * kMb),
                           std::string());

  // Restart-required flag is resolved by the caller comparing plans; a boot
  // switch change always needs a relaunch.
  plan.restart_required = !plan.boot.Empty();

  // Memory budget estimate (used by the memory guard and the UI gauge).
  if (profile.total_ram_mb != 0) {
    switch (mode) {
      case Mode::kLowRam: plan.estimated_memory_budget_mb = 640; break;
      case Mode::kEco: plan.estimated_memory_budget_mb = 900; break;
      case Mode::kUltra: plan.estimated_memory_budget_mb = 1400; break;
      case Mode::kHighPerformance: plan.estimated_memory_budget_mb = 2200; break;
      case Mode::kGaming: plan.estimated_memory_budget_mb = 2600; break;
    }
    const int ram_share = static_cast<int>(profile.total_ram_mb / 3);
    if (plan.estimated_memory_budget_mb > ram_share) {
      plan.estimated_memory_budget_mb = ram_share;
    }
  }

  if (policy.block_images || policy.block_media || policy.block_fonts) {
    plan.notes.push_back(
        "Resource blocking is on: images/media/fonts are dropped by the request interceptor "
        "(per-site allowlist available).");
  }
  if (!plan.boot.ChildEmpty()) {
    plan.notes.push_back("Some switches are applied only to child processes.");
  }
  return plan;
}

Mode DefaultModeFor(const SystemProfile& profile) {
  if (profile.battery == BatteryState::kDischarging && profile.battery_percent >= 0 &&
      profile.battery_percent < 35.0) {
    return Mode::kEco;
  }
  if (profile.IsVeryLowRam()) return Mode::kLowRam;
  if (profile.low_ram_hint) return Mode::kLowRam;
  if (profile.gpu_tier == GpuTier::kDiscrete && profile.total_ram_mb >= 16384 &&
      profile.battery == BatteryState::kNotPresent) {
    return Mode::kHighPerformance;
  }
  return Mode::kUltra;
}

std::string DescribePlanOneLine(const ModePlan& plan) {
  const ModeInfo& info = GetModeInfo(plan.mode);
  char buffer[256];
  std::snprintf(buffer, sizeof(buffer), "%s (%s)%s", info.label, info.id,
                plan.boot.Empty() ? "" : " - boot switches pending");
  return std::string(buffer);
}

}  // namespace core
}  // namespace browserx
