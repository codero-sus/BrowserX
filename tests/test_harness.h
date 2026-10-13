// BrowserX — micro test harness (no third-party dependencies).
//
// Usage:
//   #include "test_harness.h"
//   TEST(JsonSuite, ParsesObjects) { EXPECT_EQ(json::Serialize(v), "{}"); }
//   EXPECT_TRUE(flag_applied) << "diagnostic detail " << value;
//
// Run with: make test                              (all tests)
//           make test TEST_FILTER=ModeEngine       (substring filter)
//
// Failure reporting is done by a CheckResult temporary whose destructor writes
// the message, which is what makes the trailing `<< "extra context"` work.

#ifndef BROWSERX_TESTS_TEST_HARNESS_H_
#define BROWSERX_TESTS_TEST_HARNESS_H_

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace browserx {
namespace tests {

struct TestCase {
  const char* suite;
  const char* name;
  std::function<void()> body;
  const char* file;
  int line;
};

std::vector<TestCase>& Registry();
int Register(const char* suite, const char* name, const char* file, int line,
             std::function<void()> body);

struct Failure {
  std::string message;
};
struct FatalFailure {
  std::string message;
};

struct RunStats {
  int passed = 0;
  int failed = 0;
  int skipped = 0;
  std::vector<std::string> failures;
};

RunStats RunAll(const std::string& filter);

void ReportFailure(const std::string& text);
void ReportFatal(const std::string& text);

/// Formatting helper so EXPECT_EQ works for any streamable type.
template <typename T>
std::string Stringify(const T& value) {
  std::ostringstream out;
  out << value;
  return out.str();
}
inline std::string Stringify(const std::string& value) { return value; }
inline std::string Stringify(std::string_view value) { return std::string(value); }
inline std::string Stringify(bool value) { return value ? "true" : "false"; }
inline std::string Stringify(const char* value) { return value == nullptr ? "(null)" : value; }
inline std::string Stringify(std::nullptr_t) { return "(nullptr)"; }

/// Generic truthiness (works for bool, pointers, ...) and for optionals.
template <typename T>
bool HasValue(const T& value) {
  return static_cast<bool>(value);
}
template <typename T>
bool HasValue(const std::optional<T>& value) {
  return value.has_value();
}

/// Collects a failure message and (optionally) streamed context, then reports.
class CheckResult {
 public:
  CheckResult(const char* file, int line, bool ok, std::string detail)
      : file_(file), line_(line), ok_(ok), detail_(std::move(detail)) {}
  CheckResult(const CheckResult&) = delete;
  CheckResult& operator=(const CheckResult&) = delete;
  CheckResult(CheckResult&&) = delete;

  ~CheckResult() {
    if (ok_) return;
    std::ostringstream out;
    out << file_ << ":" << line_ << "  " << detail_;
    if (!extra_.empty()) out << "\n      " << extra_;
    ReportFailure(out.str());
  }

  template <typename T>
  CheckResult& operator<<(const T& value) {
    if (!extra_.empty()) extra_ += " ";
    extra_ += Stringify(value);
    return *this;
  }

 private:
  const char* file_;
  int line_;
  bool ok_;
  std::string detail_;
  std::string extra_;
};

inline CheckResult Check(const char* file, int line, bool ok, std::string detail) {
  return CheckResult(file, line, ok, std::move(detail));
}

template <typename A, typename B>
CheckResult CheckEq(const char* file, int line, const char* a_text, const char* b_text,
                    const A& actual, const B& expected) {
  if (actual == expected) return CheckResult(file, line, true, std::string());
  std::ostringstream out;
  out << "EXPECT_EQ(" << a_text << ", " << b_text << ") failed\n"
      << "    actual:   " << Stringify(actual) << "\n"
      << "    expected: " << Stringify(expected);
  return CheckResult(file, line, false, out.str());
}

template <typename A, typename B>
CheckResult CheckCmp(const char* file, int line, const char* op, const char* a_text,
                     const char* b_text, bool ok, const A& a, const B& b) {
  if (ok) return CheckResult(file, line, true, std::string());
  std::ostringstream out;
  out << "EXPECT_" << op << "(" << a_text << ", " << b_text << ") failed\n"
      << "    actual:   " << Stringify(a) << "\n"
      << "    expected: " << op << " " << Stringify(b);
  return CheckResult(file, line, false, out.str());
}

/// Fatal assertion: reports and aborts the current test.
inline void AssertTrue(const char* file, int line, const char* text, bool ok) {
  if (ok) return;
  std::ostringstream out;
  out << file << ":" << line << "  ASSERT failed: " << text;
  ReportFatal(out.str());
}

}  // namespace tests
}  // namespace browserx

