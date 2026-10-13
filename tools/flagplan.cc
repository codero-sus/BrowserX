// mybrowser-flagplan — prints exactly which Chromium switches MyBrowser would
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
#include "core/log.h"
#include "core/mode_engine.h"
#include "core/report.h"
#include "core/system_profile.h"

namespace {

using mybrowser::core::EngineContext;
using mybrowser::core::GetModeInfo;
using mybrowser::core::Mode;
using mybrowser::core::ModePlan;
using mybrowser::core::ParseMode;
using mybrowser::core::RuntimeOverrides;
using mybrowser::core::SystemProfile;

void PrintUsage() {
  std::printf(
      "\n  mybrowser-flagplan — Chromium switch plan inspector\n\n"
      "  usage: mybrowser-flagplan [mode] [options]\n\n"
      "    mode              ultra | gaming | high-performance | low-ram | eco\n"
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
  mybrowser::core::Logger::Get().SetLevel(mybrowser::core::LogLevel::kError);

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

  const SystemProfile profile = mybrowser::core::DetectSystemProfile();

  if (show_profile) {
    std::printf("%s\n", mybrowser::core::report::RenderProfileReport(profile).c_str());
    return 0;
  }

  if (show_catalog) {
    std::printf("\n%s\n", mybrowser::core::RenderCatalogReport(include_removed).c_str());
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
    const mybrowser::core::ModePlan plan_a = mybrowser::core::BuildPlan(mode_a, context);
    const mybrowser::core::ModePlan plan_b = mybrowser::core::BuildPlan(mode_b, context);
    std::printf("%s", mybrowser::core::report::RenderPlanDiff(plan_a, plan_b).c_str());
    return 0;
  }

  std::vector<Mode> selected;
  if (all || modes.empty()) {
    for (const auto& info : mybrowser::core::AllModes()) selected.push_back(info.mode);
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
    std::vector<mybrowser::core::ModePlan> plans;
    for (const Mode mode : selected) plans.push_back(mybrowser::core::BuildPlan(mode, context));
    std::printf("%s", mybrowser::core::report::RenderPlansJson(plans, profile).c_str());
    return 0;
  }

  if (modes.empty() && !all) {
    // No arguments at all: behave like `--all`, but print a short header.
    std::printf("\n  MyBrowser — Chromium switch plans (verified %s against chromium@%s)\n",
                mybrowser::core::kCatalogVerifiedOn, mybrowser::core::kCatalogChromiumRef);
  }
  for (const Mode mode : selected) {
    const ModePlan plan = mybrowser::core::BuildPlan(mode, context);
    std::printf("%s", mybrowser::core::report::RenderPlanText(plan, profile, quiet).c_str());
  }
  if (!json && !quiet) std::printf("%s", mybrowser::core::report::RenderCatalogFooter().c_str());
  return 0;
}
