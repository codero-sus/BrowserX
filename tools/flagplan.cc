// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// browserx-flagplan — prints exactly which Chromium switches BrowserX would
// apply for every mode, with the reason and provenance for each one.
//
//   make bench                       # all modes, this machine's detected profile
//   make bench ARGS="gaming"         # one mode
//   make bench ARGS="--diff ultra low-ram"
//   make bench ARGS="--catalog"      # the full verified switch catalogue
//   make bench ARGS="--json"         # machine-readable plan (CI-friendly)
//
// This tool links only against src/core (no CEF), so it runs anywhere - which
// is the point: the optimisation claims are inspectable without building the
// browser.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/chromium_catalog.h"
#include "core/format.h"
#include "core/json.h"
#include "core/log.h"
#include "core/mode_engine.h"
#include "core/report.h"
#include "core/system_profile.h"

namespace {

using browserx::core::EngineContext;
using browserx::core::GetModeInfo;
using browserx::core::Mode;
using browserx::core::ModePlan;
using browserx::core::ParseMode;
using browserx::core::RuntimeOverrides;
using browserx::core::SystemProfile;

void PrintUsage() {
  std::printf(
      "\n  browserx-flagplan — Chromium switch plan inspector\n\n"
      "  usage: browserx-flagplan [mode] [options]\n\n"
      "    mode              any mode id: ultra | gaming | high-performance | low-ram | eco |\n"
      "                      super-internet | download | streaming\n"
      "    --all             print every mode (default when no mode is given)\n"
      "    --diff A B        compare the switch plans of two modes\n"
      "    --catalog         print the verified switch catalogue\n"
      "    --removed         include removed/superseded switches in --catalog\n"
      "    --json            emit JSON instead of text\n"
      "    --advanced=off    simulate settings with advanced optimisations disabled\n"
      "    --lab             simulate settings with lab flags enabled\n"
      "    --cache-mb N      override the HTTP cache budget\n"
      "    --renderers N     override the renderer process limit\n"
      "    --profile         print the detected system profile and exit\n"
      "    --quiet           only the resolved command line\n\n");
}

}  // namespace

