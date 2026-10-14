// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "browser/headless_probe.h"

#include <chrono>
#include <string>
#include <utility>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_load_handler.h"
#include "include/cef_render_handler.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"

#include "app/app_paths.h"
#include "app/metrics_sampler.h"
#include "core/format.h"
#include "core/log.h"

namespace browserx {
namespace browser {
namespace {

constexpr int kProbeWidth = 1280;
constexpr int kProbeHeight = 800;
constexpr int kMaxFrames = 4;  // enough to prove that compositing really runs

int NowMs() {
  using clock = std::chrono::steady_clock;
  static const clock::time_point start = clock::now();
  return static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - start).count());
}

class ProbeClient : public CefClient, public CefRenderHandler, public CefLoadHandler {
 public:
  ProbeClient(app::CefHost* host, std::string url) : host_(host), url_(std::move(url)) {}

  // --- CefClient ------------------------------------------------------------
  CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }

  // --- CefRenderHandler (off-screen rendering: no window, no display) ------
  void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override {
    rect = CefRect(0, 0, kProbeWidth, kProbeHeight);
  }

  bool GetScreenInfo(CefRefPtr<CefBrowser> browser, CefScreenInfo& info) override {
    info.device_scale_factor = 1.0f;
    info.depth = 24;
    info.is_monochrome = 0;
    info.rect = CefRect(0, 0, kProbeWidth, kProbeHeight);
    info.available_rect = info.rect;
    return true;
  }

  void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList& dirty_rects,
               const void* buffer, int width, int height) override {
    if (type != PET_VIEW) return;
    browser_ = browser;
    ++frames_painted_;
    if (first_paint_ms_ < 0) {
      first_paint_ms_ = NowMs();
      core::log::InfoF("smoke", "first frame painted after %d ms (%dx%d, %zu dirty rects)",
                       first_paint_ms_, width, height, dirty_rects.size());
    }
    // Once the page is loaded *and* we have painted a few frames, everything we
    // wanted to prove has been proven.
    if (loaded_ && frames_painted_ >= kMaxFrames) Finish();
  }

  // --- CefLoadHandler ------------------------------------------------------
  void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                 int http_status_code) override {
    if (!frame->IsMain()) return;
    browser_ = browser;
    current_url_ = frame->GetURL().ToString();
    loaded_ = true;
    load_ms_ = NowMs();
    core::log::InfoF("smoke", "main frame finished loading (HTTP %d) after %d ms",
                     http_status_code, load_ms_);
    if (frames_painted_ > 0) Finish();
    // A page can legitimately paint nothing (about:blank); give it one more tick.
  }

  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool is_loading, bool can_go_back,
                            bool can_go_forward) override {
    if (is_loading) loading_ = true;
  }

  void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode error_code,
                   const CefString& error_text, const CefString& failed_url) override {
    if (!frame->IsMain()) return;
    failed_ = true;
    error_ = error_text.ToString();
    error_code_ = static_cast<int>(error_code);
    core::log::ErrorF("smoke", "main frame failed: %s (net error %d)", error_.c_str(), error_code_);
    Finish();
  }

  // --- results -------------------------------------------------------------
  void Fill(ProbeResult* result) const {
    result->painted = frames_painted_ > 0;
    result->load_finished = loaded_;
    result->frames = frames_painted_;
    result->first_paint_ms = first_paint_ms_ < 0 ? 0 : first_paint_ms_;
    result->load_ms = load_ms_ < 0 ? 0 : load_ms_;
    result->url = current_url_.empty() ? url_ : current_url_;

    if (failed_) {
      result->ok = false;
      result->exit_code = 1;
      result->error = error_ + " (net error " + std::to_string(error_code_) + ")";
      return;
    }
    if (loaded_ && result->painted) {
      result->ok = true;
      result->exit_code = 0;
      return;
    }
    result->ok = false;
    result->exit_code = 2;
    if (!loaded_) {
      result->error = "page did not finish loading before the timeout";
    } else if (!result->painted) {
      result->error = "page loaded but nothing was painted (compositing or GPU problem)";
    }
    if (loading_) result->error += " - still loading";
  }

 private:
  /// Ends the probe exactly once, on the UI thread.
  void Finish() {
    if (finished_) return;
    finished_ = true;
    CefQuitMessageLoop();
  }

  app::CefHost* host_ = nullptr;
  std::string url_;
  std::string current_url_;
  std::string error_;
  int error_code_ = 0;
  CefRefPtr<CefBrowser> browser_;
  int frames_painted_ = 0;
  int first_paint_ms_ = -1;
  int load_ms_ = -1;
  bool loaded_ = false;
  bool loading_ = false;
  bool failed_ = false;
  bool finished_ = false;
  IMPLEMENT_REFCOUNTING(ProbeClient);
};

/// Safety net: quits the loop when the probe never finishes.
class TimeoutTask : public CefTask {
 public:
  explicit TimeoutTask(CefRefPtr<ProbeClient> client) : client_(client) {}
  void Execute() override {
    core::log::Warn("smoke", "probe timed out - quitting the message loop");
    CefQuitMessageLoop();
  }

 private:
  CefRefPtr<ProbeClient> client_;
  IMPLEMENT_REFCOUNTING(TimeoutTask);
};

}  // namespace

ProbeResult RunHeadlessProbe(app::CefHost* host, const std::string& url, int timeout_seconds) {
  ProbeResult result;
  result.url = url;
  const int started = NowMs();

  CefRefPtr<ProbeClient> client = new ProbeClient(host, url);

  CefWindowInfo window_info;
  window_info.SetAsWindowless(kNullWindowHandle);
  window_info.windowless_rendering_enabled = true;
  window_info.bounds = CefRect(0, 0, kProbeWidth, kProbeHeight);
  window_info.shared_texture_enabled = false;

  CefBrowserSettings browser_settings;
  browser_settings.windowless_frame_rate = 30;
  browser_settings.background_color = 0xFF101318;

  if (!CefBrowserHost::CreateBrowser(window_info, client, url, browser_settings, nullptr, nullptr)) {
    result.error = "CreateBrowser failed: the renderer process could not be started";
    result.exit_code = 2;
    return result;
  }

  CefPostDelayedTask(TID_UI, new TimeoutTask(client), static_cast<int64_t>(timeout_seconds) * 1000);
  CefRunMessageLoop();  // the probe quits this loop from the UI thread

  client->Fill(&result);
  result.startup_ms = NowMs() - started;

  // Real memory numbers for the whole tree, including the renderer we just used.
  app::MetricsSampler sampler;
  sampler.SetSystemProfile(host != nullptr ? host->system_profile() : core::SystemProfile{});
  core::MemorySnapshot snapshot;
  snapshot.total_mb = host != nullptr ? host->system_profile().total_ram_mb : 0;
  snapshot.available_mb = host != nullptr ? host->system_profile().available_ram_mb : 0;
  snapshot.tab_count = 1;
  sampler.set_startup_ms(result.startup_ms);
  sampler.Tick(snapshot, host != nullptr ? core::GetModeInfo(host->current_mode()).id : "unknown");
  result.telemetry = sampler.RenderOneLineStatus();
  return result;
}

}  // namespace browser
}  // namespace browserx
