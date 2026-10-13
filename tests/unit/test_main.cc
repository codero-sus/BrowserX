#include "test_harness.h"

#include <exception>
#include <string>

namespace browserx {
namespace tests {
namespace {

TestCase* g_current = nullptr;
bool g_current_failed = false;

}  // namespace

std::vector<TestCase>& Registry() {
  static std::vector<TestCase> registry;
  return registry;
}

int Register(const char* suite, const char* name, const char* file, int line,
             std::function<void()> body) {
  Registry().push_back(TestCase{suite, name, std::move(body), file, line});
  return 0;
}

void ReportFailure(const std::string& text) {
  g_current_failed = true;
  std::printf("      FAIL: %s\n", text.c_str());
}

void ReportFatal(const std::string& text) {
  g_current_failed = true;
  ReportFailure(text);
  throw FatalFailure{text};
}

RunStats RunAll(const std::string& filter) {
  RunStats stats;
  std::string last_suite;
  for (TestCase& test : Registry()) {
    const std::string full = std::string(test.suite) + "." + test.name;
    if (!filter.empty() && full.find(filter) == std::string::npos) {
      ++stats.skipped;
      continue;
    }
    if (test.suite != last_suite) {
      std::printf("\n  %s\n", test.suite);
      last_suite = test.suite;
    }
    std::printf("    %-52s", test.name);
    std::fflush(stdout);

    g_current = &test;
    g_current_failed = false;
    try {
      test.body();
    } catch (const FatalFailure&) {
      // already reported
    } catch (const std::exception& exc) {
      ReportFailure(std::string("unhandled exception: ") + exc.what());
    } catch (...) {
      ReportFailure("unhandled non-standard exception");
    }

    if (g_current_failed) {
      ++stats.failed;
      stats.failures.push_back(full);
      std::printf("FAIL\n");
    } else {
      ++stats.passed;
      std::printf("ok\n");
    }
    g_current = nullptr;
  }
  return stats;
}

}  // namespace tests
}  // namespace browserx

int main(int argc, char** argv) {
  std::string filter;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--filter" && i + 1 < argc) {
      filter = argv[++i];
    } else if (arg.rfind("--filter=", 0) == 0) {
      filter = arg.substr(9);
    } else if (!arg.empty() && arg[0] != '-') {
      filter = arg;
    }
  }

  std::printf("\n  BrowserX unit tests\n  ====================\n");
  const browserx::tests::RunStats stats = browserx::tests::RunAll(filter);
  std::printf("\n  %d passed, %d failed", stats.passed, stats.failed);
  if (stats.skipped != 0) std::printf(", %d filtered out", stats.skipped);
  std::printf("\n");
  if (stats.failed != 0) {
    std::printf("\n  failed tests:\n");
    for (const std::string& name : stats.failures) std::printf("    - %s\n", name.c_str());
  }
  std::printf("\n");
  return stats.failed == 0 ? 0 : 1;
}
