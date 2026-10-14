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
      {Mode::kSuperInternet, "super-internet", "SUPER INTERNET", "\xF0\x9F\x9A\x80",
       "Every network throttle removed: fastest transfers and the lowest connection latency a "
       "browser can ask for.",
       "Opens connections before they are needed and keeps them alive (Happy Eyeballs, optimistic "
       "DNS, TLS 1.3 early data, HTTP/2 keep-alive), removes the IPC rate limiter, prefetches "
       "speculatively, and never unloads a tab - so a background transfer or upload is never "
       "killed to free memory. Honest limit: a browser cannot create bandwidth. What this mode "
       "buys you is Chromium staying out of the way; the ceiling stays your link, your router and "
       "the server."},
      {Mode::kDownload, "download", "DOWNLOAD", "\xF0\x9F\x93\xA5",
       "Built for big transfers: parallel range requests, a huge cache, nothing unloaded or "
       "throttled while a queue runs.",
       "Enables Chromium's parallel range downloads so large files arrive over several "
       "connections where the server supports it, keeps speculative connections and TLS 1.3 "
       "early data on, gives the HTTP cache its largest budget, and disables background "
       "throttling and tab unloading so a long transfer is never interrupted or deprioritised. "
       "The display stays awake. Many servers cap per-connection speed - parallelism is exactly "
       "the difference; a server that refuses range requests will not go faster."},
      {Mode::kStreaming, "streaming", "STREAMING", "\xF0\x9F\x93\xBA",
       "Smooth video and audio: hardware decode, uncapped frame rate, screen stays awake.",
       "Requests hardware video decode and encode, removes the compositor frame cap, allows "
       "playback without a click, prefers hardware overlays for fullscreen video and keeps the "
       "display awake while the mode is active - the browser's only legitimate power lever for "
       "watching something. Tabs are never unloaded or throttled, so a playing tab keeps its "
       "buffer. If no working hardware decoder is detected, you get a warning instead of a "
       "silent CPU hit."},
      {Mode::kPrivacy, "privacy", "PRIVACY", "\xF0\x9F\x95\xB5\xEF\xB8\x8F",
       "Nothing is left behind: the shield runs at full strength, third-party heavy resources "
       "are cut, and nothing is written to the disk cache.",
       "Sets the HTTP cache to its floor (16 MB, the smallest the engine allows) so pages are "
       "not kept on disk after you leave them, "
       "stops the GPU shader cache from being written, refuses remote web fonts (a third-party "
       "fetch on almost every site), and blocks third-party images, media and fonts before they "
       "are requested. The tracker shield does the rest of the work. Honest limit: this mode "
       "hardens the profile you are already in; it is not an incognito window, and it cannot "
       "stop a site you log in to from knowing who you are."},
      {Mode::kDataSaver, "data-saver", "DATA SAVER", "\xF0\x9F\x93\xB6",
       "Spend the least data that still shows you the page: images, media and web fonts are "
       "dropped before they are requested, and what does load is cached hard.",
       "The biggest data lever a browser has is not requesting things. Images, video/audio and "
       "web fonts are cancelled before they leave the machine, third-party versions of the same "
       "are cut too, remote fonts are refused, and speculative connections are switched off "
       "because prefetching spends data on pages you may never open. What does load is cached "
       "aggressively so a repeat visit costs almost nothing. Honest limit: a page whose text "
       "sits inside an image will look empty - the shield allowlist is one click away when a "
       "site matters enough."},
      {Mode::kWeakNetwork, "weak-network", "WEAK NETWORK", "\xF0\x9F\x90\xA2",
       "For a slow, crowded or metered link: fewer round trips, less requested, and Chromium "
       "told the truth about the connection so it stops waiting on it.",
       "Enables Chromium's network quality estimator and tells it the link is slow, so the "
       "engine itself backs off instead of optimistically queueing work that will time out: "
       "shorter timeouts, deferred speculative requests, smaller media buffers. Optimistic DNS "
       "is on so short connections skip a round trip, images and third-party heavy resources are "
       "dropped, and the cache is large because on a slow link the cache is the fastest server "
       "you have. Honest limit: forcing the reported connection type changes what Chromium "
       "assumes, not what the link can do - nothing here makes a slow connection faster."},
      {Mode::kLowLatency, "low-latency", "LOW LATENCY", "\xE2\x8F\xB1",
       "Built for the millisecond that matters: the first byte and the response to your input, "
       "not the last kilobyte of a download.",
       "Keeps connections warm and handshakes skipped (TLS early data, HTTP/2 keep-alive, "
       "address sorting), runs the network service in-process so every request crosses one IPC "
       "hop fewer, removes the IPC rate limiter, and raises audio priority so voice and video "
       "calls do not crackle. Speculative prefetch is switched off here on purpose: it competes "
       "for the connection with the thing you are actually waiting for. Telemetry runs at 250 ms "
       "so the effect is visible while it happens."},
      {Mode::kReader, "reader", "READER", "\xF0\x9F\x93\x96",
       "Long-form reading: the page stays resident and legible for hours, while animation and "
       "video noise are stopped.",
       "Keeps the back/forward cache and a large HTTP cache so returning to an article is "
       "instant, sleeps idle tabs late rather than early, and asks sites for reduced motion with "
       "scrolling and animation work cut to the minimum that still feels native. Media is not "
       "requested at all, which on a news site is most of the page weight. Honest limit: pages "
       "that are mostly video are not readable in this mode - use STREAMING for those."},
      {Mode::kBenchmark, "benchmark", "BENCHMARK", "\xF0\x9F\x93\x8A",
       "A measurement mode: uncapped frame rate, Chromium's own FPS counter, GPU benchmarking "
       "hints and 250 ms telemetry, so the numbers you compare are real.",
       "Removes vsync and the compositor frame cap, turns on Chromium's FPS overlay and GPU "
       "benchmarking hints, disables background throttling, and takes memory thresholds almost "
       "out of the way so nothing trims mid-run. Honest limit: this is a diagnostic mode, not a "
       "fast browsing mode - an uncapped frame rate burns power and says nothing about how the "
       "browser behaves with everything switched back on."},
      {Mode::kKiosk, "kiosk", "KIOSK", "\xF0\x9F\x96\xA5",
       "An unattended screen: stays awake, stays in front, plays without a click, and never "
       "unloads the tab it is showing.",
       "Keeps the display awake and the window on top, allows media playback without a gesture, "
       "prefers hardware overlays for fullscreen video, hides the page's scrollbars for a clean "
       "signage look, and disables background timer throttling so a dashboard keeps updating "
       "while it is not focused. No tab is ever slept or discarded while this mode is active. "
       "Honest limit: this mode keeps the screen awake; it cannot stop the operating system from "
       "sleeping the machine, which is a power setting outside the browser."},
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

