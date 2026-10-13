#include "core/format.h"

#include "test_harness.h"

using namespace browserx::core::fmt;

TEST(Format, CaseAndTrimHelpers) {
  EXPECT_EQ(ToLower("HeLLo"), std::string("hello"));
  EXPECT_EQ(ToUpper("HeLLo"), std::string("HELLO"));
  EXPECT_EQ(Trim("  spaces  "), std::string("spaces"));
  EXPECT_EQ(TrimLeft("  left"), std::string("left"));
  EXPECT_EQ(TrimRight("right  "), std::string("right"));
  EXPECT_EQ(Trim("\t\n mixed \r\n"), std::string("mixed"));
}

TEST(Format, Predicates) {
  EXPECT_TRUE(StartsWith("browserx://settings", "browserx://"));
  EXPECT_FALSE(StartsWith("https://x", "http://"));
  EXPECT_TRUE(EndsWith("index.html", ".html"));
  EXPECT_TRUE(Contains("a needle here", "needle"));
  EXPECT_TRUE(ContainsCaseInsensitive("ULTRA Mode", "ultra"));
  EXPECT_TRUE(EqualsCaseInsensitive("Game", "GAME"));
  EXPECT_FALSE(EqualsCaseInsensitive("Game", "Gam3"));
}

TEST(Format, SplitAndJoin) {
  const auto parts = Split("a,b,,c", ',');
  EXPECT_EQ(parts.size(), 3u);
  EXPECT_EQ(parts[2], std::string("c"));
  const auto keep_empty = Split("a,,b", ',', false);
  EXPECT_EQ(keep_empty.size(), 3u);
  const auto any = SplitAny("1;2,3 4", ";, ");
  EXPECT_EQ(any.size(), 4u);
  EXPECT_EQ(Join({"x", "y", "z"}, " + "), std::string("x + y + z"));
}

TEST(Format, ReplaceAndTruncate) {
  EXPECT_EQ(ReplaceAll("http://a/b", "http", "https"), std::string("https://a/b"));
  EXPECT_EQ(ReplaceAll("aaa", "aa", "b"), std::string("ba"));
  EXPECT_EQ(Truncate("abcdef", 3), std::string("abc\xE2\x80\xA6"));
  EXPECT_EQ(Truncate("abc", 10), std::string("abc"));
  EXPECT_EQ(PadRight("ab", 4), std::string("ab  "));
  EXPECT_EQ(PadLeft("ab", 4), std::string("  ab"));
}

TEST(Format, SanitizeRemovesControlCharacters) {
  // NOTE: "\x01b" would parse as a single hex escape (0x1b), so build with a char literal.
  const std::string with_control = std::string("a") + '\x01' + "b\tc\nd";
  EXPECT_EQ(SanitizeText(with_control), std::string("ab c d"));
}

TEST(Format, ByteSizes) {
  EXPECT_EQ(FormatBytes(0), std::string("0.0 B"));
  EXPECT_EQ(FormatBytes(1023), std::string("1023.0 B"));
  EXPECT_EQ(FormatBytes(1024), std::string("1.0 KiB"));
  EXPECT_EQ(FormatBytes(1536, 1), std::string("1.5 KiB"));
  EXPECT_EQ(FormatBytes(1024ULL * 1024 * 250), std::string("250.0 MiB"));
  EXPECT_EQ(FormatBytes(1024ULL * 1024 * 1024 * 3, 0), std::string("3 GiB"));
}

TEST(Format, RatesAndPercents) {
  EXPECT_EQ(FormatRate(0), std::string("0 B/s"));
  EXPECT_EQ(FormatRate(2048), std::string("2.0 KiB/s"));
  EXPECT_EQ(FormatPercent(41.7), std::string("42%"));
  EXPECT_EQ(FormatPercent(41.7, 1), std::string("41.7%"));
}

TEST(Format, NumbersWithSeparators) {
  EXPECT_EQ(FormatNumber(0), std::string("0"));
  EXPECT_EQ(FormatNumber(999), std::string("999"));
  EXPECT_EQ(FormatNumber(1000), std::string("1,000"));
  EXPECT_EQ(FormatNumber(1234567), std::string("1,234,567"));
  EXPECT_EQ(FormatNumber(-4321), std::string("-4,321"));
  EXPECT_EQ(FormatDouble(3.14159, 2), std::string("3.14"));
  EXPECT_EQ(FormatSigned(2.5, 1), std::string("+2.5"));
  EXPECT_EQ(FormatSigned(-2.5, 1), std::string("-2.5"));
}

TEST(Format, TimeDurationsFps) {
  EXPECT_EQ(FormatMs(12.4), std::string("12 ms"));
  EXPECT_EQ(FormatMs(1500), std::string("1.50 s"));
  EXPECT_EQ(FormatMs(90000), std::string("1.5 min"));
  EXPECT_EQ(FormatDuration(45), std::string("45s"));
  EXPECT_EQ(FormatDuration(125), std::string("2m 05s"));
  EXPECT_EQ(FormatDuration(3600 + 59 * 60), std::string("1h 59m"));
  EXPECT_EQ(FormatFps(59.6), std::string("60 FPS"));
  EXPECT_EQ(FormatMemoryMiB(512), std::string("512 MiB"));
}

TEST(Format, Parsing) {
  bool b = false;
  EXPECT_TRUE(ParseBool("Yes", &b));
  EXPECT_TRUE(b);
  EXPECT_TRUE(ParseBool("OFF", &b));
  EXPECT_FALSE(b);
  EXPECT_FALSE(ParseBool("maybe", &b));

  int64_t i = 0;
  EXPECT_TRUE(ParseInt(" -42 ", &i));
  EXPECT_EQ(i, -42);
  EXPECT_FALSE(ParseInt("4x", &i));
  EXPECT_FALSE(ParseInt("", &i));

  double d = 0;
  EXPECT_TRUE(ParseDouble("2.5e3", &d));
  EXPECT_EQ(d, 2500.0);
  EXPECT_FALSE(ParseDouble("abc", &d));
}

TEST(Format, SizeParsingForSettings) {
  int64_t mb = 0;
  EXPECT_TRUE(ParseSizeMb("512", &mb));
  EXPECT_EQ(mb, 512);
  EXPECT_TRUE(ParseSizeMb("2GB", &mb));
  EXPECT_EQ(mb, 2048);
  EXPECT_TRUE(ParseSizeMb("1 gb", &mb));
  EXPECT_EQ(mb, 1024);
  EXPECT_TRUE(ParseSizeMb("1048576KB", &mb));
  EXPECT_EQ(mb, 1024);
  EXPECT_FALSE(ParseSizeMb("lots", &mb));
  EXPECT_FALSE(ParseSizeMb("-5", &mb));
}

TEST(Format, SparklineBucketCount) {
  std::vector<double> values(100);
  for (size_t i = 0; i < values.size(); ++i) values[i] = static_cast<double>(i) / 100.0;
  const std::string line = Sparkline(values, 10);
  EXPECT_EQ(line.size(), 30u);  // 10 x 3 UTF-8 bytes
  EXPECT_EQ(Sparkline({}, 10), std::string());
  EXPECT_EQ(Sparkline({0.0, 1.0}, 24).size(), 6u);
}
