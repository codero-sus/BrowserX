// BrowserX — headless start-up probe.
//
// `--smoke-test` and `--benchmark` must work on machines with no display (CI,
// containers, SSH sessions), so they do not open a window at all: the browser is
// created in CEF's off-screen-rendering mode. The probe loads one page, waits for
// the first painted frame, then reports real numbers (frame latency, memory of
// the whole process tree, resolved mode) and exits with a meaningful status.
//
// Exit codes:  0 = page loaded and painted, 1 = load error, 2 = timeout/failure.

#ifndef BROWSERX_BROWSER_HEADLESS_PROBE_H_
#define BROWSERX_BROWSER_HEADLESS_PROBE_H_

#include <string>

#include "app/cef_host.h"

namespace browserx {
namespace browser {

struct ProbeResult {
  bool ok = false;
  bool painted = false;
  bool load_finished = false;
  int exit_code = 2;
  int startup_ms = 0;
  int first_paint_ms = 0;
  int load_ms = 0;
  int frames = 0;
  std::string url;
  std::string title;
  std::string error;
  std::string telemetry;
};

/// Runs the probe and returns when it finished (or timed out).
ProbeResult RunHeadlessProbe(app::CefHost* host, const std::string& url, int timeout_seconds);

}  // namespace browser
}  // namespace browserx

#endif  // BROWSERX_BROWSER_HEADLESS_PROBE_H_
