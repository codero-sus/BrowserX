// MyBrowser — entry point.
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

namespace mybrowser {
namespace app {
CefRefPtr<CefApp> CreateApplication();
}
}  // namespace mybrowser

namespace {

using mybrowser::app::CliAction;
using mybrowser::app::LaunchOptions;

/// True for helper-process invocations ("MyBrowser --type=renderer ...").
bool IsHelperProcess(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i] != nullptr && std::strncmp(argv[i], "--type=", 7) == 0) return true;
  }
  return false;
}

void ApplyPathOverrides(const LaunchOptions& options, mybrowser::app::Paths* paths) {
  if (!options.profile_dir.empty()) {
    paths->profile_dir = options.profile_dir;
    paths->sessions_dir = mybrowser::app::JoinPath(paths->profile_dir, "sessions");
    paths->logs_dir = mybrowser::app::JoinPath(paths->profile_dir, "logs");
    paths->code_cache_dir = mybrowser::app::JoinPath(paths->profile_dir, "code_cache");
  }
  if (!options.cache_dir.empty()) paths->cache_dir = options.cache_dir;
  if (!options.downloads_dir.empty()) paths->downloads_dir = options.downloads_dir;
  mybrowser::app::EnsureDirectory(paths->profile_dir);
  mybrowser::app::EnsureDirectory(paths->cache_dir);
  mybrowser::app::EnsureDirectory(paths->sessions_dir);
  mybrowser::app::EnsureDirectory(paths->logs_dir);
}

void SetUpLogging(const LaunchOptions& options, const mybrowser::app::Paths& paths) {
  using mybrowser::core::Logger;
  using mybrowser::core::LogLevel;
  LogLevel level = LogLevel::kInfo;
  if (!options.log_level.empty()) mybrowser::core::ParseLogLevel(options.log_level, &level);
  Logger::Get().SetLevel(level);
  Logger::Get().SetEchoToStderr(level <= LogLevel::kInfo);
}

/// Builds the plan for a diagnostic request (same inputs the browser would use).
mybrowser::core::ModePlan PlanForDiagnostics(const LaunchOptions& options,
                                             const mybrowser::core::SystemProfile& profile,
                                             const mybrowser::core::Settings& settings) {
  mybrowser::core::EngineContext context = settings.MakeContext(options.mode, &profile);
  context.overrides.allow_advanced = options.advanced || context.overrides.allow_advanced;
  context.overrides.allow_lab = options.lab;
  if (options.renderer_limit > 0) context.overrides.renderer_process_limit = options.renderer_limit;
  if (options.cache_mb > 0) context.overrides.cache_size_mb = options.cache_mb;
  if (options.target_fps > 0) context.overrides.target_fps = options.target_fps;
  if (!options.gl_backend.empty()) context.gl_backend = options.gl_backend;
  return mybrowser::core::BuildPlan(options.mode, context);
}

