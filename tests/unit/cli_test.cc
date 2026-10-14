// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// Unit tests for the command-line grammar (src/app/cli.cc + app_paths helpers).
//
// These run without CEF: the CLI is pure string logic, so `make test` verifies
// the exact options the browser accepts - including the promise that unknown
// switches are forwarded to Chromium untouched.

#include <string>
#include <vector>

#include "app/app_paths.h"
#include "app/cli.h"
#include "core/url_utils.h"
#include "core/mode_engine.h"
#include "test_harness.h"

namespace {

using browserx::app::CliAction;
using browserx::app::LaunchOptions;
using browserx::app::ParseCli;
using browserx::core::AutoPilotMode;
using browserx::core::Mode;

LaunchOptions Parse(const std::vector<std::string>& args) {
  std::vector<const char*> argv;
  argv.push_back("browserx");
  for (const std::string& arg : args) argv.push_back(arg.c_str());
  return ParseCli(static_cast<int>(argv.size()), argv.data());
}

TEST(Cli, DefaultsAreSafe) {
  const LaunchOptions options = Parse({});
  EXPECT_EQ(static_cast<int>(options.action), static_cast<int>(CliAction::kRun));
  EXPECT_EQ(static_cast<int>(options.mode), static_cast<int>(Mode::kUltra));
  EXPECT_TRUE(options.sandbox);
  EXPECT_FALSE(options.advanced);
  EXPECT_FALSE(options.lab);
  EXPECT_FALSE(options.headless);
  EXPECT_TRUE(options.parse_error.empty());
  EXPECT_TRUE(options.url.empty());
}

TEST(Cli, ParsesModeInBothForms) {
  EXPECT_EQ(static_cast<int>(Parse({"--mode=gaming"}).mode), static_cast<int>(Mode::kGaming));
  EXPECT_EQ(static_cast<int>(Parse({"--mode", "low-ram"}).mode), static_cast<int>(Mode::kLowRam));
  EXPECT_TRUE(Parse({"--mode=gaming"}).mode_explicit);

  const LaunchOptions bad = Parse({"--mode=warp-speed"});
  EXPECT_FALSE(bad.parse_error.empty());
}

TEST(Cli, ParsesAutoPilot) {
  EXPECT_EQ(static_cast<int>(Parse({"--auto-pilot=off"}).auto_pilot),
            static_cast<int>(AutoPilotMode::kOff));
  EXPECT_TRUE(Parse({"--auto-pilot=auto"}).auto_pilot_explicit);
  EXPECT_FALSE(Parse({"--auto-pilot=maybe"}).parse_error.empty());
}

TEST(Cli, LabImpliesAdvanced) {
  const LaunchOptions options = Parse({"--lab"});
  EXPECT_TRUE(options.lab);
  EXPECT_TRUE(options.advanced);
}

TEST(Cli, ForwardsUnknownSwitchesToChromium) {
  const LaunchOptions options = Parse({"--enable-logging=stderr", "--v=1", "example.com"});
  ASSERT_TRUE_OR_RETURN(options.parse_error.empty());
  ASSERT_TRUE_OR_RETURN(options.passthrough.size() == 2);
  EXPECT_EQ(options.passthrough[0], std::string("--enable-logging=stderr"));
  EXPECT_EQ(options.passthrough[1], std::string("--v=1"));
  // The positional argument becomes the start page (raw; resolved at launch).
  EXPECT_EQ(options.url, std::string("example.com"));
}

TEST(Cli, PositionalArgsBecomeTabs) {
  const LaunchOptions options = Parse({"example.com", "wikipedia.org"});
  ASSERT_TRUE_OR_RETURN(options.parse_error.empty());
  EXPECT_EQ(options.url, std::string("example.com"));
  ASSERT_TRUE_OR_RETURN(options.tabs.size() == 1);
  EXPECT_EQ(options.tabs[0], std::string("wikipedia.org"));
}

TEST(Cli, SearchTermsAreKeptRawAndResolvedAtLaunch) {
  // The CLI cannot know the configured search engine, so it must not bake in a
  // URL here: CefHost::Initialize() resolves this with settings.json.
  const LaunchOptions options = Parse({"how to speed up chromium"});
  ASSERT_TRUE_OR_RETURN(options.parse_error.empty());
  EXPECT_EQ(options.url, std::string("how to speed up chromium"));
  EXPECT_EQ(browserx::core::url::ResolveInput(options.url, "https://ddg.example/?q=%s"),
            std::string("https://ddg.example/?q=how+to+speed+up+chromium"));
}

TEST(Cli, NumericOptionsAreRangeChecked) {
  EXPECT_EQ(Parse({"--renderers=3"}).renderer_limit, 3);
  EXPECT_EQ(Parse({"--cache-mb", "512"}).cache_mb, 512);
  EXPECT_FALSE(Parse({"--renderers=0"}).parse_error.empty());
  EXPECT_FALSE(Parse({"--renderers=9999"}).parse_error.empty());
  EXPECT_FALSE(Parse({"--cache-mb"}).parse_error.empty());
}

TEST(Cli, DiagnosticsActions) {
  EXPECT_EQ(static_cast<int>(Parse({"--help"}).action), static_cast<int>(CliAction::kPrintHelp));
  EXPECT_EQ(static_cast<int>(Parse({"-v"}).action), static_cast<int>(CliAction::kPrintVersion));
  EXPECT_EQ(static_cast<int>(Parse({"--print-profile"}).action),
            static_cast<int>(CliAction::kPrintProfile));
  EXPECT_EQ(static_cast<int>(Parse({"--print-catalog"}).action),
            static_cast<int>(CliAction::kPrintCatalog));
  EXPECT_EQ(static_cast<int>(Parse({"--list-modes"}).action), static_cast<int>(CliAction::kListModes));
  EXPECT_EQ(static_cast<int>(Parse({"--smoke-test"}).action), static_cast<int>(CliAction::kSmokeTest));
  EXPECT_EQ(static_cast<int>(Parse({"--doctor"}).action), static_cast<int>(CliAction::kDoctor));
}

TEST(Cli, FlagPlanTakesAnOptionalMode) {
  const LaunchOptions all = Parse({"--flag-plan"});
  EXPECT_EQ(static_cast<int>(all.action), static_cast<int>(CliAction::kPrintFlagPlan));
  EXPECT_TRUE(all.flag_plan_modes.empty());

  const LaunchOptions one = Parse({"--flag-plan=eco"});
  EXPECT_EQ(one.flag_plan_modes, std::string("eco"));

  const LaunchOptions spaced = Parse({"--flag-plan", "gaming"});
  EXPECT_EQ(spaced.flag_plan_modes, std::string("gaming"));

  // A mode name that does not parse must not be swallowed as the value.
  const LaunchOptions other = Parse({"--flag-plan", "example.com"});
  EXPECT_TRUE(other.flag_plan_modes.empty());
  EXPECT_EQ(other.url, std::string("example.com"));
}

TEST(Cli, BenchmarkTakesSeconds) {
  const LaunchOptions options = Parse({"--benchmark=12"});
  EXPECT_EQ(static_cast<int>(options.action), static_cast<int>(CliAction::kBenchmark));
  EXPECT_EQ(options.benchmark_seconds, 12);
  EXPECT_FALSE(Parse({"--benchmark=abc"}).parse_error.empty());
}

TEST(Cli, DoubleDashEndsOptionParsing) {
  const LaunchOptions options = Parse({"--", "--not-a-flag.com"});
  ASSERT_TRUE_OR_RETURN(options.parse_error.empty());
  EXPECT_TRUE(options.passthrough.empty());
  EXPECT_EQ(options.url, std::string("--not-a-flag.com"));
}

TEST(Cli, PathsAndJoinHelpers) {
  using browserx::app::FileName;
  using browserx::app::JoinPath;
  using browserx::app::ParentDir;

  EXPECT_EQ(JoinPath("/a", "b"), std::string("/a/b"));
  EXPECT_EQ(JoinPath("/a/", "b"), std::string("/a/b"));
  EXPECT_EQ(JoinPath("", "b"), std::string("b"));
  EXPECT_EQ(ParentDir("/a/b/c.json"), std::string("/a/b"));
  EXPECT_EQ(FileName("/a/b/c.json"), std::string("c.json"));
  EXPECT_EQ(FileName("c.json"), std::string("c.json"));
  EXPECT_TRUE(ParentDir("c.json").empty());
}

TEST(Cli, WriteBenchPageTakesAnOptionalFile) {
  const LaunchOptions bare = Parse({"--write-bench-page"});
  EXPECT_EQ(static_cast<int>(bare.action), static_cast<int>(CliAction::kWriteBenchPage));
  EXPECT_TRUE(bare.bench_page_path.empty()) << "empty means the default file name";

  const LaunchOptions joined = Parse({"--write-bench-page=/tmp/bench.html"});
  EXPECT_EQ(static_cast<int>(joined.action), static_cast<int>(CliAction::kWriteBenchPage));
  EXPECT_EQ(joined.bench_page_path, std::string("/tmp/bench.html"));

  const LaunchOptions spaced = Parse({"--write-bench-page", "bench.html"});
  EXPECT_EQ(spaced.bench_page_path, std::string("bench.html"));

  // The next token belongs to the page only when it is not another option: a URL must
  // still be understood as a URL.
  const LaunchOptions with_url = Parse({"--write-bench-page", "example.com"});
  EXPECT_EQ(with_url.bench_page_path, std::string("example.com"));

  const LaunchOptions flag_next = Parse({"--write-bench-page", "--doctor"});
  EXPECT_TRUE(flag_next.bench_page_path.empty());
  EXPECT_EQ(static_cast<int>(flag_next.action), static_cast<int>(CliAction::kDoctor));
}

TEST(Cli, UsageTextDocumentsEveryRealOption) {
  const std::string usage = browserx::app::CliUsage("browserx");
  for (const char* needle : {"--mode", "--auto-pilot", "--advanced", "--lab", "--renderers",
                            "--cache-mb", "--fps", "--gl", "--portable", "--profile",
                            "--cache-dir", "--downloads", "--frameless", "--no-sandbox",
                            "--headless", "--allow-multiple", "--tab", "--log-level",
                            "--flag-plan", "--print-profile", "--print-catalog", "--doctor",
                            "--benchmark", "--smoke-test", "--write-bench-page", "--help"}) {
    EXPECT_TRUE(usage.find(needle) != std::string::npos);
  }
}

TEST(Cli, UsageCreditsTheAuthorAndShipsTheLinks) {
  const std::string usage = browserx::app::CliUsage("browserx");
  EXPECT_TRUE(usage.find("codero-sus") != std::string::npos);
  EXPECT_TRUE(usage.find("https://the-codero.web.app") != std::string::npos);
  EXPECT_TRUE(usage.find("https://github.com/codero-sus/BrowserX") != std::string::npos);
  // The licence is personal-use-only: the CLI must say so, in the same words the
  // About page and the packaged build use (kProjectLicenceTerms).
  EXPECT_TRUE(usage.find("BrowserX Personal-Use License") != std::string::npos);
  EXPECT_TRUE(usage.find("personal, non-commercial") != std::string::npos);
  EXPECT_TRUE(usage.find("no modification") != std::string::npos);
  EXPECT_TRUE(usage.find("no redistribution or resale") != std::string::npos);
  EXPECT_TRUE(usage.find("not open source") != std::string::npos);
  EXPECT_FALSE(usage.find("BSD-3-Clause") != std::string::npos);
  // The banner is what `--version` prints; it must name the author too.
  EXPECT_TRUE(browserx::app::VersionBanner().find("BrowserX") == 0);
  EXPECT_TRUE(browserx::app::VersionBanner().find("codero-sus") != std::string::npos);
}

}  // namespace

TEST(Cli, HelpListsEveryModeFromTheEngine) {
  // The mode list in --help is generated from AllModes(), so a new mode can never
  // be missing from the documentation the user actually reads.
  const std::string usage = browserx::app::CliUsage("browserx");
  for (const browserx::core::ModeInfo& info : browserx::core::AllModes()) {
    EXPECT_TRUE(usage.find(info.id) != std::string::npos)
        << "mode missing from --help: " << info.id;
  }
  EXPECT_TRUE(usage.find("super-internet") != std::string::npos);
  EXPECT_TRUE(usage.find("download") != std::string::npos);
  EXPECT_TRUE(usage.find("streaming") != std::string::npos);
}
