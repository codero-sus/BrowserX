// BrowserX — leveled logging with an in-memory ring buffer.
//
// The ring buffer is what powers the in-app Diagnostics panel: the last N
// entries are always available for display/export, even if stdout is closed.

#ifndef BROWSERX_CORE_LOG_H_
#define BROWSERX_CORE_LOG_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace browserx {
namespace core {

enum class LogLevel : int { kTrace = 0, kDebug = 1, kInfo = 2, kWarn = 3, kError = 4, kOff = 5 };

const char* LogLevelName(LogLevel level);
/// Accepts "trace"/"debug"/"info"/"warn"/"error"/"off" (case-insensitive).
bool ParseLogLevel(std::string_view name, LogLevel* out);

struct LogEntry {
  double timestamp_ms = 0.0;  // milliseconds since process start
  LogLevel level = LogLevel::kInfo;
  std::string category;  // e.g. "modes", "tabs", "memory"
  std::string message;
  /// Renders as "[  12.345s] INFO  modes: message".
  std::string ToString() const;
};

class Logger {
 public:
  static Logger& Get();

  void SetLevel(LogLevel level) { level_ = level; }
  LogLevel level() const { return level_; }
  void SetRingCapacity(size_t capacity);
  void SetEchoToStderr(bool echo) { echo_stderr_ = echo; }

  void Log(LogLevel level, std::string_view category, std::string message);

  /// Snapshot of the ring buffer, oldest first.
  std::vector<LogEntry> Snapshot() const;
  /// Snapshot filtered by minimum level.
  std::vector<LogEntry> SnapshotFrom(LogLevel min_level) const;
  void Clear();
  size_t size() const;

 private:
  Logger() = default;

  struct Impl;
  Impl* impl();
  const Impl* impl() const;

  void* state_ = nullptr;  // opaque Impl*, lazily created to keep the header light
  LogLevel level_ = LogLevel::kInfo;
  bool echo_stderr_ = true;
};

namespace log {

void Trace(std::string_view category, std::string message);
void Debug(std::string_view category, std::string message);
void Info(std::string_view category, std::string message);
void Warn(std::string_view category, std::string message);
void Error(std::string_view category, std::string message);

/// printf-style convenience wrappers.
void InfoF(std::string_view category, const char* format, ...);
void WarnF(std::string_view category, const char* format, ...);
void ErrorF(std::string_view category, const char* format, ...);

}  // namespace log

}  // namespace core
}  // namespace browserx

#endif  // BROWSERX_CORE_LOG_H_
