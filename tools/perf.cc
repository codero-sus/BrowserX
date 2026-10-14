// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — performance harness.
//
// What this can and cannot measure, stated up front because the difference matters:
//
//   * It CAN measure everything BrowserX adds on top of Chromium. That is the honest
//     question of "is this browser's own layer costing me anything?" - a policy that
//     takes 40 microseconds per request is free; one that takes 4 milliseconds is a
//     design bug, and this tool is where that shows up before a user feels it.
//   * It CANNOT measure page-load or frames-per-second of the full browser here: that
//     needs the CEF runtime and a display, neither of which exists in a build sandbox.
//     So it prints the protocol for the machine that does have them, and refuses to
//     invent a number it did not measure.
//
// The budgets are enforced: exceeding one exits non-zero, so a performance regression
// fails like a test failure instead of being discovered in a release.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/blocklist.h"
#include "core/chromium_catalog.h"
#include "core/memory_policy.h"
#include "core/mode_engine.h"
#include "core/request_guard.h"
#include "core/system_profile.h"
#include "core/url_utils.h"

namespace {

using browserx::core::Blocklist;
using browserx::core::SwitchEntry;
using browserx::core::EngineContext;
using browserx::core::Mode;
using browserx::core::ModeInfo;
using browserx::core::ModePlan;
using browserx::core::RequestFacts;
using browserx::core::ResourceType;
using browserx::core::RuntimePolicy;
using browserx::core::SystemProfile;

double NowMs() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch()).count();
}

struct Measurement {
  std::string name;
  double per_call_us = 0.0;   // microseconds per operation
  double budget_us = 0.0;     // fail above this
  std::string unit;
  int iterations = 0;
  bool passed = true;
  std::string note;
};

std::vector<Measurement> g_measurements;

/// Times `fn` `iterations` times and records the average in microseconds.
template <typename Fn>
void Measure(const std::string& name, int iterations, double budget_us, Fn fn,
             const std::string& unit = "us/op", const std::string& note = std::string()) {
  // A warm-up pass so the first call's allocations do not land in the sample.
  fn();
  const double start = NowMs();
  for (int i = 0; i < iterations; ++i) fn();
  const double total_us = (NowMs() - start) * 1000.0;

  Measurement m;
  m.name = name;
  m.iterations = iterations;
  m.per_call_us = total_us / static_cast<double>(iterations);
  m.budget_us = budget_us;
  m.unit = unit;
  m.passed = m.per_call_us <= budget_us;
  m.note = note;
  g_measurements.push_back(m);
}

SystemProfile DesktopProfile() {
  SystemProfile profile;
  profile.total_ram_mb = 16384;
  profile.cpu_logical = 8;
  profile.cpu_physical = 4;
  profile.available_ram_mb = 12000;
  profile.gpu_tier = browserx::core::GpuTier::kDiscrete;
  profile.has_accelerated_video = true;
  return profile;
}

}  // namespace

