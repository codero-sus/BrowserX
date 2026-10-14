// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — entry point.
//
// One binary serves every role CEF needs:
//   * helper processes (renderer, GPU, utility) start with --type=... and are
//     dispatched to CefExecuteProcess() before anything else happens;
//   * the browser process parses the CLI, loads settings, resolves the mode plan,
//     initialises CEF, then runs either the full UI or a headless probe.
//
// The order below is deliberate: everything that must happen before Chromium
// starts (switches) happens first, and everything that only makes sense once
// Chromium exists (windows, request contexts) happens last.

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/wrapper/cef_helpers.h"

#include "app/app_paths.h"
#include "app/cef_host.h"
#include "app/cli.h"
#include "app/single_instance.h"
#include "browser/browser_shell.h"
#include "browser/headless_probe.h"
#include "core/chromium_catalog.h"
#include "core/format.h"
#include "core/log.h"
#include "core/mode_engine.h"
#include "core/report.h"
#include "core/system_profile.h"
#include "web/bench_page.h"

namespace browserx {
namespace app {
CefRefPtr<CefApp> CreateApplication();
}
}  // namespace browserx

namespace {

using browserx::app::CliAction;
using browserx::app::LaunchOptions;

/// True for helper-process invocations ("BrowserX --type=renderer ...").
bool IsHelperProcess(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i] != nullptr && std::strncmp(argv[i], "--type=", 7) == 0) return true;
  }
  return false;
}

void ApplyPathOverrides(const LaunchOptions& options, browserx::app::Paths* paths) {
  if (!options.profile_dir.empty()) {
    paths->profile_dir = options.profile_dir;
    paths->sessions_dir = browserx::app::JoinPath(paths->profile_dir, "sessions");
    paths->logs_dir = browserx::app::JoinPath(paths->profile_dir, "logs");
    paths->code_cache_dir = browserx::app::JoinPath(paths->profile_dir, "code_cache");
  }
  if (!options.cache_dir.empty()) paths->cache_dir = options.cache_dir;
  if (!options.downloads_dir.empty()) paths->downloads_dir = options.downloads_dir;
  browserx::app::EnsureDirectory(paths->profile_dir);
  browserx::app::EnsureDirectory(paths->cache_dir);
  browserx::app::EnsureDirectory(paths->sessions_dir);
  browserx::app::EnsureDirectory(paths->logs_dir);
}

void SetUpLogging(const LaunchOptions& options, const browserx::app::Paths& paths) {
  using browserx::core::Logger;
  using browserx::core::LogLevel;
  LogLevel level = LogLevel::kInfo;
  if (!options.log_level.empty()) browserx::core::ParseLogLevel(options.log_level, &level);
  Logger::Get().SetLevel(level);
  Logger::Get().SetEchoToStderr(level <= LogLevel::kInfo);
}

/// Builds the plan for a diagnostic request (same inputs the browser would use).
browserx::core::ModePlan PlanForDiagnostics(const LaunchOptions& options,
                                             const browserx::core::SystemProfile& profile,
                                             const browserx::core::Settings& settings) {
  browserx::core::EngineContext context = settings.MakeContext(options.mode, &profile);
  context.overrides.allow_advanced = options.advanced || context.overrides.allow_advanced;
  context.overrides.allow_lab = options.lab;
  if (options.renderer_limit > 0) context.overrides.renderer_process_limit = options.renderer_limit;
  if (options.cache_mb > 0) context.overrides.cache_size_mb = options.cache_mb;
  if (options.target_fps > 0) context.overrides.target_fps = options.target_fps;
  if (!options.gl_backend.empty()) context.gl_backend = options.gl_backend;
  return browserx::core::BuildPlan(options.mode, context);
}