int main(int argc, char** argv) {
  // The tool's own output is the artifact: keep the log out of the way unless
  // something is wrong.
  browserx::core::Logger::Get().SetLevel(browserx::core::LogLevel::kError);

  std::vector<std::string> modes;
  bool all = false;
  bool json = false;
  bool quiet = false;
  bool advanced = true;
  bool lab = false;
  bool show_profile = false;
  bool show_catalog = false;
  bool include_removed = false;
  std::string diff_a;
  std::string diff_b;
  RuntimeOverrides overrides;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      PrintUsage();
      return 0;
    } else if (arg == "--all") {
      all = true;
    } else if (arg == "--json") {
      json = true;
    } else if (arg == "--quiet") {
      quiet = true;
    } else if (arg == "--lab") {
      lab = true;
    } else if (arg == "--profile") {
      show_profile = true;
    } else if (arg == "--catalog") {
      show_catalog = true;
    } else if (arg == "--removed") {
      include_removed = true;
    } else if (arg == "--advanced=off") {
      advanced = false;
    } else if (arg == "--advanced=on") {
      advanced = true;
    } else if (arg == "--cache-mb" && i + 1 < argc) {
      overrides.cache_size_mb = std::atoi(argv[++i]);
    } else if (arg == "--renderers" && i + 1 < argc) {
      overrides.renderer_process_limit = std::atoi(argv[++i]);
    } else if (arg == "--diff" && i + 2 < argc) {
      diff_a = argv[++i];
      diff_b = argv[++i];
    } else if (!arg.empty() && arg[0] != '-') {
      modes.push_back(arg);
    } else {
      std::printf("unknown option: %s\n", arg.c_str());
      PrintUsage();
      return 2;
    }
  }

  overrides.allow_advanced = advanced;
  overrides.allow_lab = lab;

  const SystemProfile profile = browserx::core::DetectSystemProfile();

  if (show_profile) {
    std::printf("%s\n", browserx::core::report::RenderProfileReport(profile).c_str());
    return 0;
  }

  if (show_catalog) {
    if (json) {
      // Structured catalogue: the preview UI and the diagnostics panel read this
      // to show where every switch was verified. Removed switches are included
      // only when asked for, and always carry status "removed".
      browserx::core::json::Value::Array entries;
      for (const browserx::core::SwitchEntry& entry : browserx::core::AllSwitches()) {
        const browserx::core::Provenance provenance = entry.provenance;
        const bool removed = provenance == browserx::core::Provenance::kRemoved;
        if (removed && !include_removed) continue;
        browserx::core::json::Value::Object item;
        item["name"] = browserx::core::json::Value(entry.id);
        item["status"] = browserx::core::json::Value(browserx::core::ProvenanceLabel(provenance));
        item["scope"] = browserx::core::json::Value(browserx::core::ScopeLabel(entry.scope));
        item["risk"] = browserx::core::json::Value(browserx::core::RiskLabel(entry.risk));
        item["provenance"] = browserx::core::json::Value(entry.source);
        item["summary"] = browserx::core::json::Value(entry.summary);
        item["requires_value"] = browserx::core::json::Value(entry.requires_value);
        item["applicable"] = browserx::core::json::Value(browserx::core::IsApplicable(entry));
        entries.emplace_back(item);
      }
      browserx::core::json::Value::Array features;
      for (const browserx::core::FeatureEntry& entry : browserx::core::AllFeatures()) {
        if (entry.provenance == browserx::core::Provenance::kRemoved && !include_removed) continue;
        browserx::core::json::Value::Object item;
        item["name"] = browserx::core::json::Value(entry.name);
        item["status"] = browserx::core::json::Value(browserx::core::ProvenanceLabel(entry.provenance));
        item["provenance"] = browserx::core::json::Value(entry.source);
        item["summary"] = browserx::core::json::Value(entry.summary);
        item["kind"] = browserx::core::json::Value("feature");
        features.emplace_back(item);
      }
      const browserx::core::CatalogStats stats = browserx::core::GetCatalogStats();
      browserx::core::json::Value::Object summary;
      summary["switches_total"] = browserx::core::json::Value(static_cast<int64_t>(stats.switches_total));
      summary["switches_verified"] = browserx::core::json::Value(static_cast<int64_t>(stats.switches_verified));
      summary["switches_removed"] = browserx::core::json::Value(static_cast<int64_t>(stats.switches_removed));
      summary["features_total"] = browserx::core::json::Value(static_cast<int64_t>(stats.features_total));
      browserx::core::json::Value::Object output;
      output["switches"] = browserx::core::json::Value(std::move(entries));
      output["features"] = browserx::core::json::Value(std::move(features));
      output["stats"] = browserx::core::json::Value(std::move(summary));
      std::printf("%s\n", browserx::core::json::Serialize(browserx::core::json::Value(std::move(output)), 0).c_str());
      return 0;
    }
    std::printf("\n%s\n", browserx::core::RenderCatalogReport(include_removed).c_str());
    std::printf("  Evidence: https://source.chromium.org/search?q=<switch-name>&ss=chromium\n\n");
    return 0;
  }

  EngineContext context;
  context.profile = &profile;
  context.overrides = overrides;

  if (!diff_a.empty() || !diff_b.empty()) {
    Mode mode_a = Mode::kUltra;
    Mode mode_b = Mode::kLowRam;
    if (!ParseMode(diff_a, &mode_a) || !ParseMode(diff_b, &mode_b)) {
      std::printf("could not parse modes to diff\n");
      return 2;
    }
    const browserx::core::ModePlan plan_a = browserx::core::BuildPlan(mode_a, context);
    const browserx::core::ModePlan plan_b = browserx::core::BuildPlan(mode_b, context);
    std::printf("%s", browserx::core::report::RenderPlanDiff(plan_a, plan_b).c_str());
    return 0;
  }

  std::vector<Mode> selected;
  if (all || modes.empty()) {
    for (const auto& info : browserx::core::AllModes()) selected.push_back(info.mode);
  } else {
    for (const std::string& text : modes) {
      Mode mode = Mode::kUltra;
      if (!ParseMode(text, &mode)) {
        std::printf("unknown mode: %s\n", text.c_str());
        return 2;
      }
      selected.push_back(mode);
    }
  }

  if (json) {
    std::vector<browserx::core::ModePlan> plans;
    for (const Mode mode : selected) {
      const browserx::core::ModePlan plan = browserx::core::BuildPlan(mode, context);
      std::string problem;
      if (!browserx::core::ValidatePlan(plan, &problem)) {
        std::fprintf(stderr, "  !! invalid plan in --json output: %s\n", problem.c_str());
        return 2;
      }
      plans.push_back(plan);
    }
    std::printf("%s", browserx::core::report::RenderPlansJson(plans, profile).c_str());
    return 0;
  }

  if (modes.empty() && !all) {
    // No arguments at all: behave like `--all`, but print a short header.
    std::printf("\n  BrowserX — Chromium switch plans (verified %s against chromium@%s)\n",
                browserx::core::kCatalogVerifiedOn, browserx::core::kCatalogChromiumRef);
  }
  // Every plan is validated before it is printed: a switch that is not in the verified
  // catalogue, or a plan that contradicts itself, fails loudly here instead of being
  // applied by Chromium and silently ignored (which is the one failure mode a browser
  // making performance claims cannot afford).
  bool invalid = false;
  for (const Mode mode : selected) {
    const ModePlan plan = browserx::core::BuildPlan(mode, context);
    std::string problem;
    if (!browserx::core::ValidatePlan(plan, &problem)) {
      std::fprintf(stderr, "  !! %s: invalid plan: %s\n", plan.mode == Mode::kUltra ? "ultra" : "",
                   problem.c_str());
      invalid = true;
    }
  }
  if (invalid) return 2;
  for (const Mode mode : selected) {
    const ModePlan plan = browserx::core::BuildPlan(mode, context);
    std::printf("%s", browserx::core::report::RenderPlanText(plan, profile, quiet).c_str());
  }
  if (!json && !quiet) std::printf("%s", browserx::core::report::RenderCatalogFooter().c_str());
  return 0;
}
