// browserx-doctor — environment diagnosis.
//
//   make doctor                 what this machine is, and what BrowserX will do
//   make doctor ARGS="--check"  exit non-zero when the browser cannot start
//   make doctor ARGS="--json"   machine-readable, for bug reports
//
// It answers the three questions that actually block people:
//   1. What did we detect? (cores, RAM, /dev/shm, GPU, session, battery)
//   2. Which mode will be chosen, and why?
//   3. Is anything missing before `make run` can work (CEF build, libraries,
//      sandbox, small /dev/shm)?
//
// Links only against src/core, so it runs even when CEF is not downloaded yet -
// which is exactly when it is most useful.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/format.h"
#include "core/json.h"
#include "core/mode_engine.h"
#include "core/report.h"
#include "core/system_profile.h"

namespace {

using browserx::core::AllModes;
using browserx::core::BuildPlan;
using browserx::core::DefaultModeFor;
using browserx::core::DetectSystemProfile;
using browserx::core::DescribeProfile;
using browserx::core::EngineContext;
using browserx::core::GetModeInfo;
using browserx::core::Mode;
using browserx::core::ModePlan;
using browserx::core::ParseMode;
using browserx::core::RuntimeOverrides;
using browserx::core::SystemProfile;

struct Finding {
  enum class Level { kOk, kWarn, kFail };
  Level level = Level::kOk;
  std::string what;
  std::string detail;
  std::string fix;
};

/// Libraries Chromium/CEF dlopen at start-up. Missing ones are the #1 reason a
/// first build refuses to launch, and the message the user sees from CEF is not
/// helpful about which one it was.
const char* const kRequiredLibs[] = {
    "libX11.so.6",      "libXext.so.6",     "libXcomposite.so.1", "libXdamage.so.1",
    "libXfixes.so.3",   "libXrandr.so.2",   "libXtst.so.6",       "libgtk-3.so.0",
    "libnss3.so",       "libnspr4.so",      "libasound.so.2",     "libatk-1.0.so.0",
    "libcups.so.2",     "libdrm.so.2",      "libgbm.so.1",        "libexpat.so.1",
    "libpango-1.0.so.0", "libcairo.so.2",   "libgdk_pixbuf-2.0.so.0",
};

std::vector<std::string> LibrarySearchDirs() {
  std::vector<std::string> dirs = {
      "/usr/lib/x86_64-linux-gnu",
      "/usr/lib/aarch64-linux-gnu",
      "/usr/lib64",
      "/usr/lib",
      "/lib/x86_64-linux-gnu",
      "/lib64",
      "/lib",
  };
#if defined(__aarch64__)
  std::swap(dirs[0], dirs[1]);
#endif
  return dirs;
}

bool LibraryPresent(const std::string& name, std::string* location) {
  for (const std::string& dir : LibrarySearchDirs()) {
    std::ifstream probe(dir + "/" + name);
    if (probe.good()) {
      if (location != nullptr) *location = dir + "/" + name;
      return true;
    }
  }
  return false;
}

std::string ReadFileOrEmpty(const std::string& path) {
  std::ifstream file(path);
  if (!file.good()) return std::string();
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

/// Cheap check for a CEF distribution: read the lock file and look for a matching
/// directory under .cef-deps/.
void CheckCef(const std::string& root, std::vector<Finding>* findings) {
  const std::string lock = ReadFileOrEmpty(root + "/cef.lock.json");
  std::string version;
  if (!lock.empty()) {
    auto parsed = browserx::core::json::Parse(lock);
    if (parsed.has_value()) {
      const browserx::core::json::Value& found = parsed->Get("version");
      if (found.IsString()) version = found.AsString("");
    }
  }

  std::ifstream dirs(root + "/.cef-deps");
  if (!dirs.good()) {
    findings->push_back({Finding::Level::kWarn, "CEF distribution",
                         version.empty() ? "not downloaded yet" : "pinned " + version + ", not downloaded",
                         "make deps"});
    return;
  }

  // Look for any extracted cef_binary_* tree with the headers in place.
  const std::string probe = root + "/.cef-deps";
  std::ifstream listing(probe);
  if (!listing.good()) {
    findings->push_back({Finding::Level::kWarn, "CEF distribution",
                         "no .cef-deps directory yet", "make deps"});
    return;
  }
  bool found = false;
  // Directory listing without <filesystem> (kept C++17-friendly on purpose).
  FILE* pipe = popen(("ls -1 " + probe + "/cef_binary_*/include/cef_app.h 2>/dev/null").c_str(), "r");
  if (pipe != nullptr) {
    char buffer[512];
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) found = true;
    pclose(pipe);
  }
  if (found) {
    findings->push_back({Finding::Level::kOk, "CEF distribution",
                         version.empty() ? "installed" : "installed (pinned " + version + ")", ""});
  } else {
    findings->push_back({Finding::Level::kWarn, "CEF distribution",
                         "the download directory exists but holds no complete build", "make deps"});
  }
}

std::string LevelTag(Finding::Level level) {
  switch (level) {
    case Finding::Level::kOk: return "  ✔";
    case Finding::Level::kWarn: return "  !";
    case Finding::Level::kFail: return "  ✘";
  }
  return "  ?";
}

int RunDoctor(const std::vector<std::string>& args) {
  bool json = false;
  bool check_only = false;
  bool quiet = false;
  std::string root = ".";
  for (const std::string& arg : args) {
    if (arg == "--json") json = true;
    else if (arg == "--check") check_only = true;
    else if (arg == "--quiet") quiet = true;
    else if (arg.rfind("--root=", 0) == 0) root = arg.substr(7);
    else if (arg == "-h" || arg == "--help") {
      std::printf(
          "browserx-doctor — environment diagnosis for BrowserX\n\n"
          "  --check        exit non-zero when the browser cannot start here\n"
          "  --json         machine-readable report\n"
          "  --root=DIR     repository root (default: the working directory)\n");
      return 0;
    } else {
      std::fprintf(stderr, "doctor: unknown option %s\n", arg.c_str());
      return 2;
    }
  }

  SystemProfile profile = DetectSystemProfile();
  std::vector<Finding> findings;
  CheckCef(root, &findings);

  // --- libraries -----------------------------------------------------------
  std::vector<std::string> missing;
  for (const char* name : kRequiredLibs) {
    std::string location;
    if (!LibraryPresent(name, &location)) missing.push_back(name);
  }
  if (missing.empty()) {
    findings.push_back({Finding::Level::kOk, "GUI libraries",
                        "all " + std::to_string(sizeof(kRequiredLibs) / sizeof(kRequiredLibs[0])) +
                            " runtime libraries found",
                        ""});
  } else {
    std::string list;
    for (size_t i = 0; i < missing.size(); ++i) {
      list += (i == 0 ? "" : ", ") + missing[i];
    }
    findings.push_back({Finding::Level::kFail, "GUI libraries", "missing: " + list,
                        "install them (Debian/Ubuntu: apt install libgtk-3-0 libnss3 libasound2 "
                        "libx11-6 libxcomposite1 libxdamage1 libxrandr2 libxtst6; Fedora: "
                        "gtk3 nss alsa-lib libX11 libXcomposite libXdamage libXrandr libXtst)"});
  }

  // --- /dev/shm ------------------------------------------------------------
  if (profile.low_shm) {
    findings.push_back({Finding::Level::kWarn, "/dev/shm",
                        "only " + std::to_string(static_cast<long long>(profile.shm_total_mb)) +
                            " MB — Chromium renderers can crash without a workaround",
                        "BrowserX applies --disable-dev-shm-usage automatically for this machine"});
  } else if (profile.shm_total_mb > 0) {
    findings.push_back({Finding::Level::kOk, "/dev/shm",
                        std::to_string(static_cast<long long>(profile.shm_total_mb)) + " MB", ""});
  }

  // --- sandbox -------------------------------------------------------------
#if defined(__linux__)
  const std::string sandbox = ReadFileOrEmpty(root + "/out/chrome-sandbox");
  if (sandbox.empty()) {
    findings.push_back({Finding::Level::kWarn, "Chromium sandbox",
                        "chrome-sandbox is not staged yet",
                        "make stage, then: make sandbox-setup (needs sudo)"});
  } else {
    findings.push_back({Finding::Level::kOk, "Chromium sandbox", "chrome-sandbox staged", ""});
  }
#endif

  const bool blocked = std::any_of(findings.begin(), findings.end(), [](const Finding& finding) {
    return finding.level == Finding::Level::kFail;
  });

  if (json) {
    using browserx::core::json::Value;
    Value::Object root_object;
    root_object["blocked"] = Value(blocked);
    Value::Array lines;
    for (const Finding& finding : findings) {
      Value::Object entry;
      entry["level"] = Value(finding.level == Finding::Level::kOk
                                 ? "ok"
                                 : (finding.level == Finding::Level::kWarn ? "warn" : "fail"));
      entry["what"] = Value(finding.what);
      entry["detail"] = Value(finding.detail);
      entry["fix"] = Value(finding.fix);
      lines.emplace_back(entry);
    }
    root_object["findings"] = Value(lines);
    root_object["default_mode"] = Value(GetModeInfo(DefaultModeFor(profile)).id);
    std::printf("%s\n", browserx::core::json::Serialize(Value(root_object), 2).c_str());
    return blocked ? 1 : 0;
  }

  if (!quiet) {
    std::printf("\n  BrowserX — doctor\n  %s\n\n", std::string(72, '=').c_str());
    std::printf("%s\n", DescribeProfile(profile).c_str());
    std::printf("  recommended mode : %s (%s)\n\n", GetModeInfo(DefaultModeFor(profile)).label,
                GetModeInfo(DefaultModeFor(profile)).id);
  }

  std::printf("  readiness\n");
  for (const Finding& finding : findings) {
    std::printf("%s %-18s %s\n", LevelTag(finding.level).c_str(), finding.what.c_str(),
                finding.detail.c_str());
    if (!finding.fix.empty()) std::printf("      fix: %s\n", finding.fix.c_str());
  }

  if (check_only) {
    std::printf("\n  %s\n\n", blocked ? "✘ something would prevent BrowserX from starting"
                                      : "✔ this machine can run BrowserX");
    return blocked ? 1 : 0;
  }

  // Full plans: the same data `make bench` prints, condensed here.
  EngineContext context;
  context.profile = &profile;
  std::printf("\n  what each mode would apply on this machine\n");
  for (const browserx::core::ModeInfo& info : AllModes()) {
    const ModePlan plan = BuildPlan(info.mode, context);
    std::printf("    %-16s %s\n", info.label, plan.boot.ToCommandLine().c_str());
    std::printf("                     %s\n", plan.notes.empty() ? "" : plan.notes.front().c_str());
  }
  std::printf("\n%s\n", browserx::core::report::RenderCatalogFooter().c_str());
  std::printf("  next: make deps && make && make run\n\n");
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
  return RunDoctor(args);
}