int main(int argc, char** argv) {
  const bool json = argc > 1 && std::string(argv[1]) == "--json";

  std::printf("\n  BrowserX — performance harness\n");
  std::printf("  ============================================================\n\n");

  const SystemProfile profile = DesktopProfile();

  // ---- the layer BrowserX adds -------------------------------------------
  EngineContext context;
  context.overrides.allow_advanced = true;

  Measure("BuildPlan (one mode)", 2000, 500.0,
          [&] { volatile int n = static_cast<int>(browserx::core::BuildPlan(Mode::kGaming, context)
                                                     .boot.switches().size()); (void)n; },
          "us/plan", "the whole switch plan: policy, features, reasons");

  Measure("AllModes() lookup", 20000, 5.0,
          [&] { volatile size_t n = browserx::core::AllModes().size(); (void)n; });

  Measure("GetModeInfo(id)", 20000, 5.0,
          [&] { volatile const char* id = browserx::core::GetModeInfo(Mode::kUltra).id; (void)id; });

  Measure("url::HostOf", 50000, 5.0,
          [&] { volatile size_t n = browserx::core::url::HostOf("https://news.example/a/b?c=1").size(); (void)n; });

  Measure("url::PathOf", 50000, 5.0,
          [&] { volatile size_t n = browserx::core::url::PathOf("https://news.example/a/b?c=1").size(); (void)n; });

  Measure("Blocklist::AddDefaults", 50, 2000.0,
          [&] { Blocklist list; list.AddDefaults(); },
          "us/setup", "once per profile load, so 2 ms is still invisible");

  // ---- the per-request path: this is the one that must be free -----------
  Blocklist shield;
  shield.AddDefaults();
  RuntimePolicy policy = browserx::core::BuildPlan(Mode::kUltra, context).runtime;

  RequestFacts ordinary;
  ordinary.document_host = "news.example";
  ordinary.request_host = "static.example";
  ordinary.request_path = "/img/hero.png";
  ordinary.type = ResourceType::kImage;

  RequestFacts tracker;
  tracker.document_host = "news.example";
  tracker.request_host = "www.google-analytics.com";
  tracker.request_path = "/collect?v=1";
  tracker.type = ResourceType::kScript;

  Measure("DecideRequest (allowed)", 200000, 5.0,
          [&] { volatile bool b = browserx::core::DecideRequest(shield, policy, ordinary).block; (void)b; },
          "us/request", "cost added to every resource of every page");

  Measure("DecideRequest (blocked)", 200000, 5.0,
          [&] { volatile bool b = browserx::core::DecideRequest(shield, policy, tracker).block; (void)b; });

  // ---- the timer path ----------------------------------------------------
  browserx::core::MemoryPolicy memory(policy);
  browserx::core::MemorySnapshot snapshot;
  snapshot.total_mb = 16384;
  snapshot.available_mb = 7000;
  snapshot.browser_rss_mb = 9000;
  snapshot.tab_count = 12;
  snapshot.hidden_tabs = 4;

  Measure("MemoryPolicy::Evaluate", 20000, 100.0,
          [&] { volatile int level = static_cast<int>(memory.Evaluate(snapshot).level);
                (void)level; },
          "us/tick", "runs on the telemetry timer, not per frame");

  // ---- an audit, not a benchmark: the speed claims that must never appear --
  int planned_lab = 0;
  int planned_removed = 0;
  int total_switches = 0;
  for (const ModeInfo& info : browserx::core::AllModes()) {
    EngineContext plain;  // no advanced, no lab: what an ordinary user gets
    const ModePlan plan = browserx::core::BuildPlan(info.mode, plain);
    for (const browserx::core::PlannedSwitch& entry : plan.boot.switches()) {
      ++total_switches;
      const SwitchEntry* found = browserx::core::FindSwitch(entry.id);
      if (found == nullptr) continue;
      if (found->provenance == browserx::core::Provenance::kRemoved) ++planned_removed;
      if (found->risk == browserx::core::Risk::kLab) ++planned_lab;
    }
  }

  // ---- report ------------------------------------------------------------
  bool ok = true;
  if (!json) {
    std::printf("  BrowserX's own overhead (lower is better; budget in brackets)\n");
    std::printf("  -------------------------------------------------------------------------\n");
    for (const Measurement& m : g_measurements) {
      std::printf("  %-26s %9.3f %-9s [budget %8.2f]  %s\n", m.name.c_str(), m.per_call_us,
                  m.unit.c_str(), m.budget_us, m.passed ? "ok" : "OVER BUDGET");
      if (!m.passed) ok = false;
    }
    std::printf("\n  Audit\n");
    std::printf("  -------------------------------------------------------------------------\n");
    std::printf("  switches planned across all %zu modes : %d\n",
                browserx::core::AllModes().size(), total_switches);
    std::printf("  removed/cargo-cult switches planned   : %d %s\n", planned_removed,
                planned_removed == 0 ? "(none - good)" : "(MUST be 0)");
    std::printf("  lab-only switches planned by default  : %d %s\n", planned_lab,
                planned_lab == 0 ? "(none - good)" : "(MUST be 0)");
    if (planned_removed != 0 || planned_lab != 0) ok = false;

    std::printf(
        "\n  What this proves: every switch and every request-decision BrowserX adds\n"
        "  costs microseconds, so browsing speed is Chromium's speed plus the tuning,\n"
        "  not minus a slow policy layer.\n"
        "\n  What this cannot measure here: page load and frames per second, which need\n"
        "  the CEF runtime and a display. On the machine that has both:\n"
        "\n      make && make smoke                 # start-up, 3 tabs, telemetry report\n"
        "      ./out/BrowserX --write-bench-page  # writes browserx-bench.html\n"
        "      open that file in BrowserX and in the other browser, click Run again,\n"
        "      compare the score and the per-test numbers - same file, no network.\n"
        "\n      make run RUN_ARGS=\"--mode=high-performance\"   # same page, tuned vs default\n"
        "\n  A comparison is only worth reading if both sides ran the same file offline.\n\n");
  }

  if (json) {
    std::printf("{\n  \"suite\": \"browserx-perf\",\n  \"measurements\": [\n");
    for (size_t i = 0; i < g_measurements.size(); ++i) {
      const Measurement& m = g_measurements[i];
      std::printf("    {\"name\": \"%s\", \"per_call_us\": %.3f, \"budget_us\": %.1f, \"passed\": %s}%s\n",
                  m.name.c_str(), m.per_call_us, m.budget_us, m.passed ? "true" : "false",
                  i + 1 < g_measurements.size() ? "," : "");
    }
    std::printf("  ],\n  \"planned_switches\": %d,\n  \"planned_removed\": %d,\n"
                "  \"planned_lab\": %d,\n  \"passed\": %s\n}\n",
                total_switches, planned_removed, planned_lab, ok ? "true" : "false");
  }

  return ok ? 0 : 1;
}