/// Writes the standalone benchmark page and prints how to use it.
///
/// The point of writing the file (rather than only serving browserx://bench) is that the
/// exact same bytes can be opened in another browser: the comparison is only worth
/// reading when both browsers ran the same code, offline, with no network involved.
int WriteBenchPage(const LaunchOptions& options) {
  const std::string path =
      options.bench_page_path.empty() ? std::string("browserx-bench.html") : options.bench_page_path;
  const std::string document = browserx::web::BenchPageDocument();

  std::FILE* file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) {
    std::fprintf(stderr, "could not write %s\n", path.c_str());
    return 1;
  }
  const size_t written = std::fwrite(document.data(), 1, document.size(), file);
  const int closed = std::fclose(file);
  if (written != document.size() || closed != 0) {
    std::fprintf(stderr, "could not write all of %s\n", path.c_str());
    return 1;
  }

  std::printf("\n  wrote %s (%.1f KB, self-contained - no network, no external files)\n", path.c_str(),
              static_cast<double>(document.size()) / 1024.0);
  std::printf("\n  How to compare two browsers honestly:\n");
  std::printf("    1. open this file in BrowserX and in the other browser\n");
  std::printf("       (the page is also always available at browserx://bench)\n");
  std::printf("    2. press Run in both, on the same machine, one at a time\n");
  std::printf("    3. compare the score line and the per-test numbers\n");
  std::printf("    4. make run RUN_ARGS=\"--mode=high-performance\"  does the same page with\n");
  std::printf("       BrowserX tuned instead of default, which is the number worth quoting\n");
  std::printf("\n  BrowserX by codero-sus - https://github.com/codero-sus/BrowserX\n");
  std::printf("  BrowserX Personal-Use License: personal, non-commercial use only;\n");
  std::printf("  no modification; no redistribution or resale. https://the-codero.web.app\n\n");
  return 0;
}

int RunDiagnostics(const LaunchOptions& options, const browserx::app::Paths& paths) {
  const browserx::core::SystemProfile profile = browserx::core::DetectSystemProfile();

  switch (options.action) {
    case CliAction::kPrintProfile: {
      std::printf("%s\n", browserx::core::DescribeProfile(profile).c_str());
      return 0;
    }
    case CliAction::kListModes: {
      std::printf("%s", browserx::core::report::RenderModeList(profile).c_str());
      return 0;
    }
    case CliAction::kPrintCatalog: {
      std::printf("%s", browserx::core::RenderCatalogReport(/*include_removed=*/false).c_str());
      return 0;
    }
    case CliAction::kWriteBenchPage:
      return WriteBenchPage(options);
    case CliAction::kPrintFlagPlan: {
      browserx::core::Settings settings = browserx::core::Settings::Defaults();
      settings.startup_mode = options.mode;
      // Only the modes the user asked for; "all" or nothing means every mode.
      std::vector<browserx::core::Mode> modes;
      if (options.flag_plan_modes.empty() || options.flag_plan_modes == "all") {
        for (const browserx::core::ModeInfo& info : browserx::core::AllModes()) {
          modes.push_back(info.mode);
        }
      } else {
        browserx::core::Mode parsed{};
        if (!browserx::core::ParseMode(options.flag_plan_modes, &parsed)) {
          std::fprintf(stderr, "unknown mode '%s'\n", options.flag_plan_modes.c_str());
          return 2;
        }
        modes.push_back(parsed);
      }
      std::printf("\n  BrowserX — Chromium switch plan (catalogue verified %s against chromium@%s)\n",
                  browserx::core::kCatalogVerifiedOn, browserx::core::kCatalogChromiumRef);
      for (const browserx::core::Mode mode : modes) {
        LaunchOptions plan_options = options;
        plan_options.mode = mode;
        const browserx::core::ModePlan plan = PlanForDiagnostics(plan_options, profile, settings);
        std::printf("%s", browserx::core::report::RenderPlanText(plan, profile, false).c_str());
      }
      std::printf("%s", browserx::core::report::RenderCatalogFooter().c_str());
      return 0;
    }
    case CliAction::kDoctor: {
      std::string report;
      report += "\n  BrowserX doctor\n";
      report += "  =====================================================================\n";
      report += "  version      : " + browserx::app::VersionBanner() + "\n";
      report += "  profile dir  : " + paths.profile_dir + "\n";
      report += "  cache dir    : " + paths.cache_dir + "\n";
      report += "  logs dir     : " + paths.logs_dir + "\n";
      report += "  settings     : " + paths.settings_file() + "\n";
      report += "  portable     : " + std::string(paths.portable ? "yes" : "no") + "\n";
      report += browserx::core::DescribeProfile(profile);
      browserx::core::Settings settings = browserx::core::Settings::Defaults();
      settings.startup_mode = options.mode;
      const browserx::core::ModePlan plan = PlanForDiagnostics(options, profile, settings);
      report += "\n  resolved mode: " + browserx::core::DescribePlanOneLine(plan) + "\n";
      report += "  boot switches: " + std::to_string(plan.boot.switches().size()) + "\n";
      report += "  runtime      : " + browserx::core::report::RenderRuntimePolicySummary(plan.runtime) + "\n";
      report += browserx::core::report::RenderCatalogFooter();
      std::printf("%s", report.c_str());
      return 0;
    }
    default:
      return -1;  // not a diagnostics action
  }
}

