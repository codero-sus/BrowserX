// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// browserx-pages — renders every internal page to out/pages/<host>.html.
//
//   make pages
//
// The pages are produced by the *same* function the browser uses
// (web::RenderPage in src/web/internal_pages.cc), with data from the real engine:
// the mode plans come from core::BuildPlan on the detected hardware profile, the
// catalogue from the real switch catalogue. Two things cannot exist outside a
// running browser - the live tab list and the version string - so the session data
// here is clearly a sample and the version comes from the build.
//
// This is what makes the UI reviewable without a CEF build: `make preview` serves
// these files, so the browser's own pages can be opened and clicked in any browser,
// on any machine, before the first `make run`.

#include <cstdio>
#include <cstring>
#include <string>

#include "core/chromium_catalog.h"
#include "core/mode_engine.h"
#include "core/report.h"
#include "core/system_profile.h"
#include "web/internal_pages.h"

#ifndef BROWSERX_VERSION
#define BROWSERX_VERSION "dev"
#endif

namespace {

/// A tab list that does not exist yet. Written here, next to its use, so nobody can
/// mistake it for live data: the preview labels this frame as sample session data.
const char* const kSampleSession = R"JSON({
  "version": 1,
  "active_id": 2,
  "next_id": 4,
  "tabs": [
    { "id": 1, "url": "https://example.com/docs", "title": "Documentation", "group": "",
      "pinned": true, "muted": false, "sleeping": true, "zoom": 100, "last_active_ms": 42000 },
    { "id": 2, "url": "browserx://telemetry", "title": "Telemetry", "group": "",
      "pinned": false, "muted": false, "sleeping": false, "zoom": 100, "last_active_ms": 1500 },
    { "id": 3, "url": "https://news.example/article", "title": "A long article", "group": "reading",
      "pinned": false, "muted": true, "sleeping": true, "zoom": 110, "last_active_ms": 61000 }
  ]
})JSON";

bool WriteFile(const std::string& path, const std::string& contents) {
  std::FILE* file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) {
    std::fprintf(stderr, "browserx-pages: cannot write %s\n", path.c_str());
    return false;
  }
  const size_t written = std::fwrite(contents.data(), 1, contents.size(), file);
  const int closed = std::fclose(file);
  return written == contents.size() && closed == 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::string output_dir = "out/pages";
  bool with_session = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--with-sample-session") == 0) {
      with_session = true;
    } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
      std::printf("\n  browserx-pages — write every internal page as a file\n\n"
                  "  usage: browserx-pages [output-dir] [--with-sample-session]\n\n"
                  "  Rendered by the same code the browser uses, with live engine data.\n"
                  "  --with-sample-session fills the tab list with a sample so the\n"
                  "  telemetry and session pages have something to show.\n\n");
      return 0;
    } else if (output_dir == "out/pages") {
      output_dir = argv[i];
    }
  }

  const browserx::core::SystemProfile profile = browserx::core::DetectSystemProfile();

  browserx::web::PageData data;
  data.version = BROWSERX_VERSION;
  const browserx::core::ModeInfo& active = browserx::core::GetModeInfo(browserx::core::Mode::kUltra);
  data.active_mode_id = active.id != nullptr ? active.id : "ultra";
  data.active_mode_label = active.label != nullptr ? active.label : "ULTRA";
  data.profile_report = browserx::core::DescribeProfile(profile);

  browserx::core::EngineContext context;
  context.profile = &profile;
  const browserx::core::ModePlan active_plan =
      browserx::core::BuildPlan(browserx::core::Mode::kUltra, context);
  data.runtime_summary = browserx::core::report::RenderRuntimePolicySummary(active_plan.runtime);
  data.plan_text = active_plan.ToReport();
  data.catalog_report = browserx::core::RenderCatalogReport(/*include_removed=*/false);

  std::vector<browserx::core::ModePlan> plans;
  for (const browserx::core::ModeInfo& info : browserx::core::AllModes()) {
    plans.push_back(browserx::core::BuildPlan(info.mode, context));
  }
  data.plans_json = browserx::core::report::RenderPlansJson(plans, profile);
  if (with_session) data.session_json = kSampleSession;

  const std::string mkdir = "mkdir -p '" + output_dir + "'";
  if (std::system(mkdir.c_str()) != 0) {
    std::fprintf(stderr, "browserx-pages: cannot create %s\n", output_dir.c_str());
    return 1;
  }

  size_t written = 0;
  for (const browserx::web::PageKind kind :
       {browserx::web::PageKind::kNewTab, browserx::web::PageKind::kModes,
        browserx::web::PageKind::kTelemetry, browserx::web::PageKind::kSettings,
        browserx::web::PageKind::kShortcuts, browserx::web::PageKind::kCatalog,
        browserx::web::PageKind::kSession, browserx::web::PageKind::kAbout,
        browserx::web::PageKind::kFind}) {
    const std::string path = output_dir + "/" + browserx::web::PageName(kind) + ".html";
    if (!WriteFile(path, browserx::web::RenderPage(kind, data))) return 1;
    std::printf("  wrote %s\n", path.c_str());
    ++written;
  }
  // The benchmark is its own document, and it is already generated elsewhere; copying
  // it here keeps every page in one directory for the preview to serve.
  std::printf("\n  %zu pages written from the same renderer the browser uses\n", written);
  std::printf("  BrowserX by codero-sus — BrowserX Personal-Use License: personal,\n"
              "  non-commercial use only; no modification; no redistribution or resale.\n\n");
  return 0;
}
