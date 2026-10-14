// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/log.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>

namespace browserx {
namespace core {

namespace {

constexpr size_t kDefaultCapacity = 800;
std::chrono::steady_clock::time_point g_start = std::chrono::steady_clock::now();

double NowMs() {
  const auto now = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(now - g_start).count();
}

}  // namespace

const char* LogLevelName(LogLevel level) {
  switch (level) {
    case LogLevel::kTrace: return "TRACE";
    case LogLevel::kDebug: return "DEBUG";
    case LogLevel::kInfo: return "INFO";
    case LogLevel::kWarn: return "WARN";
    case LogLevel::kError: return "ERROR";
    case LogLevel::kOff: return "OFF";
  }
  return "?";
}

bool ParseLogLevel(std::string_view name, LogLevel* out) {
  if (name.empty()) return false;
  char first = name[0];
  if (first >= 'A' && first <= 'Z') first = static_cast<char>(first - 'A' + 'a');
  switch (first) {
    case 't': *out = LogLevel::kTrace; return true;
    case 'd': *out = LogLevel::kDebug; return true;
    case 'i': *out = LogLevel::kInfo; return true;
    case 'w': *out = LogLevel::kWarn; return true;
    case 'e': *out = LogLevel::kError; return true;
    case 'o': *out = LogLevel::kOff; return true;
    default: return false;
  }
}

std::string LogEntry::ToString() const {
  char prefix[64];
  std::snprintf(prefix, sizeof(prefix), "[%9.3fs] %-5s %s: ", timestamp_ms / 1000.0,
                LogLevelName(level), category.c_str());
  return std::string(prefix) + message;
}

struct Logger::Impl {
  mutable std::mutex mutex;
  std::deque<LogEntry> entries;
  size_t capacity = kDefaultCapacity;
};

Logger& Logger::Get() {
  static Logger logger;
  return logger;
}

Logger::Impl* Logger::impl() {
  if (state_ == nullptr) state_ = new Impl();
  return static_cast<Impl*>(state_);
}

const Logger::Impl* Logger::impl() const { return const_cast<Logger*>(this)->impl(); }

void Logger::SetRingCapacity(size_t capacity) {
  if (capacity == 0) capacity = 1;
  Impl* state = impl();
  std::lock_guard<std::mutex> lock(state->mutex);
  state->capacity = capacity;
  while (state->entries.size() > state->capacity) state->entries.pop_front();
}

void Logger::Log(LogLevel level, std::string_view category, std::string message) {
  if (level < level_ || level_ == LogLevel::kOff) return;
  LogEntry entry;
  entry.timestamp_ms = NowMs();
  entry.level = level;
  entry.category.assign(category);
  entry.message = std::move(message);

  Impl* state = impl();
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->entries.push_back(entry);
    while (state->entries.size() > state->capacity) state->entries.pop_front();
  }
  if (echo_stderr_) {
    const std::string line = entry.ToString() + "\n";
    std::fwrite(line.data(), 1, line.size(), stderr);
    std::fflush(stderr);
  }
}

std::vector<LogEntry> Logger::Snapshot() const {
  const Impl* state = impl();
  std::lock_guard<std::mutex> lock(state->mutex);
  return std::vector<LogEntry>(state->entries.begin(), state->entries.end());
}

std::vector<LogEntry> Logger::SnapshotFrom(LogLevel min_level) const {
  const Impl* state = impl();
  std::lock_guard<std::mutex> lock(state->mutex);
  std::vector<LogEntry> out;
  out.reserve(state->entries.size());
  for (const LogEntry& entry : state->entries) {
    if (entry.level >= min_level) out.push_back(entry);
  }
  return out;
}

void Logger::Clear() {
  Impl* state = impl();
  std::lock_guard<std::mutex> lock(state->mutex);
  state->entries.clear();
}

size_t Logger::size() const {
  const Impl* state = impl();
  std::lock_guard<std::mutex> lock(state->mutex);
  return state->entries.size();
}

namespace log {

void Trace(std::string_view category, std::string message) {
  Logger::Get().Log(LogLevel::kTrace, category, std::move(message));
}
void Debug(std::string_view category, std::string message) {
  Logger::Get().Log(LogLevel::kDebug, category, std::move(message));
}
void Info(std::string_view category, std::string message) {
  Logger::Get().Log(LogLevel::kInfo, category, std::move(message));
}
void Warn(std::string_view category, std::string message) {
  Logger::Get().Log(LogLevel::kWarn, category, std::move(message));
}
void Error(std::string_view category, std::string message) {
  Logger::Get().Log(LogLevel::kError, category, std::move(message));
}

void InfoF(std::string_view category, const char* format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  Info(category, buffer);
}

void WarnF(std::string_view category, const char* format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  Warn(category, buffer);
}

void ErrorF(std::string_view category, const char* format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  Error(category, buffer);
}

}  // namespace log

}  // namespace core
}  // namespace browserx