int RunDiagnostics(const LaunchOptions& options, const mybrowser::app::Paths& paths) {
  const mybrowser::core::SystemProfile profile = mybrowser::core::DetectSystemProfile();

  switch (options.action) {
    case CliAction::kPrintProfile: {
      std::printf("%s\n", mybrowser::core::DescribeProfile(profile).c_str());
      return 0;
    }
    case CliAction::kListModes: {
      std::printf("%s", mybrowser::core::report::RenderModeList(profile).c_str());
      return 0;
    }
    case CliAction::kPrintCatalog: {
      std::printf("%s", mybrowser::core::RenderCatalogReport(/*include_removed=*/false).c_str());
      return 0;
    }
    case CliAction::kPrintFlagPlan: {
      mybrowser::core::Settings settings = mybrowser::core::Settings::Defaults();
      settings.startup_mode = options.mode;
      // Only the modes the user asked for; "all" or nothing means every mode.
      std::vector<mybrowser::core::Mode> modes;
      if (options.flag_plan_modes.empty() || options.flag_plan_modes == "all") {
        for (const mybrowser::core::ModeInfo& info : mybrowser::core::AllModes()) {
          modes.push_back(info.mode);
        }
      } else {
        mybrowser::core::Mode parsed{};
        if (!mybrowser::core::ParseMode(options.flag_plan_modes, &parsed)) {
          std::fprintf(stderr, "unknown mode '%s'\n", options.flag_plan_modes.c_str());
          return 2;
        }
        modes.push_back(parsed);
      }
      std::printf("\n  MyBrowser — Chromium switch plan (catalogue verified %s against chromium@%s)\n",
                  mybrowser::core::kCatalogVerifiedOn, mybrowser::core::kCatalogChromiumRef);
      for (const mybrowser::core::Mode mode : modes) {
        LaunchOptions plan_options = options;
        plan_options.mode = mode;
        const mybrowser::core::ModePlan plan = PlanForDiagnostics(plan_options, profile, settings);
        std::printf("%s", mybrowser::core::report::RenderPlanText(plan, profile, false).c_str());
      }
      std::printf("%s", mybrowser::core::report::RenderCatalogFooter().c_str());
      return 0;
    }
    case CliAction::kDoctor: {
      std::string report;
      report += "\n  MyBrowser doctor\n";
      report += "  =====================================================================\n";
      report += "  version      : " + mybrowser::app::VersionBanner() + "\n";
      report += "  profile dir  : " + paths.profile_dir + "\n";
      report += "  cache dir    : " + paths.cache_dir + "\n";
      report += "  logs dir     : " + paths.logs_dir + "\n";
      report += "  settings     : " + paths.settings_file() + "\n";
      report += "  portable     : " + std::string(paths.portable ? "yes" : "no") + "\n";
      report += mybrowser::core::DescribeProfile(profile);
      mybrowser::core::Settings settings = mybrowser::core::Settings::Defaults();
      settings.startup_mode = options.mode;
      const mybrowser::core::ModePlan plan = PlanForDiagnostics(options, profile, settings);
      report += "\n  resolved mode: " + mybrowser::core::DescribePlanOneLine(plan) + "\n";
      report += "  boot switches: " + std::to_string(plan.boot.switches().size()) + "\n";
      report += "  runtime      : " + mybrowser::core::report::RenderRuntimePolicySummary(plan.runtime) + "\n";
      report += mybrowser::core::report::RenderCatalogFooter();
      std::printf("%s", report.c_str());
      return 0;
    }
    default:
      return -1;  // not a diagnostics action
  }
}

CefSettings BuildCefSettings(const LaunchOptions& options, const mybrowser::app::Paths& paths) {
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
  CefString(&settings.log_file).FromString(mybrowser::app::JoinPath(paths.logs_dir, "cef.log"));
  CefString(&settings.user_agent_product)
      .FromString(std::string("MyBrowser/") + MYBROWSER_VERSION);
  const std::string resources = mybrowser::app::JoinPath(paths.executable_dir, "Resources");
  if (mybrowser::app::DirectoryExists(resources)) {
    CefString(&settings.resources_dir_path).FromString(resources);
    CefString(&settings.locales_dir_path).FromString(mybrowser::app::JoinPath(resources, "locales"));
  }
  return settings;
}

std::string BenchmarkUrl(const mybrowser::app::Paths& paths) {
  // Self-contained page: it exercises layout, paint and compositing without
  // needing the network, so `--benchmark` works in CI and offline.
  (void)paths;
  return "data:text/html,<html><body style='margin:0;background:%23101318;color:%23e6e8ee;"
         "font:16px sans-serif'><h1 style='padding:24px'>MyBrowser benchmark</h1>"
         "<p style='padding:0 24px'>Compositing, raster and memory measurement page.</p>"
         "<div style='height:2000px;background:linear-gradient(%23101318,%235ac8fa)'></div>"
         "</body></html>";
}