CefSettings BuildCefSettings(const LaunchOptions& options, const browserx::app::Paths& paths) {
  CefSettings settings;
  settings.no_sandbox = !options.sandbox;
  settings.multi_threaded_message_loop = false;  // we drive CefRunMessageLoop ourselves
  settings.external_message_pump = false;
  settings.windowless_rendering_enabled =
      options.headless || options.action == CliAction::kSmokeTest ||
      options.action == CliAction::kBenchmark;
  settings.command_line_args_disabled = false;
  settings.persist_session_cookies = true;
  settings.log_severity = LOGSEVERITY_WARNING;  // our own logger carries the detail
  settings.background_color = 0xFF101318;
  settings.remote_debugging_port = 0;
  settings.uncaught_exception_stack_size = 64;

  CefString(&settings.root_cache_path).FromString(paths.profile_dir);
  CefString(&settings.cache_path).FromString(paths.cache_dir);
  CefString(&settings.log_file).FromString(browserx::app::JoinPath(paths.logs_dir, "cef.log"));
  CefString(&settings.user_agent_product)
      .FromString(std::string("BrowserX/") + BROWSERX_VERSION);
  const std::string resources = browserx::app::JoinPath(paths.executable_dir, "Resources");
  if (browserx::app::DirectoryExists(resources)) {
    CefString(&settings.resources_dir_path).FromString(resources);
    CefString(&settings.locales_dir_path).FromString(browserx::app::JoinPath(resources, "locales"));
  }
  return settings;
}

std::string BenchmarkUrl(const browserx::app::Paths& paths) {
  // Self-contained page: it exercises layout, paint and compositing without
  // needing the network, so `--benchmark` works in CI and offline.
  (void)paths;
  return "data:text/html,<html><body style='margin:0;background:%23101318;color:%23e6e8ee;"
         "font:16px sans-serif'><h1 style='padding:24px'>BrowserX benchmark</h1>"
         "<p style='padding:0 24px'>Compositing, raster and memory measurement page.</p>"
         "<div style='height:2000px;background:linear-gradient(%23101318,%235ac8fa)'></div>"
         "</body></html>";
}

int RunSmokeOrBenchmark(browserx::app::CefHost* host, const LaunchOptions& options,
                        const browserx::app::Paths& paths) {
  const std::string url = options.url.empty() ? BenchmarkUrl(paths) : options.url;
  const int timeout = options.action == CliAction::kBenchmark ? 30 : 20;
  const browserx::browser::ProbeResult result =
      browserx::browser::RunHeadlessProbe(host, url, timeout);

  std::printf("\n");
  std::printf("  %s\n", browserx::app::VersionBanner().c_str());
  std::printf("  mode        : %s\n", browserx::core::GetModeInfo(host->current_mode()).label);
  std::printf("  url         : %s\n", result.url.c_str());
  std::printf("  startup     : %d ms\n", result.startup_ms);
  std::printf("  first paint : %d ms\n", result.first_paint_ms);
  std::printf("  load        : %d ms\n", result.load_ms);
  std::printf("  frames      : %d\n", result.frames);
  std::printf("  %s\n", result.telemetry.c_str());
  if (!result.ok) std::printf("  error       : %s\n", result.error.c_str());
  std::printf("\n  result: %s\n\n", result.ok ? "PASS" : "FAIL");
  return result.exit_code;
}

}  // namespace