/// Features are resolved last-write-wins.
///
/// A mode enables what it wants and a later stage (the low-RAM device caps, a
/// user override) can countermand it. Without this, the same feature would end up
/// in *both* --enable-features and --disable-features, and Chromium's parser would
/// silently pick one while the plan showed a contradiction to the user.
void SwitchPlan::EnableFeature(std::string name, std::string reason) {
  SetFeatureState(name, std::move(reason), /*enabled=*/true);
}

void SwitchPlan::DisableFeature(std::string name, std::string reason) {
  SetFeatureState(name, std::move(reason), /*enabled=*/false);
}

void SwitchPlan::SetFeatureState(const std::string& name, std::string reason, bool enabled) {
  const auto drop = [&name](std::vector<std::string>* list) {
    list->erase(std::remove(list->begin(), list->end(), name), list->end());
  };
  drop(&enabled_features_);
  drop(&disabled_features_);

  // Keep exactly one explanation entry per feature: the decision that stands.
  feature_reasons_.erase(
      std::remove_if(feature_reasons_.begin(), feature_reasons_.end(),
                     [&name](const PlannedSwitch& entry) { return entry.id == name; }),
      feature_reasons_.end());

  PlannedSwitch entry;
  entry.id = name;
  entry.reason = std::move(reason);
  entry.provenance = "feature";
  feature_reasons_.push_back(std::move(entry));

  if (enabled) {
    enabled_features_.push_back(name);
  } else {
    disabled_features_.push_back(name);
  }
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

void SwitchPlan::SetSwitchValueOrAdd(std::string id, std::string value, std::string reason) {
  // Note the deliberate copy: SetSwitchValue takes its reason by value, so passing
  // std::move(reason) here would leave `reason` empty for the Add() fallback and the
  // switch would enter the plan with no explanation - exactly what
  // NoPlannedSwitchIsUnverified exists to catch.
  if (SetSwitchValue(id, value, reason)) return;
  Add(std::move(id), std::move(value), std::move(reason));
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

void AddPrivacyProfile(ModePlan* plan, const EngineContext& context,
                        const SystemProfile& profile) {
  (void)context;
  (void)profile;
  // The disk is the part of a browser that keeps records. Each switch below
  // removes one; none of them is a speed claim, and the plan says so.
  // The tiny cache budget comes from the policy (cache_size_mb), which is what drives
  // the --disk-cache-size switch for every mode - one source of truth, not two.
  plan->boot.Add("disable-gpu-shader-disk-cache", "",
                 "Do not write compiled GPU shaders into the profile directory");
  plan->boot.Add("disable-remote-fonts", "",
                 "Refuse third-party web fonts: one less host that sees you on every page");
  plan->notes.push_back(
      "The tracker shield stays on and does most of the work here: it blocks ad, analytics and "
      "fingerprinting hosts before their requests leave the browser. This mode adds the "
      "third-party images, media and fonts the shield has no rule for.");
  plan->notes.push_back(
      "Honest limit: this hardens the profile you are already signed into. It is not an "
      "incognito window - BrowserX does not create one - and a site you log in to still knows "
      "who you are.");
  plan->notes.push_back(
      "Third-party scripts and stylesheets still load on purpose: most sites serve them from a "
      "CDN, and blocking them breaks pages rather than protecting anyone.");
}

void AddDataSaverProfile(ModePlan* plan, const EngineContext& context,
                         const SystemProfile& profile) {
  (void)context;
  plan->boot.Add("disable-remote-fonts", "",
                 "Use system fonts instead of downloading web fonts");
  // The large cache budget comes from the policy (cache_size_mb) like every other mode.
  (void)profile;
  plan->boot.EnableFeature("NetworkServiceInProcess2",
                           "One less process on a data-tight device");
  plan->notes.push_back(
      "Images, media and web fonts are cancelled before the request is sent, which is where the "
      "saving really is: a request that never happens costs no data and no radio time.");
  plan->notes.push_back(
      "Speculative preconnecting is off. It spends data on pages you may never open - a good "
      "trade only when data is free.");
  plan->notes.push_back(
      "A page whose text is inside an image will look empty. The shield allowlist "
      "(browserx://settings) turns one site back on without switching the mode off.");
}

void AddWeakNetworkProfile(ModePlan* plan, const EngineContext& context,
                           const SystemProfile& profile) {
  (void)context;
  // Enabling the estimator is what makes the forced connection type mean anything:
  // with the estimator off (Chromium's default) that switch would be inert, so this
  // mode enables both and claims the indirect effect only.
  plan->boot.EnableFeature("NetworkQualityEstimator",
                           "Let Chromium measure the link and adapt to it");
  plan->boot.SetSwitchValueOrAdd("force-effective-connection-type", "2G",
                            "Tell Chromium the link is slow: shorter timeouts, deferred "
                            "speculative work, smaller media buffers");
  plan->boot.EnableFeature("OptimisticDnsForTcp",
                           "Start connecting while DNS is still resolving: one round trip saved");
  plan->boot.EnableFeature("OptimisticDnsForQuic", "The same for QUIC connections");
  plan->boot.EnableFeature("AddressSorterConnectCache",
                           "Remember which address worked: faster reconnects on a flaky link");
  // Cache budget comes from the policy, as in every other mode.
  (void)profile;
  plan->notes.push_back(
      "Forcing the effective connection type changes what Chromium *assumes*, not what the link "
      "can do. The value is real but indirect: the engine stops queueing work it will only have "
      "to time out, and that queueing is what makes a bad connection feel stuck.");
  plan->notes.push_back(
      "The network quality estimator is switched on by this mode because Chromium ships it off; "
      "without it the forced type would never be read.");
  plan->notes.push_back(
      "Images are not requested and third-party heavy resources are cut, so every load carries "
      "less. Nothing here makes the connection itself faster.");
}

void AddLowLatencyProfile(ModePlan* plan, const EngineContext& context,
                          const SystemProfile& profile) {
  (void)context;
  (void)profile;
  plan->boot.Add("disable-ipc-flooding-protection", "",
                 "No IPC rate limiting: a request burst reaches the network without delay");
  plan->boot.Add("audio-process-high-priority", "",
                 "Raise audio priority so voice and video calls do not crackle");
  plan->boot.EnableFeature("NetworkServiceInProcess2",
                           "One less IPC hop per request: the biggest latency lever here");
  plan->boot.EnableFeature("ConnectionKeepAliveForHttp2",
                           "Keep HTTP/2 sessions open so a revisit skips the handshake");
  plan->boot.EnableFeature("EnableTLS13EarlyData",
                           "TLS 1.3 0-RTT: a resumed connection can send data immediately");
  plan->boot.EnableFeature("AddressSorterConnectCache",
                           "Try the address that connected last time, first");
  plan->boot.EnableFeature("HappyEyeballsV3",
                           "Race IPv6 and IPv4 instead of waiting for one to fail");
  plan->notes.push_back(
      "Speculative prefetch is deliberately OFF here, unlike SUPER INTERNET: a prefetch "
      "competes for the connection with the request you are actually waiting for, which is the "
      "opposite of what an interactive session needs.");
  plan->notes.push_back(
      "This mode optimises the start of a request - DNS, connect, handshake, first byte. For "
      "bulk throughput instead, use SUPER INTERNET or DOWNLOAD.");
}

void AddReaderProfile(ModePlan* plan, const EngineContext& context,
                      const SystemProfile& profile) {
  (void)context;
  (void)profile;
  plan->boot.Add("force-prefers-reduced-motion", "",
                 "Tell sites the reduced-motion preference: less animation work per page");
  plan->boot.Add("disable-smooth-scrolling", "",
                 "Scroll immediately instead of animating: text lands where you look");
  plan->boot.SetSwitchValueOrAdd("cc-scroll-animation-duration-in-seconds", "0.08",
                            "Shorten the remaining scroll animation to a minimum");
  plan->boot.EnableFeature("BackForwardCache",
                           "An article you came from returns instantly, from memory");
  plan->boot.EnableFeatureIf(plan->runtime.bfcache, "BackForwardCacheMemoryControls",
                             "Bound that cache so a long reading session cannot grow forever");
  plan->notes.push_back(
      "Media is not requested at all. On a news or blog page that is usually most of the bytes, "
      "and it is also what stops an autoplaying video from interrupting a paragraph.");
  plan->notes.push_back(
      "Animation and scrolling are cut, but text, fonts and images are untouched: this is a "
      "reading mode, not a text-only mode.");
}

void AddBenchmarkProfile(ModePlan* plan, const EngineContext& context,
                         const SystemProfile& profile) {
  (void)context;
  (void)profile;
  plan->boot.Add("disable-gpu-vsync", "",
                 "Do not synchronise with the display: required for an uncapped measurement");
  plan->boot.Add("disable-frame-rate-limit", "",
                 "Remove the compositor frame cap so the measured number is the real one");
  plan->boot.Add("show-fps-counter", "",
                 "Chromium's own frame-rate overlay, so the figure is not BrowserX's own");
  plan->boot.Add("enable-gpu-benchmarking", "",
                 "Enable the GPU benchmarking hints (lifts the WebGL performance caveat)");
  plan->boot.EnableFeature("AcceleratedVideoDecoder",
                           "Keep the normal decode path so the measurement is not skewed");
  plan->notes.push_back(
      "This is a diagnostic mode. An uncapped frame rate burns power and produces numbers that "
      "do not describe ordinary browsing - compare runs of the same thing, not against ULTRA.");
  plan->notes.push_back(
      "Memory thresholds are raised for the duration so nothing is trimmed mid-run, which also "
      "means this mode uses more RAM than the balance modes.");
}

void AddKioskProfile(ModePlan* plan, const EngineContext& context,
                     const SystemProfile& profile) {
  (void)context;
  (void)profile;
  plan->boot.Add("hide-scrollbars", "",
                 "Hide page scrollbars for an unattended signage look");
  plan->boot.Add("autoplay-policy", "no-user-gesture-required",
                 "Media starts without a click: nobody is there to click");
  plan->boot.Add("overlay-fullscreen-video", "",
                 "Fullscreen video goes to a hardware overlay: smoother on a cheap device");
  plan->boot.Add("disable-background-timer-throttling", "",
                 "A dashboard keeps updating even when the window is not focused");
  plan->boot.Add("disable-backgrounding-occluded-windows", "",
                 "Do not deprioritise the window if something covers it");
  plan->notes.push_back(
      "The window is kept on top and the display awake while this mode is active, and no tab is "
      "slept or discarded: the tab on screen is the entire job.");
  plan->notes.push_back(
      "Honest limit: the browser can keep its own window awake, but the operating system can "
      "still sleep or blank the machine - that is a power setting outside BrowserX.");
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

void AddSuperInternetProfile(ModePlan* plan, const EngineContext& context,
                             const SystemProfile& profile) {
  (void)context;
  (void)profile;
  // The IPC rate limiter exists to stop a hostile page flooding the browser
  // process. Removing it is exactly what a saturated transfer wants.
  plan->boot.Add("disable-ipc-flooding-protection", "",
                 "No IPC rate limiting between renderers and the network path: request bursts are "
                 "not delayed");

  // Connections: start them earlier, race the address families, keep them alive.
  plan->boot.EnableFeature("HappyEyeballsV3",
                           "Try IPv6 and IPv4 in parallel and keep whichever answers first: no "
                           "IPv6 stall, shorter connects");
  plan->boot.EnableFeature("OptimisticDnsForTcp",
                           "Start the TCP connection while DNS is still resolving (saves a round "
                           "trip on cold loads)");
  plan->boot.EnableFeature("OptimisticDnsForQuic",
                           "The same for QUIC/HTTP-3 connections");
  plan->boot.EnableFeature("EnableTLS13EarlyData",
                           "TLS 1.3 0-RTT: returning to a site can skip the handshake round trip");
  plan->boot.EnableFeature("ConnectionKeepAliveForHttp2",
                           "Keep HTTP/2 sessions alive longer so returning to a site reuses the "
                           "open socket");
  plan->boot.EnableFeature("AddressSorterConnectCache",
                           "Cache which address actually connected, and try it first next time");
  plan->boot.EnableFeature("NetworkContextPrefetch",
                           "Speculatively open connections so the first request does not pay for "
                           "DNS + TCP + TLS");
  plan->boot.EnableFeature("NetworkServiceInProcess2",
                           "Run the network service in-process: one less IPC hop (and one less "
                           "process) for every request");
  if (plan->runtime.bfcache) {
    plan->boot.EnableFeature("BackForwardCache",
                             "Instant back/forward while the connection work continues");
    plan->boot.EnableFeature("BackForwardCacheMemoryControls",
                             "Bound that cache so it cannot grow without limit");
  }

  plan->notes.push_back(
      "Honest limit: this mode removes every Chromium-side throttle it can. It cannot create "
      "bandwidth, make a distant server faster, or beat the speed of light - the ceiling is your "
      "link, your router and the server.");
  plan->notes.push_back(
      "QUIC / HTTP-3 is already enabled by default in Chromium and there is no supported switch to "
      "force it (the old --enable-quic is catalogued as removed), so this mode does not pretend to.");
  plan->notes.push_back(
      "Tabs are never unloaded in this mode, so an upload or a background transfer is never "
      "sacrificed to free memory. Prefetch and preconnect cost extra data on a metered connection.");
}

void AddDownloadProfile(ModePlan* plan, const EngineContext& context, const SystemProfile& profile) {
  (void)context;
  (void)profile;
  plan->boot.Add("disable-ipc-flooding-protection", "",
                 "No IPC rate limiting: transferred data is not held back by the browser process");

  plan->boot.EnableFeature("ParallelDownloading",
                           "Split large downloads into parallel range requests instead of a single "
                           "stream (works where the server supports ranges)");
  plan->boot.EnableFeature("NetworkContextPrefetch",
                           "Warm the connection for the next transfer instead of starting cold");
  plan->boot.EnableFeature("EnableTLS13EarlyData",
                           "TLS 1.3 0-RTT resumption: less handshake time per connection");
  plan->boot.EnableFeature("ConnectionKeepAliveForHttp2",
                           "Reuse the open HTTP/2 session for the next file in the queue");
  plan->boot.EnableFeature("AddressSorterConnectCache",
                           "Prefer the address that worked last time: faster reconnects");

  plan->notes.push_back(
      "Parallel range downloads are the one real browser-side download lever: many servers throttle "
      "a single connection, so several connections add up. A server that refuses range requests, or "
      "limits per IP, will not go faster - that part is not up to the browser.");
  plan->notes.push_back(
      "Nothing is unloaded or throttled while this mode runs, and the display is kept awake, so a "
      "long queue is not interrupted by the session blanking. OS suspend/hibernate is outside the "
      "browser's control.");
}

void AddStreamingProfile(ModePlan* plan, const EngineContext& context, const SystemProfile& profile) {
  (void)context;
  plan->boot.EnableFeature("AcceleratedVideoDecoder",
                           "Decode video on the GPU: less CPU, fewer dropped frames");
  plan->boot.EnableFeature("AcceleratedVideoEncoder",
                           "Hardware encode too, for calls and screen sharing");
  plan->boot.Add("autoplay-policy", "no-user-gesture-required",
                 "Playback starts without a click (no 'sound is blocked' interruptions)");
  plan->boot.Add("overlay-fullscreen-video", "",
                 "Promote fullscreen video to a hardware overlay: smoother and cheaper");
  plan->boot.EnableFeature("NetworkContextPrefetch",
                           "Start the next segment's connection before it is requested");

  if (!profile.has_accelerated_video) {
    plan->warnings.push_back(
        "No working hardware video decoder was detected on this machine, so playback will fall back "
        "to software decoding and use noticeably more CPU.");
  }
  plan->notes.push_back(
      "The display is kept awake while this mode is active. A browser cannot detect whether video "
      "is actually playing, so this is a mode-level choice, not playback detection.");
  plan->notes.push_back(
      "Tabs are never unloaded or throttled, so a playing tab keeps its buffer and stays at full "
      "frame rate in the background.");
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
    case Mode::kSuperInternet:
      policy.background_throttling = false;  // a background transfer keeps full speed
      policy.sleep_after_seconds = 0;        // never unload a tab that may be transferring
      policy.discard_after_seconds = 0;
      policy.warn_pressure = 0.90;
      policy.discard_pressure = 0.95;
      policy.cache_size_mb = std::max(1024, profile.RecommendedCacheSizeMb());
      policy.metrics_interval_ms = 500;      // tight telemetry: watch the throughput move
      policy.preconnect = true;
      policy.bfcache = true;
      policy.code_cache = true;
      policy.block_trackers = true;          // trackers cost bandwidth before they cost anything
      policy.lazy_new_tabs = false;          // start the transfer now, not when the tab is shown
      break;
    case Mode::kDownload:
      policy.background_throttling = false;
      policy.sleep_after_seconds = 0;
      policy.discard_after_seconds = 0;
      policy.warn_pressure = 0.92;
      policy.discard_pressure = 0.97;
      policy.cache_size_mb = std::max(2048, profile.RecommendedCacheSizeMb());
      policy.metrics_interval_ms = 1000;
      policy.preconnect = true;
      policy.bfcache = false;                // RAM belongs to transfer buffers, not back/forward
      policy.code_cache = true;
      policy.keep_display_awake = true;
      policy.block_trackers = true;
      policy.lazy_new_tabs = false;
      break;
    case Mode::kStreaming:
      policy.background_throttling = false;
      policy.target_fps = 0.0;               // uncapped: match the display
      policy.sleep_after_seconds = 0;
      policy.discard_after_seconds = 0;
      policy.warn_pressure = 0.88;
      policy.discard_pressure = 0.95;
      policy.cache_size_mb = std::max(1024, profile.RecommendedCacheSizeMb());
      policy.metrics_interval_ms = 1000;
      policy.preconnect = true;
      policy.bfcache = false;
      policy.code_cache = true;
      policy.keep_display_awake = true;      // watching something means the screen stays on
      policy.block_trackers = true;          // on video sites that is bandwidth and CPU saved
      policy.lazy_new_tabs = false;
      break;
    case Mode::kPrivacy:
      // Privacy, not speed: nothing is kept, and nobody else is asked for anything
      // the page does not need. The shield does most of the work (it is on in
      // every mode); what changes here is what touches the disk and third parties.
      policy.background_throttling = true;
      policy.sleep_after_seconds = 5 * 60;   // sleep tabs sooner: less resident = less to leak
      policy.discard_after_seconds = 30 * 60;
      policy.warn_pressure = 0.72;
      policy.discard_pressure = 0.80;
      // 8 MB is the request; the engine's sanity floor is 16 MB (a smaller cache
      // starves the browser rather than protecting anything), and the plan reports
      // whichever value actually applies.
      policy.cache_size_mb = 8;
      policy.metrics_interval_ms = 1000;
      policy.preconnect = false;             // no connection the page did not ask for
      policy.bfcache = false;                // pages resident in RAM are still your session
      policy.code_cache = false;             // no compiled-script cache left in the profile
      policy.block_trackers = true;
      policy.block_third_party = true;       // images/media/fonts only; scripts still load
      policy.reduce_motion = false;
      policy.lazy_new_tabs = true;
      break;
    case Mode::kDataSaver:
      policy.background_throttling = false;  // a background tab may be the one downloading
      policy.sleep_after_seconds = 20 * 60;
      policy.discard_after_seconds = 45 * 60;
      policy.warn_pressure = 0.85;
      policy.discard_pressure = 0.92;
      policy.cache_size_mb = std::max(1024, profile.RecommendedCacheSizeMb());
      policy.metrics_interval_ms = 1000;
      policy.preconnect = false;             // prefetching spends data on pages you may not open
      policy.bfcache = true;                 // a cached page costs nothing to go back to
      policy.code_cache = true;
      policy.block_images = true;            // the single biggest data saving there is
      policy.block_media = true;
      policy.block_fonts = true;
      policy.block_third_party = true;
      policy.block_trackers = true;
      policy.lazy_new_tabs = true;
      break;
    case Mode::kWeakNetwork:
      policy.background_throttling = true;
      policy.sleep_after_seconds = 10 * 60;
      policy.discard_after_seconds = 30 * 60;
      policy.warn_pressure = 0.82;
      policy.discard_pressure = 0.90;
      policy.cache_size_mb = std::max(1024, profile.RecommendedCacheSizeMb());
      policy.metrics_interval_ms = 1000;
      policy.preconnect = false;             // speculative work is wasted on a bad link
      policy.bfcache = true;
      policy.code_cache = true;
      policy.block_images = true;            // an image you never see is data you never pay for
      policy.block_third_party = true;
      policy.block_trackers = true;
      policy.lazy_new_tabs = true;
      break;
    case Mode::kLowLatency:
      policy.background_throttling = false;
      policy.target_fps = 0.0;
      policy.sleep_after_seconds = 0;        // never unload a tab you may be interacting with
      policy.discard_after_seconds = 0;
      policy.warn_pressure = 0.88;
      policy.discard_pressure = 0.94;
      policy.cache_size_mb = std::max(512, profile.RecommendedCacheSizeMb());
      policy.metrics_interval_ms = 250;      // watch latency while it happens
      policy.preconnect = false;             // see the profile note: prefetch competes here
      policy.bfcache = true;                 // instant back is part of feeling quick
      policy.code_cache = true;
      policy.block_trackers = true;
      policy.lazy_new_tabs = false;
      break;
    case Mode::kReader:
      policy.background_throttling = true;
      policy.sleep_after_seconds = 60 * 60;  // an article you are part-way through is not idle
      policy.discard_after_seconds = 3 * 60 * 60;
      policy.warn_pressure = 0.82;
      policy.discard_pressure = 0.90;
      policy.cache_size_mb = std::max(512, profile.RecommendedCacheSizeMb());
      policy.metrics_interval_ms = 2000;     // nothing here is time-critical
      policy.preconnect = true;
      policy.bfcache = true;                 // going back to the article must be instant
      policy.code_cache = true;
      policy.block_media = true;             // autoplaying video mid-sentence is the enemy
      policy.block_trackers = true;
      policy.reduce_motion = true;
      policy.lazy_new_tabs = true;
      break;
    case Mode::kBenchmark:
      policy.background_throttling = false;
      policy.target_fps = 0.0;               // measuring means not capping
      policy.sleep_after_seconds = 0;
      policy.discard_after_seconds = 0;      // a discarded tab would invalidate the run
      policy.warn_pressure = 0.95;
      policy.discard_pressure = 0.98;
      policy.cache_size_mb = std::max(512, profile.RecommendedCacheSizeMb());
      policy.metrics_interval_ms = 250;
      policy.preconnect = true;
      policy.bfcache = true;
      policy.code_cache = true;
      policy.show_perf_hud = true;           // the FPS HUD is the measurement surface
      policy.block_trackers = true;          // tracker CPU cost would skew a measurement
      policy.lazy_new_tabs = false;
      break;
    case Mode::kKiosk:
      policy.background_throttling = false;
      policy.target_fps = 0.0;
      policy.sleep_after_seconds = 0;        // the tab on screen is the whole point
      policy.discard_after_seconds = 0;
      policy.warn_pressure = 0.90;
      policy.discard_pressure = 0.96;
      policy.cache_size_mb = std::max(512, profile.RecommendedCacheSizeMb());
      policy.metrics_interval_ms = 5000;     // nobody is watching the counters
      policy.preconnect = true;
      policy.bfcache = false;
      policy.code_cache = true;
      policy.keep_display_awake = true;
      policy.always_on_top = true;           // a kiosk that can be hidden is not a kiosk
      policy.block_trackers = true;
      policy.show_status_counters = false;   // signed-out display: no counters in view
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
    case Mode::kSuperInternet: AddSuperInternetProfile(&plan, context, profile); break;
    case Mode::kDownload: AddDownloadProfile(&plan, context, profile); break;
    case Mode::kStreaming: AddStreamingProfile(&plan, context, profile); break;
    case Mode::kPrivacy: AddPrivacyProfile(&plan, context, profile); break;
    case Mode::kDataSaver: AddDataSaverProfile(&plan, context, profile); break;
    case Mode::kWeakNetwork: AddWeakNetworkProfile(&plan, context, profile); break;
    case Mode::kLowLatency: AddLowLatencyProfile(&plan, context, profile); break;
    case Mode::kReader: AddReaderProfile(&plan, context, profile); break;
    case Mode::kBenchmark: AddBenchmarkProfile(&plan, context, profile); break;
    case Mode::kKiosk: AddKioskProfile(&plan, context, profile); break;
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
      case Mode::kSuperInternet: plan.estimated_memory_budget_mb = 1800; break;
      case Mode::kDownload: plan.estimated_memory_budget_mb = 2000; break;
      case Mode::kStreaming: plan.estimated_memory_budget_mb = 1600; break;
      case Mode::kPrivacy: plan.estimated_memory_budget_mb = 900; break;
      case Mode::kDataSaver: plan.estimated_memory_budget_mb = 700; break;
      case Mode::kWeakNetwork: plan.estimated_memory_budget_mb = 900; break;
      case Mode::kLowLatency: plan.estimated_memory_budget_mb = 1700; break;
      case Mode::kReader: plan.estimated_memory_budget_mb = 1500; break;
      case Mode::kBenchmark: plan.estimated_memory_budget_mb = 1800; break;
      case Mode::kKiosk: plan.estimated_memory_budget_mb = 1400; break;
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

bool IsSpecialistMode(Mode mode) {
  switch (mode) {
    case Mode::kSuperInternet:
    case Mode::kDownload:
    case Mode::kStreaming:
    case Mode::kPrivacy:
    case Mode::kDataSaver:
    case Mode::kWeakNetwork:
    case Mode::kLowLatency:
    case Mode::kReader:
    case Mode::kBenchmark:
    case Mode::kKiosk:
      return true;
    case Mode::kUltra:
    case Mode::kGaming:
    case Mode::kHighPerformance:
    case Mode::kLowRam:
    case Mode::kEco:
      return false;
  }
  return false;  // unreachable: every enumerator is listed above
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