int RunSmokeOrBenchmark(mybrowser::app::CefHost* host, const LaunchOptions& options,
                        const mybrowser::app::Paths& paths) {
  const std::string url = options.url.empty() ? BenchmarkUrl(paths) : options.url;
  const int timeout = options.action == CliAction::kBenchmark ? 30 : 20;
  const mybrowser::browser::ProbeResult result =
      mybrowser::browser::RunHeadlessProbe(host, url, timeout);

  std::printf("\n");
  std::printf("  %s\n", mybrowser::app::VersionBanner().c_str());
  std::printf("  mode        : %s\n", mybrowser::core::GetModeInfo(host->current_mode()).label);
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
    return CefExecuteProcess(main_args, mybrowser::app::CreateApplication(), nullptr);
  }

  // 2. Our command line.
  const LaunchOptions options = mybrowser::app::ParseCli(argc, argv);
  if (!options.parse_error.empty()) {
    std::fprintf(stderr, "MyBrowser: %s\n\n%s", options.parse_error.c_str(),
                 mybrowser::app::CliUsage(argv[0]).c_str());
    return 2;
  }
  if (options.action == CliAction::kPrintHelp) {
    std::printf("%s", mybrowser::app::CliUsage(argv[0]).c_str());
    return 0;
  }
  if (options.action == CliAction::kPrintVersion) {
    std::printf("%s\n", mybrowser::app::VersionBanner().c_str());
    return 0;
  }

  // 3. Profile layout and logging.
  mybrowser::app::Paths paths = mybrowser::app::ResolvePaths();
  ApplyPathOverrides(options, &paths);
  SetUpLogging(options, paths);

  // 4. Diagnostics that do not need Chromium at all.
  const int diagnostics = RunDiagnostics(options, paths);
  if (diagnostics >= 0) return diagnostics;

  // 5. One instance per profile (unless the user insists). The lock is released
  //    by the OS if we crash, so a stale file can never block a launch.
  mybrowser::app::InstanceLock instance_lock;
  if (!options.allow_multiple) {
    int holder = 0;
    if (!instance_lock.Acquire(paths.profile_dir, &holder)) {
      std::fprintf(stderr,
                   "MyBrowser: another instance is already using this profile%s.\n"
                   "  (pid %d holds %s)\n"
                   "  use --allow-multiple only if you know the profile is not in use.\n",
                   paths.portable ? " (portable mode)" : "", holder,
                   mybrowser::app::JoinPath(paths.profile_dir, "instance.lock").c_str());
      return 3;
    }
  }

  // 6. Settings + mode plan: everything that must exist before Chromium starts.
  mybrowser::app::CefHost& host = mybrowser::app::Host();
  host.Initialize(options, paths);
  host.set_primary_instance(!options.allow_multiple);

  // 7. Initialise CEF. Our CefApp applies the boot switches from inside
  //    OnBeforeCommandLineProcessing, which CefInitialize invokes.
  const CefSettings cef_settings = BuildCefSettings(options, paths);
  if (!CefInitialize(main_args, cef_settings, mybrowser::app::CreateApplication(), nullptr)) {
    const int code = CefGetExitCode();
    mybrowser::core::log::ErrorF("app", "CefInitialize failed (exit code %d); see %s", code,
                                 mybrowser::app::JoinPath(paths.logs_dir, "cef.log").c_str());
    return code == 0 ? 4 : code;
  }
  mybrowser::core::log::InfoF("app", "CEF initialised (%s), %zu boot switches applied",
                              mybrowser::app::VersionBanner().c_str(),
                              host.applied_boot_flags().size());
  host.settings().RecordAppliedPlan(mybrowser::core::GetModeInfo(host.current_mode()).id,
                                    host.applied_boot_flags());
  host.settings().SaveIfDirty();

  // 8. Either the full UI or a headless probe.
  int exit_code = 0;
  if (options.action == CliAction::kSmokeTest || options.action == CliAction::kBenchmark) {
    exit_code = RunSmokeOrBenchmark(&host, options, paths);
  } else {
    std::unique_ptr<mybrowser::BrowserShell> shell =
        mybrowser::browser::CreateBrowserShell(&host);
    host.SetShell(shell.get());
    CefRunMessageLoop();
    shell->OnShuttingDown();
    host.SetShell(nullptr);
  }

  CefShutdown();
  mybrowser::core::log::Info("app", "shutdown complete");
  return exit_code;
}