// ---------------------------------------------------------------------------
// Macros
// ---------------------------------------------------------------------------
#define TEST(suite, name)                                                             \
  static void suite##_##name##_Body();                                                \
  static int suite##_##name##_Reg = ::browserx::tests::Register(                      \
      #suite, #name, __FILE__, __LINE__, suite##_##name##_Body);                       \
  static void suite##_##name##_Body()

#define EXPECT_TRUE(cond)                                                             \
  ::browserx::tests::Check(__FILE__, __LINE__, static_cast<bool>(cond),               \
                            "EXPECT_TRUE(" #cond ") failed")

#define EXPECT_FALSE(cond)                                                            \
  ::browserx::tests::Check(__FILE__, __LINE__, !static_cast<bool>(cond),              \
                            "EXPECT_FALSE(" #cond ") failed")

#define EXPECT_EQ(a, b)                                                               \
  ::browserx::tests::CheckEq(__FILE__, __LINE__, #a, #b, (a), (b))

#define EXPECT_NE(a, b)                                                               \
  ::browserx::tests::CheckCmp(__FILE__, __LINE__, "NE", #a, #b, ((a) != (b)), (a), (b))
#define EXPECT_LT(a, b)                                                               \
  ::browserx::tests::CheckCmp(__FILE__, __LINE__, "LT", #a, #b, ((a) < (b)), (a), (b))
#define EXPECT_LE(a, b)                                                               \
  ::browserx::tests::CheckCmp(__FILE__, __LINE__, "LE", #a, #b, ((a) <= (b)), (a), (b))
#define EXPECT_GT(a, b)                                                               \
  ::browserx::tests::CheckCmp(__FILE__, __LINE__, "GT", #a, #b, ((a) > (b)), (a), (b))
#define EXPECT_GE(a, b)                                                               \
  ::browserx::tests::CheckCmp(__FILE__, __LINE__, "GE", #a, #b, ((a) >= (b)), (a), (b))

#define EXPECT_NEAR(a, b, tol)                                                        \
  ::browserx::tests::Check(__FILE__, __LINE__,                                       \
                            (((a) - (b)) <= (tol) && ((b) - (a)) <= (tol)),           \
                            "EXPECT_NEAR(" #a ", " #b ", " #tol ") failed")

/// Non-fatal failure with a message.
#define FAIL(message)                                                                 \
  do {                                                                                \
    std::ostringstream mb_oss_;                                                       \
    mb_oss_ << __FILE__ << ":" << __LINE__ << "  " << message;                        \
    ::browserx::tests::ReportFailure(mb_oss_.str());                                 \
  } while (false)

/// Fatal failure: aborts the current test.
#define FATAL_FAIL(message)                                                           \
  do {                                                                                \
    std::ostringstream mb_oss_;                                                       \
    mb_oss_ << __FILE__ << ":" << __LINE__ << "  " << message;                        \
    ::browserx::tests::ReportFatal(mb_oss_.str());                                   \
  } while (false)

/// Fatal assertion: use for preconditions that would crash the rest of the test.
#define ASSERT_TRUE(cond)                                                             \
  ::browserx::tests::AssertTrue(__FILE__, __LINE__, #cond, static_cast<bool>(cond))

/// Fatal assertion that also unwraps optionals/pointers:
///   auto value = Parse(text); ASSERT_TRUE_OR_RETURN(value); value->Get(..);
#define ASSERT_TRUE_OR_RETURN(expr)                                                   \
  do {                                                                                \
    if (!::browserx::tests::HasValue(expr)) {                                        \
      ::browserx::tests::AssertTrue(__FILE__, __LINE__, "ASSERT_TRUE_OR_RETURN(" #expr ")", false); \
      return;                                                                         \
    }                                                                                 \
  } while (false)

/// Marks a test as skipped at runtime (e.g. missing privileges).
#define SKIP(message)                                                                 \
  do {                                                                                \
    std::printf("      SKIP: %s\n", std::string(message).c_str());                    \
    return;                                                                           \
  } while (false)

#endif  // BROWSERX_TESTS_TEST_HARNESS_H_
