// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/log.h"

#include <string>

#include "test_harness.h"

using browserx::core::LogLevel;
using browserx::core::Logger;

TEST(Log, LevelNamesRoundTrip) {
  EXPECT_EQ(std::string(browserx::core::LogLevelName(LogLevel::kWarn)), std::string("WARN"));
  LogLevel parsed = LogLevel::kOff;
  EXPECT_TRUE(browserx::core::ParseLogLevel("debug", &parsed));
  EXPECT_TRUE(parsed == LogLevel::kDebug);
  EXPECT_TRUE(browserx::core::ParseLogLevel("ERROR", &parsed));
  EXPECT_TRUE(parsed == LogLevel::kError);
  EXPECT_FALSE(browserx::core::ParseLogLevel("nonsense", &parsed));
}

TEST(Log, RingBufferKeepsNewestEntries) {
  Logger& logger = Logger::Get();
  logger.SetEchoToStderr(false);
  logger.SetRingCapacity(5);
  logger.SetLevel(LogLevel::kTrace);
  logger.Clear();

  for (int i = 0; i < 12; ++i) {
    logger.Log(LogLevel::kInfo, "test", "message " + std::to_string(i));
  }
  const auto entries = logger.Snapshot();
  EXPECT_EQ(entries.size(), 5u);
  EXPECT_EQ(entries.front().message, std::string("message 7"));
  EXPECT_EQ(entries.back().message, std::string("message 11"));
  EXPECT_EQ(entries.back().category, std::string("test"));
}

TEST(Log, LevelFiltering) {
  Logger& logger = Logger::Get();
  logger.SetEchoToStderr(false);
  logger.SetLevel(LogLevel::kWarn);
  logger.Clear();
  logger.Log(LogLevel::kInfo, "test", "hidden");
  logger.Log(LogLevel::kError, "test", "shown");
  const auto entries = logger.Snapshot();
  EXPECT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries.front().message, std::string("shown"));

  logger.SetLevel(LogLevel::kOff);
  logger.Log(LogLevel::kError, "test", "also hidden");
  EXPECT_EQ(logger.Snapshot().size(), 1u);

  logger.SetLevel(LogLevel::kTrace);
  logger.Clear();
  browserx::core::log::InfoF("fmt", "value=%d name=%s", 7, "seven");
  const auto formatted = logger.SnapshotFrom(LogLevel::kInfo);
  EXPECT_GT(formatted.size(), 0u);
  EXPECT_EQ(formatted.back().message, std::string("value=7 name=seven"));
}

TEST(Log, EntryFormatting) {
  browserx::core::LogEntry entry;
  entry.timestamp_ms = 1234.0;
  entry.level = LogLevel::kWarn;
  entry.category = "modes";
  entry.message = "switched to gaming";
  const std::string text = entry.ToString();
  EXPECT_TRUE(text.find("WARN") != std::string::npos);
  EXPECT_TRUE(text.find("modes: switched to gaming") != std::string::npos);
}