int main(int argc, char** argv) {
  CefMainArgs main_args(argc, argv);

  // 1. Helper processes must be dispatched before we touch anything else.
  if (IsHelperProcess(argc, argv)) {
    return CefExecuteProcess(main_args, browserx::app::CreateApplication(), nullptr);
  }

  // 2. Our command line.
  const LaunchOptions options = browserx::app::ParseCli(argc, argv);
  if (!options.parse_error.empty()) {
    std::fprintf(stderr, "BrowserX: %s\n\n%s", options.parse_error.c_str(),
                 browserx::app::CliUsage(argv[0]).c_str());
    return 2;
  }
  if (options.action == CliAction::kPrintHelp) {
    std::printf("%s", browserx::app::CliUsage(argv[0]).c_str());
    return 0;
  }
  if (options.action == CliAction::kPrintVersion) {
    std::printf("%s\n", browserx::app::VersionBanner().c_str());
    return 0;
  }

  // 3. Profile layout and logging.
  browserx::app::Paths paths = browserx::app::ResolvePaths();
  ApplyPathOverrides(options, &paths);
  SetUpLogging(options, paths);

  // 4. Diagnostics that do not need Chromium at all.
  const int diagnostics = RunDiagnostics(options, paths);
  if (diagnostics >= 0) return diagnostics;

  // 5. One instance per profile (unless the user insists). The lock is released
  //    by the OS if we crash, so a stale file can never block a launch.
  browserx::app::InstanceLock instance_lock;
  if (!options.allow_multiple) {
    int holder = 0;
    if (!instance_lock.Acquire(paths.profile_dir, &holder)) {
      std::fprintf(stderr,
                   "BrowserX: another instance is already using this profile%s.\n"
                   "  (pid %d holds %s)\n"
                   "  use --allow-multiple only if you know the profile is not in use.\n",
                   paths.portable ? " (portable mode)" : "", holder,
                   browserx::app::JoinPath(paths.profile_dir, "instance.lock").c_str());
      return 3;
    }
  }

  // 6. Settings + mode plan: everything that must exist before Chromium starts.
  browserx::app::CefHost& host = browserx::app::Host();
  host.Initialize(options, paths);
  host.set_primary_instance(!options.allow_multiple);

  // 7. Initialise CEF. Our CefApp applies the boot switches from inside
  //    OnBeforeCommandLineProcessing, which CefInitialize invokes.
  const CefSettings cef_settings = BuildCefSettings(options, paths);
  if (!CefInitialize(main_args, cef_settings, browserx::app::CreateApplication(), nullptr)) {
    const int code = CefGetExitCode();
    browserx::core::log::ErrorF("app", "CefInitialize failed (exit code %d); see %s", code,
                                 browserx::app::JoinPath(paths.logs_dir, "cef.log").c_str());
    return code == 0 ? 4 : code;
  }
  browserx::core::log::InfoF("app", "CEF initialised (%s), %zu boot switches applied",
                              browserx::app::VersionBanner().c_str(),
                              host.applied_boot_flags().size());
  host.settings().RecordAppliedPlan(browserx::core::GetModeInfo(host.current_mode()).id,
                                    host.applied_boot_flags());
  host.settings().SaveIfDirty();

  // 8. Either the full UI or a headless probe.
  int exit_code = 0;
  if (options.action == CliAction::kSmokeTest || options.action == CliAction::kBenchmark) {
    exit_code = RunSmokeOrBenchmark(&host, options, paths);
  } else {
    std::unique_ptr<browserx::BrowserShell> shell =
        browserx::browser::CreateBrowserShell(&host);
    host.SetShell(shell.get());
    CefRunMessageLoop();
    shell->OnShuttingDown();
    host.SetShell(nullptr);
  }

  CefShutdown();
  browserx::core::log::Info("app", "shutdown complete");
  return exit_code;
}
