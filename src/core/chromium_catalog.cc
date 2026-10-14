// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/chromium_catalog.h"

#include <algorithm>
#include <cstdio>

#include "core/format.h"

namespace browserx {
namespace core {
namespace {

using Provenance::kVerified;

// Sources are the Chromium files that *declare* the literal, verified on
// kCatalogVerifiedOn. Shorthand keeps the table readable.
constexpr const char* kSrcBase = "base/base_switches.h";
constexpr const char* kSrcContentH = "content/public/common/content_switches.h";
constexpr const char* kSrcContentC = "content/public/common/content_switches.cc";
constexpr const char* kSrcGl = "ui/gl/gl_switches.cc";
constexpr const char* kSrcGpu = "gpu/config/gpu_switches.cc";
constexpr const char* kSrcCc = "cc/base/switches.cc";
constexpr const char* kSrcMedia = "media/base/media_switches.cc";
constexpr const char* kSrcViz = "components/viz/common/switches.cc";
constexpr const char* kSrcSandbox = "sandbox/policy/switches.cc";
constexpr const char* kSrcGfx = "ui/gfx/switches.cc";
constexpr const char* kSrcDisplay = "ui/display/display_switches.cc";
constexpr const char* kSrcChrome = "chrome/common/chrome_switches.h";
constexpr const char* kSrcDiskCache = "net/disk_cache/cache_util.cc";
constexpr const char* kSrcContentFeat = "content/public/common/content_features.cc";
constexpr const char* kSrcMediaFeat = "media/base/media_switches.cc";
constexpr const char* kSrcNetFeat = "services/network/public/cpp/features.cc";
constexpr const char* kSrcNetBaseFeat = "net/base/features.cc";
constexpr const char* kSrcNetSwitches = "services/network/public/cpp/network_switches.cc";
constexpr const char* kSrcDownloadFeat = "components/download/public/common/download_features.cc";

// ---------------------------------------------------------------------------
// The catalogue.
// ---------------------------------------------------------------------------
const std::vector<SwitchEntry>& SwitchTable() {
  static const std::vector<SwitchEntry> kSwitches = {
      // ---- process model & memory -------------------------------------------
      {"enable-low-end-device-mode", false, "", Provenance::kVerified, Scope::kAll, Risk::kSafe,
       kSrcBase,
       "Chromium's own low-memory preset: smaller caches, fewer background services, lower limits."},
      {"disable-low-end-device-mode", false, "", Provenance::kVerified, Scope::kAll, Risk::kSafe,
       kSrcBase, "Force low-end mode off (used by ULTRA/HIGH PERFORMANCE to override a device hint)."},
      {"disable-dev-shm-usage", false, "", Provenance::kVerified, Scope::kAll, Risk::kSafe, kSrcBase,
       "Use /tmp instead of /dev/shm: avoids crashes where shared memory is tiny (containers, "
       "sandboxes, WSL)."},
      {"renderer-process-limit", true, "2", Provenance::kVerified, Scope::kAll, Risk::kSafe,
       kSrcContentC,
       "Hard cap on renderer processes; the single biggest lever on total browser RAM."},
      {"process-per-site", false, "", Provenance::kVerified, Scope::kAll, Risk::kSafe, kSrcContentC,
       "Share one renderer per site instead of per tab: fewer processes, less duplicated memory."},
      {"process-per-tab", false, "", Provenance::kVerified, Scope::kAll, Risk::kAdvanced, kSrcContentC,
       "One renderer per tab: maximum isolation and parallelism, highest memory use."},
      {"disable-back-forward-cache", false, "", Provenance::kVerified, Scope::kAll, Risk::kSafe,
       kSrcContentC,
       "Turn off the back/forward cache: instant back-navigation is nice, but it keeps entire pages "
       "resident in memory."},
      {"disable-layer-tree-host-memory-pressure", false, "", Provenance::kVerified, Scope::kAll,
       Risk::kAdvanced, kSrcCc,
       "Keep layer trees instead of dropping them when memory pressure rises: removes the "
       "re-rasterisation stutter, costs memory."},
      {"disable-site-isolation-trials", false, "", Provenance::kVerified, Scope::kAll, Risk::kAdvanced,
       kSrcContentC,
       "Disable site isolation: fewer renderers and a smaller footprint, weaker defence against "
       "speculative-execution attacks. Off by default; opt-in per mode."},
      {"single-process", false, "", Provenance::kVerified, Scope::kAll, Risk::kLab, kSrcContentC,
       "Run the whole browser in one process. Minimal process overhead, but breaks sandboxing and "
       "crashes the browser with the page. Lab flag only."},
      {"in-process-gpu", false, "", Provenance::kVerified, Scope::kAll, Risk::kAdvanced, kSrcContentC,
       "Collapse the GPU process into the browser process: saves a process (~40-80 MB) at the cost "
       "of robustness."},

      // ---- scheduling / responsiveness ---------------------------------------
      {"disable-background-timer-throttling", false, "", Provenance::kVerified, Scope::kAll,
       Risk::kSafe, kSrcContentC,
       "Stop throttling timers in background tabs: required for background game logic and "
       "video/audio pipelines."},
      {"disable-renderer-backgrounding", false, "", Provenance::kVerified, Scope::kAll, Risk::kSafe,
       kSrcContentC,
       "Keep renderers at normal priority when not focused; reduces stutter when a game loses focus."},
      {"disable-backgrounding-occluded-windows", false, "", Provenance::kVerified, Scope::kAll,
       Risk::kSafe, kSrcContentC,
       "Never deprioritise windows that are covered by another window."},
      {"force-effective-connection-type", true, "4G", Provenance::kVerified, Scope::kBrowser,
       Risk::kAdvanced, kSrcNetSwitches,
       "Force the Network Quality Estimator to report a fast connection, so Chromium never "
       "degrades image quality, load priority or media buffering on a conservative estimate. "
       "It cannot make the physical link faster, and it is inert unless the estimator is enabled "
       "(it is off by default), so no mode applies it automatically."},
      {"disable-ipc-flooding-protection", false, "", Provenance::kVerified, Scope::kBrowser,
       Risk::kSafe, kSrcContentC,
       "Remove the IPC rate limiter: fewer dropped frames when a page floods the browser with "
       "messages."},
      {"disable-v8-idle-tasks", false, "", Provenance::kVerified, Scope::kAll, Risk::kSafe,
       kSrcContentC,
       "Stop running V8's idle GC/optimisation tasks: less wake-up CPU work, better on battery."},
      {"audio-process-high-priority", false, "", Provenance::kVerified, Scope::kAll, Risk::kSafe,
       kSrcContentC,
       "Raise the audio service/thread priority: fewer audio dropouts while the GPU is saturated."},
      {"enable-precise-memory-info", false, "", Provenance::kVerified, Scope::kAll, Risk::kSafe,
       kSrcContentC,
       "High-resolution memory counters. Makes BrowserX's live telemetry and pressure gauge "
       "accurate instead of quantised."},
      {"enable-aggressive-domstorage-flushing", false, "", Provenance::kVerified, Scope::kAll,
       Risk::kSafe, kSrcContentC,
       "Flush DOM storage sooner: less RAM held by pending writes (small I/O cost)."},
      {"cookie-access-cache-size", true, "32", Provenance::kVerified, Scope::kBrowser, Risk::kSafe,
       kSrcNetFeat, "Bound the cookie access cache; small, predictable memory saving."},

      // ---- caches (RAM / disk) ----------------------------------------------
      {"disk-cache-size", true, "67108864", Provenance::kVerified, Scope::kBrowser, Risk::kSafe,
       kSrcDiskCache,
       "HTTP disk cache budget in bytes (declared in chrome_switches.h, consumed by "
       "net/disk_cache/cache_util.cc, so it applies to CEF too)."},
      {"skia-font-cache-limit-mb", true, "4", Provenance::kVerified, Scope::kAll, Risk::kSafe,
       kSrcContentC, "Cap the Skia font cache: direct, measurable renderer-RAM saving."},
      {"skia-resource-cache-limit-mb", true, "16", Provenance::kVerified, Scope::kAll, Risk::kSafe,
       kSrcContentC, "Cap Skia's resource (image) cache."},
      {"gpu-disk-cache-size-kb", true, "1024", Provenance::kVerified, Scope::kGpu, Risk::kSafe,
       kSrcGpu, "Cap the GPU shader disk cache."},
      {"disable-gpu-shader-disk-cache", false, "", Provenance::kVerified, Scope::kGpu, Risk::kSafe,
       kSrcGpu, "Never write shader caches to disk: saves disk space and write I/O on battery."},
      {"mse-video-buffer-size-limit-mb", true, "8", Provenance::kVerified, Scope::kAll, Risk::kSafe,
       kSrcMedia,
       "Cap per-stream MSE buffering: the largest single audio/video memory saving."},
      {"max-decoded-image-size-mb", true, "16", Provenance::kVerified, Scope::kAll, Risk::kSafe,
       kSrcContentC, "Refuse to decode absurdly large images; prevents multi-hundred-MB spikes."},
      {"disable-gpu-memory-buffer-video-frames", false, "", Provenance::kVerified, Scope::kAll,
       Risk::kSafe, kSrcContentC, "Decode video into system memory instead of GPU buffers."},
      {"disable-remote-fonts", false, "", Provenance::kVerified, Scope::kAll, Risk::kAdvanced,
       kSrcContentC, "Block downloadable web fonts: saves RAM and bandwidth, changes page typography."},
      {"disable-accelerated-2d-canvas", false, "", Provenance::kVerified, Scope::kAll, Risk::kAdvanced,
       kSrcContentC, "Use CPU canvas instead of GPU-backed canvas: less GPU memory, slower canvas."},

      // ---- GPU / graphics / frame rate --------------------------------------
      {"disable-gpu-vsync", false, "", Provenance::kVerified, Scope::kAll, Risk::kAdvanced, kSrcGl,
       "Do not block on vertical sync: removes a frame of latency, allows FPS above the refresh rate "
       "(may tear)."},
      {"disable-frame-rate-limit", false, "", Provenance::kVerified, Scope::kGpu, Risk::kAdvanced,
       kSrcViz,
       "Let the compositor produce frames as fast as it can, ignoring the display refresh cap."},
      {"use-angle", true, "default", Provenance::kVerified, Scope::kAll, Risk::kAdvanced, kSrcGl,
       "Select the ANGLE graphics backend (default/gl/d3d11/d3d9/vulkan/swiftshader)."},
      {"use-gl", true, "angle", Provenance::kVerified, Scope::kAll, Risk::kAdvanced, kSrcGl,
       "Select the GL implementation (angle/desktop/egl/swiftshader)."},
      {"use-cmd-decoder", true, "validating", Provenance::kVerified, Scope::kAll, Risk::kLab, kSrcGl,
       "Choose the command decoder (validating/passthrough)."},
      {"ignore-gpu-blocklist", false, "", Provenance::kVerified, Scope::kAll, Risk::kAdvanced, kSrcGpu,
       "Ignore the GPU driver blocklist: enables acceleration on blacklisted drivers (may glitch)."},
      {"enable-gpu-rasterization", false, "", Provenance::kVerified, Scope::kAll, Risk::kSafe, kSrcGpu,
       "Request GPU rasterisation. Kept for older CEF/Chromium builds; modern Chromium rasters on "
       "the GPU by default, so this is typically a no-op."},
      {"disable-gpu-rasterization", false, "", Provenance::kVerified, Scope::kAll, Risk::kAdvanced,
       kSrcGpu, "Force CPU rasterisation: trades CPU time for GPU memory (useful on very low VRAM)."},
      {"enable-gpu-benchmarking", false, "", Provenance::kVerified, Scope::kGpu, Risk::kSafe, kSrcCc,
       "Enable GPU benchmarking instrumentation used by the frame-time HUD."},
      {"disable-gpu-driver-bug-workarounds", false, "", Provenance::kVerified, Scope::kAll,
       Risk::kLab, kSrcGl, "Skip driver bug workarounds: sometimes faster, sometimes broken rendering."},
      {"force-high-performance-gpu", false, "", Provenance::kVerified, Scope::kAll, Risk::kSafe,
       kSrcGpu, "On dual-GPU laptops, request the discrete GPU: big win for 3D games."},
      {"disable-gpu-watchdog", false, "", Provenance::kVerified, Scope::kGpu, Risk::kLab, kSrcContentC,
       "Disable the GPU watchdog: avoids false 'GPU hung' resets, risks a hard hang."},
      {"disable-composited-antialiasing", false, "", Provenance::kVerified, Scope::kGpu, Risk::kAdvanced,
       kSrcCc, "Skip composited anti-aliasing: slightly cheaper frames, edge artefacts."},
      {"num-raster-threads", true, "4", Provenance::kVerified, Scope::kAll, Risk::kSafe, kSrcCc,
       "Number of raster threads (1 lowers memory/CPU on weak machines; more helps large pages)."},
      {"disable-checker-imaging", false, "", Provenance::kVerified, Scope::kAll, Risk::kSafe, kSrcCc,
       "Do not synthesise checkerboard images for unloaded images: less work per paint."},
      {"no-xshm", false, "", Provenance::kVerified, Scope::kAll, Risk::kSafe, kSrcGfx,
       "Avoid X shared-memory images: fixes rendering issues in containers/remote X and some "
       "sandboxes."},
      {"enable-native-gpu-memory-buffers", false, "", Provenance::kVerified, Scope::kAll, Risk::kAdvanced,
       kSrcGfx, "Use native GPU memory buffers for video: fewer copies, needs a capable driver."},
      {"force-device-scale-factor", true, "1", Provenance::kVerified, Scope::kAll, Risk::kAdvanced,
       kSrcDisplay, "Pin the device scale factor; useful on HiDPI displays with fractional scaling."},

      // ---- media -------------------------------------------------------------
      {"disable-accelerated-video-decode", false, "", Provenance::kVerified, Scope::kAll,
       Risk::kAdvanced, kSrcContentC,
       "Software video decoding: lower GPU memory, higher CPU use (helps on weak GPUs)."},
      {"disable-accelerated-mjpeg-decode", false, "", Provenance::kVerified, Scope::kAll,
       Risk::kAdvanced, kSrcMedia, "Software MJPEG decoding."},
      {"force-video-overlays", false, "", Provenance::kVerified, Scope::kGpu, Risk::kLab, kSrcMedia,
       "Force the overlay path for video: lower power, driver-dependent."},
      {"overlay-fullscreen-video", false, "", Provenance::kVerified, Scope::kGpu, Risk::kSafe,
       kSrcMedia, "Prefer overlays for fullscreen video: smoother playback, less power."},
      {"autoplay-policy", true, "no-user-gesture-required", Provenance::kVerified, Scope::kBrowser,
       Risk::kAdvanced, kSrcMedia,
       "Media autoplay policy. Game/HP modes allow autoplay so web games start immediately."},

      // ---- security / sandbox ------------------------------------------------
      {"no-sandbox", false, "", Provenance::kVerified, Scope::kAll, Risk::kLab, kSrcSandbox,
       "DISABLES the Chromium sandbox. Development/debugging only - never enabled by a mode."},
      {"disable-setuid-sandbox", false, "", Provenance::kVerified, Scope::kAll, Risk::kLab, kSrcSandbox,
       "Disable the setuid sandbox layer (containers). Dev only."},
      {"disable-gpu-sandbox", false, "", Provenance::kVerified, Scope::kGpu, Risk::kLab, kSrcSandbox,
       "Disable the GPU process sandbox. Dev only."},
      {"disable-web-security", false, "", Provenance::kVerified, Scope::kAll, Risk::kLab, kSrcContentC,
       "Turn off same-origin policy. Lab/diagnostic flag."},

      // ---- features that reduce web surface (memory + CPU) -------------------
      {"disable-webgl", false, "", Provenance::kVerified, Scope::kAll, Risk::kAdvanced, kSrcContentC,
       "Disable WebGL: big memory saving on weak GPUs, breaks 3D pages."},
      {"disable-webgl2", false, "", Provenance::kVerified, Scope::kAll, Risk::kAdvanced, kSrcContentC,
       "Disable WebGL2 while keeping WebGL1."},
      {"disable-3d-apis", false, "", Provenance::kVerified, Scope::kAll, Risk::kAdvanced, kSrcContentC,
       "Disable all 3D web APIs (WebGL + friend): lowers GPU/CPU load."},
      {"max-active-webgl-contexts", true, "8", Provenance::kVerified, Scope::kAll, Risk::kSafe,
       kSrcContentC, "Cap simultaneously live WebGL contexts; prevents runaway GPU memory."},
      {"disable-reading-from-canvas", false, "", Provenance::kVerified, Scope::kAll, Risk::kLab,
       kSrcContentC, "Make canvas readback return blank: fingerprinting defence, breaks many apps."},
      {"disable-speech-api", false, "", Provenance::kVerified, Scope::kBrowser, Risk::kSafe,
       kSrcContentC, "Disable Web Speech: saves the speech service and downloads."},
      {"hide-scrollbars", false, "", Provenance::kVerified, Scope::kAll, Risk::kAdvanced, kSrcContentC,
       "Hide scrollbars (app-like feel; also used by the GAME mode kiosk look)."},

      // ---- rendering / UX tuning --------------------------------------------
      {"disable-smooth-scrolling", false, "", Provenance::kVerified, Scope::kAll, Risk::kAdvanced,
       kSrcContentC, "Disable animated scroll interpolation: lower CPU on scrolling-heavy pages."},
      {"cc-scroll-animation-duration-in-seconds", true, "0.15", Provenance::kVerified, Scope::kAll,
       Risk::kSafe, kSrcCc, "Shorten the scroll animation: snappier feel, less compositing work."},
      {"disable-lcd-text", false, "", Provenance::kVerified, Scope::kAll, Risk::kSafe, kSrcContentC,
       "Skip subpixel (LCD) text anti-aliasing: cheaper text painting, greyscale AA."},
      {"animation-duration-scale", true, "0.5", Provenance::kVerified, Scope::kAll, Risk::kSafe,
       kSrcGfx, "Scale CSS animation durations; ECO halves them to cut CPU and battery use."},
      {"force-prefers-reduced-motion", false, "", Provenance::kVerified, Scope::kAll, Risk::kAdvanced,
       kSrcGfx, "Tell sites the user prefers reduced motion: less animation work everywhere."},
      {"show-fps-counter", false, "", Provenance::kVerified, Scope::kAll, Risk::kSafe, kSrcCc,
       "Chromium's built-in FPS overlay (BrowserX ships its own richer HUD instead; provided for "
       "cross-checking measurements)."},
      {"v8-cache-options", true, "code", Provenance::kVerified, Scope::kBrowser, Risk::kAdvanced,
       kSrcContentC, "Control V8 code caching (none/code/...). 'none' reduces disk writes; 'code' "
       "speeds up repeat loads."},

      // ---- diagnostics -------------------------------------------------------
      {"enable-logging", false, "", Provenance::kVerified, Scope::kAll, Risk::kSafe, kSrcContentC,
       "Enable Chromium logging to stderr."},
      {"log-level", true, "0", Provenance::kVerified, Scope::kAll, Risk::kSafe, kSrcContentC,
       "Chromium log verbosity 0-3."},
      {"enable-crash-reporter", false, "", Provenance::kVerified, Scope::kAll, Risk::kSafe, kSrcBase,
       "Enable the crash reporter (off by default for privacy)."},
      {"disable-breakpad", false, "", Provenance::kVerified, Scope::kAll, Risk::kSafe, kSrcBase,
       "Disable crash dump handling entirely (privacy-friendly default)."},

      // ---- declared only in the Chrome product layer -------------------------
      {"disable-background-networking", false, "", Provenance::kChromeOnly, Scope::kBrowser,
       Risk::kAdvanced, kSrcChrome,
       "Chrome-product switch (variations, updates, safe browsing pings). BrowserX achieves the "
       "same effect with its own request blocker, which also works under CEF."},
      {"disable-component-update", false, "", Provenance::kChromeOnly, Scope::kBrowser, Risk::kSafe,
       kSrcChrome, "Chrome component updater; CEF has no component updater."},
      {"disable-domain-reliability", false, "", Provenance::kChromeOnly, Scope::kBrowser, Risk::kSafe,
       kSrcChrome, "Chrome domain-reliability beacons; not part of CEF."},

      // ---- historically real, consumer unconfirmed (lab only) ----------------
      {"js-flags", true, "--max-old-space-size=512", Provenance::kLegacy, Scope::kRenderer,
       Risk::kLab, "(unconfirmed)",
       "v8 flags (for example heap caps). Widely used with Chrome, but the consumer could not be "
       "confirmed in current Chromium sources, so it stays lab-only."},
      {"force-gpu-mem-available-mb", true, "512", Provenance::kLegacy, Scope::kGpu, Risk::kLab,
       kSrcGpu, "Historical GPU texture-budget override; consumer unconfirmed on current builds."},
      {"gpu-program-cache-size-kb", true, "4096", Provenance::kLegacy, Scope::kGpu, Risk::kLab,
       kSrcGpu, "Historical shader program cache size; prefer gpu-disk-cache-size-kb."},

      // ---- confirmed gone or superseded: catalogued so we never claim them ---
      {"memory-pressure-off", false, "", Provenance::kRemoved, Scope::kAll, Risk::kLab, "(absent)",
       "Removed: no literal exists in current Chromium. Superseded by Chromium's own memory "
       "pressure signals, which BrowserX drives through the memory guard."},
      {"enable-quic", false, "", Provenance::kRemoved, Scope::kAll, Risk::kSafe, "(default-on)",
       "Removed: QUIC/HTTP3 is enabled by default, so the flag is unnecessary."},
      {"media-cache-size", true, "", Provenance::kRemoved, Scope::kAll, Risk::kSafe, "(absent)",
       "Removed: the media cache was replaced; use mse-video-buffer-size-limit-mb instead."},
      {"enable-oop-rasterization", false, "", Provenance::kRemoved, Scope::kAll, Risk::kSafe,
       "(default-on)", "Removed: out-of-process rasterisation is the default since Chromium 116."},
      {"enable-zero-copy", false, "", Provenance::kRemoved, Scope::kAll, Risk::kSafe, "(default-on)",
       "Removed: zero-copy raster upload is the default path."},
      {"disable-partial-raster", false, "", Provenance::kRemoved, Scope::kAll, Risk::kSafe, "(absent)",
       "Removed from current Chromium (was a 2012-era tuning switch)."},
      {"default-tile-width", true, "", Provenance::kRemoved, Scope::kAll, Risk::kSafe, "(absent)",
       "Removed: tile sizing is managed internally by the compositor."},
  };
  return kSwitches;
}

const std::vector<FeatureEntry>& FeatureTable() {
  static const std::vector<FeatureEntry> kFeatures = {
      {"NetworkServiceInProcess2", Provenance::kVerified, Risk::kAdvanced, kSrcContentFeat,
       "Run the network service inside the browser process: removes an entire process (~50-120 MB). "
       "LOW RAM turbo option."},
      {"BackForwardCache", Provenance::kVerified, Risk::kSafe, kSrcContentFeat,
       "Instant (bfcache) back/forward navigation: huge UX win, keeps whole pages resident in RAM."},
      {"BackForwardCacheMemoryControls", Provenance::kVerified, Risk::kSafe, kSrcContentFeat,
       "Memory-aware limits for the bfcache: keeps the speed-up but caps its footprint."},
      {"AcceleratedVideoDecoder", Provenance::kVerified, Risk::kSafe, kSrcMediaFeat,
       "Hardware video decoding path (VA-API/V4L2 on Linux): lower CPU and power for video."},
      {"AcceleratedVideoEncoder", Provenance::kVerified, Risk::kSafe, kSrcMediaFeat,
       "Hardware video encoding: lowers CPU for calls/streaming."},
      {"NetworkContextPrefetch", Provenance::kVerified, Risk::kSafe, kSrcNetFeat,
       "Speculative connection prefetch: faster page starts in HIGH PERFORMANCE mode."},
      {"ProcessPerSiteMainFrameThreshold", Provenance::kVerified, Risk::kSafe, kSrcContentFeat,
       "Site-instance sharing threshold for main frames: tunes how aggressively same-site tabs share "
       "processes."},
      // --- connection setup and transfer throughput (SUPER INTERNET / DOWNLOAD) ---
      {"HappyEyeballsV3", Provenance::kVerified, Risk::kSafe, kSrcNetBaseFeat,
       "Race the IPv6 and IPv4 connection attempts in parallel and keep whichever answers first: "
       "shorter connects on dual-stack networks, and no IPv6 stall."},
      {"OptimisticDnsForTcp", Provenance::kVerified, Risk::kSafe, kSrcNetBaseFeat,
       "Start the TCP connection while DNS is still resolving: removes a whole round trip from "
       "cold loads."},
      {"OptimisticDnsForQuic", Provenance::kVerified, Risk::kSafe, kSrcNetBaseFeat,
       "The same trick for QUIC/HTTP-3 connections: shorter setup for sites using HTTP-3."},
      {"EnableTLS13EarlyData", Provenance::kVerified, Risk::kSafe, kSrcNetBaseFeat,
       "Allow TLS 1.3 0-RTT resumption: returning to a site can skip the handshake round trip."},
      {"ConnectionKeepAliveForHttp2", Provenance::kVerified, Risk::kSafe, kSrcNetBaseFeat,
       "Keep HTTP/2 sessions alive longer, so coming back to a site reuses the open socket "
       "instead of paying for a new connection."},
      {"NetworkQualityEstimator", Provenance::kVerified, Risk::kSafe, kSrcNetBaseFeat,
       "Chromium's own connection-quality estimator: measures throughput and latency and adapts "
       "timeouts, buffering and speculative work to what it finds. It is OFF by default upstream, "
       "which is why a mode that forces an effective connection type must enable this too or the "
       "forced value is never read. It only changes what Chromium assumes, never the link."},
      {"AddressSorterConnectCache", Provenance::kVerified, Risk::kSafe, kSrcNetBaseFeat,
       "Remember which of a host's addresses connected successfully and try it first next time: "
       "faster reconnects on hosts with several A/AAAA records."},
      {"ParallelDownloading", Provenance::kVerified, Risk::kSafe, kSrcDownloadFeat,
       "Split large downloads into parallel range requests instead of one stream "
       "(components/download). Many servers speed up massively; a few do not, and on builds whose "
       "download backend does not consult this feature the download simply behaves as before."},
      {"ProcessPerSiteMainFrameSiteScalingFactor", Provenance::kVerified, Risk::kSafe, kSrcContentFeat,
       "Scales the per-site process sharing threshold with available memory."},
      {"ProcessPerSiteMainFrameTotalMemoryLimit", Provenance::kVerified, Risk::kSafe, kSrcContentFeat,
       "Absolute memory ceiling for the process-sharing heuristic."},
      {"CalculateNativeWinOcclusion", Provenance::kRemoved, Risk::kSafe, "(Windows-only, absent)",
       "Windows-specific occlusion tracking; not present in the Linux/CEF path."},
      {"CanvasOopRasterization", Provenance::kRemoved, Risk::kSafe, "(renamed)",
       "Historic canvas OOP-R flag; canvas is already GPU/out-of-process rasterised now."},
      {"VaapiVideoDecoder", Provenance::kLegacy, Risk::kSafe, kSrcMediaFeat,
       "Older Linux VA-API decoder flag; on current builds use AcceleratedVideoDecoder."},
      {"MemoryPressureBasedSourceBufferGC", Provenance::kLegacy, Risk::kSafe, "(unconfirmed)",
       "Memory-pressure-driven media buffer GC; not confirmed in current sources."},
      {"HighPerformanceGPU", Provenance::kLegacy, Risk::kSafe, "(unconfirmed)",
       "Dual-GPU hint; use the verified --force-high-performance-gpu switch instead."},
      {"UseEcoQoSForBackgroundProcess", Provenance::kRemoved, Risk::kSafe, "(Windows-only)",
       "Windows EcoQoS for background processes; Windows-only, not reachable from CEF on Linux."},
  };
  return kFeatures;
}

}  // namespace

const char* ProvenanceLabel(Provenance provenance) {
  switch (provenance) {
    case Provenance::kVerified: return "verified";
    case Provenance::kChromeOnly: return "chrome-only";
    case Provenance::kLegacy: return "legacy";
    case Provenance::kRemoved: return "removed";
  }
  return "?";
}

const char* ScopeLabel(Scope scope) {
  switch (scope) {
    case Scope::kAll: return "all";
    case Scope::kBrowser: return "browser";
    case Scope::kRenderer: return "renderer";
    case Scope::kGpu: return "gpu";
    case Scope::kUtility: return "utility";
  }
  return "?";
}

const char* RiskLabel(Risk risk) {
  switch (risk) {
    case Risk::kSafe: return "safe";
    case Risk::kAdvanced: return "advanced";
    case Risk::kLab: return "lab";
  }
  return "?";
}

const std::vector<SwitchEntry>& AllSwitches() { return SwitchTable(); }
const std::vector<FeatureEntry>& AllFeatures() { return FeatureTable(); }

const SwitchEntry* FindSwitch(std::string_view id) {
  for (const SwitchEntry& entry : SwitchTable()) {
    if (id == entry.id) return &entry;
  }
  return nullptr;
}

const FeatureEntry* FindFeature(std::string_view name) {
  for (const FeatureEntry& entry : FeatureTable()) {
    if (name == entry.name) return &entry;
  }
  return nullptr;
}

bool IsApplicable(const SwitchEntry& entry) {
  return entry.provenance == Provenance::kVerified || entry.provenance == Provenance::kChromeOnly;
}

bool RequiresOptIn(const SwitchEntry& entry) {
  return entry.risk != Risk::kSafe || !IsApplicable(entry);
}

CatalogStats GetCatalogStats() {
  CatalogStats stats;
  for (const SwitchEntry& entry : SwitchTable()) {
    ++stats.switches_total;
    switch (entry.provenance) {
      case Provenance::kVerified: ++stats.switches_verified; break;
      case Provenance::kChromeOnly: ++stats.switches_chrome_only; break;
      case Provenance::kLegacy: ++stats.switches_legacy; break;
      case Provenance::kRemoved: ++stats.switches_removed; break;
    }
  }
  for (const FeatureEntry& entry : FeatureTable()) {
    ++stats.features_total;
    switch (entry.provenance) {
      case Provenance::kVerified: ++stats.features_verified; break;
      case Provenance::kChromeOnly: ++stats.features_legacy; break;
      case Provenance::kLegacy: ++stats.features_legacy; break;
      case Provenance::kRemoved: ++stats.features_removed; break;
    }
  }
  return stats;
}

std::string SourceSearchUrl(std::string_view literal) {
  // Chromium code search accepts a plain query string.
  return "https://source.chromium.org/search?q=" + std::string(literal) + "&ss=chromium";
}

std::string RenderCatalogReport(bool include_removed) {
  std::string out;
  const CatalogStats stats = GetCatalogStats();
  char header[512];
  std::snprintf(header, sizeof(header),
                "Chromium switch catalogue - verified %s against chromium/%s\n"
                "%zu switches (%zu verified, %zu chrome-only, %zu legacy, %zu removed)\n"
                "%zu features (%zu verified, %zu legacy, %zu removed)\n\n",
                kCatalogVerifiedOn, kCatalogChromiumRef, stats.switches_total,
                stats.switches_verified, stats.switches_chrome_only, stats.switches_legacy,
                stats.switches_removed, stats.features_total, stats.features_verified,
                stats.features_legacy, stats.features_removed);
  out += header;
  for (const SwitchEntry& entry : SwitchTable()) {
    if (!include_removed && entry.provenance == Provenance::kRemoved) continue;
    char line[640];
    std::snprintf(line, sizeof(line), "  %-46s %-11s %-7s %-7s %s\n", entry.id,
                  ProvenanceLabel(entry.provenance), ScopeLabel(entry.scope), RiskLabel(entry.risk),
                  entry.source);
    out += line;
    if (entry.summary[0] != '\0') {
      out += "      ";
      out += fmt::Truncate(entry.summary, 140);
      out += "\n";
    }
  }
  out += "\n";
  for (const FeatureEntry& entry : FeatureTable()) {
    if (!include_removed && entry.provenance == Provenance::kRemoved) continue;
    char line[640];
    std::snprintf(line, sizeof(line), "  feature %-42s %-11s %-7s %s\n", entry.name,
                  ProvenanceLabel(entry.provenance), RiskLabel(entry.risk), entry.source);
    out += line;
  }
  return out;
}

}  // namespace core
}  // namespace browserx
