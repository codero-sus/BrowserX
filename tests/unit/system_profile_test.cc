#include "core/system_profile.h"

#include <string>

#include "test_harness.h"

using namespace browserx::core;

TEST(SystemProfile, ParsesMemInfo) {
  const char* meminfo =
      "MemTotal:       16331164 kB\n"
      "MemFree:         1234567 kB\n"
      "MemAvailable:    9876543 kB\n"
      "Buffers:          123456 kB\n"
      "SwapTotal:       2097148 kB\n"
      "SwapFree:        2097148 kB\n";
  uint64_t total = 0;
  uint64_t available = 0;
  uint64_t swap = 0;
  ParseMemInfoFile(meminfo, &total, &available, &swap);
  EXPECT_EQ(total, 15948u);      // 16331164 kB / 1024
  EXPECT_EQ(available, 9645u);   // prefers MemAvailable over MemFree
  EXPECT_EQ(swap, 2047u);
}

TEST(SystemProfile, FallsBackToMemFreeOnOldKernels) {
  const char* meminfo =
      "MemTotal:        8192000 kB\n"
      "MemFree:         2048000 kB\n";
  uint64_t total = 0;
  uint64_t available = 0;
  ParseMemInfoFile(meminfo, &total, &available, nullptr);
  EXPECT_EQ(total, 8000u);
  EXPECT_EQ(available, 2000u);
}

TEST(SystemProfile, EmptyOrGarbageMemInfoIsSafe) {
  uint64_t total = 999;
  uint64_t available = 999;
  ParseMemInfoFile("", &total, &available, nullptr);
  EXPECT_EQ(total, 0u);
  EXPECT_EQ(available, 0u);
  ParseMemInfoFile("MemTotal: not-a-number kB\n", &total, &available, nullptr);
  EXPECT_EQ(total, 0u);
}

TEST(SystemProfile, CountsCpusAndCores) {
  const char* cpuinfo =
      "processor\t: 0\n"
      "physical id\t: 0\n"
      "core id\t\t: 0\n"
      "model name\t: AMD Ryzen 7 5800H with Radeon Graphics\n"
      "\n"
      "processor\t: 1\n"
      "physical id\t: 0\n"
      "core id\t\t: 1\n"
      "model name\t: AMD Ryzen 7 5800H with Radeon Graphics\n"
      "\n"
      "processor\t: 2\n"
      "physical id\t: 0\n"
      "core id\t\t: 0\n"
      "model name\t: AMD Ryzen 7 5800H with Radeon Graphics\n"
      "\n"
      "processor\t: 3\n"
      "physical id\t: 0\n"
      "core id\t\t: 1\n"
      "model name\t: AMD Ryzen 7 5800H with Radeon Graphics\n";
  EXPECT_EQ(CountLogicalCpus(cpuinfo), 4);
  EXPECT_EQ(CountPhysicalCores(cpuinfo), 2);
  EXPECT_EQ(ExtractModelName(cpuinfo), std::string("AMD Ryzen 7 5800H with Radeon Graphics"));
}

TEST(SystemProfile, CoreCountSurvivesArmCpuinfoWithoutCoreIds) {
  const char* cpuinfo =
      "processor\t: 0\n"
      "BogoMIPS\t: 38.40\n"
      "\n"
      "processor\t: 1\n"
      "BogoMIPS\t: 38.40\n"
      "\n"
      "processor\t: 2\n"
      "BogoMIPS\t: 38.40\n";
  EXPECT_EQ(CountLogicalCpus(cpuinfo), 3);
  EXPECT_EQ(CountPhysicalCores(cpuinfo), 3);  // degrades gracefully
  EXPECT_EQ(ExtractModelName(cpuinfo), std::string());
}

TEST(SystemProfile, ParsesBatteryStatus) {
  EXPECT_TRUE(ParseBatteryStatus("Discharging") == BatteryState::kDischarging);
  EXPECT_TRUE(ParseBatteryStatus("Charging\n") == BatteryState::kCharging);
  EXPECT_TRUE(ParseBatteryStatus("Full") == BatteryState::kFull);
  EXPECT_TRUE(ParseBatteryStatus("Not charging") == BatteryState::kFull);
  EXPECT_TRUE(ParseBatteryStatus("") == BatteryState::kUnknown);
  EXPECT_TRUE(ParseBatteryStatus("nonsense") == BatteryState::kUnknown);
}

TEST(SystemProfile, ParsesOsRelease) {
  const char* os_release =
      "NAME=\"Debian GNU/Linux\"\n"
      "VERSION_ID=\"12\"\n"
      "PRETTY_NAME=\"Debian GNU/Linux 12 (bookworm)\"\n";
  std::string pretty;
  EXPECT_TRUE(ParseOsRelease(os_release, &pretty));
  EXPECT_EQ(pretty, std::string("Debian GNU/Linux 12 (bookworm)"));

  std::string fallback;
  EXPECT_TRUE(ParseOsRelease("NAME=\"Arch Linux\"\nVERSION_ID=\"2026.10\"\n", &fallback));
  EXPECT_EQ(fallback, std::string("Arch Linux 2026.10"));

  std::string none;
  EXPECT_FALSE(ParseOsRelease("ID=linux\n", &none));
}

TEST(SystemProfile, DerivesRecommendations) {
  SystemProfile tiny;
  tiny.total_ram_mb = 2048;
  tiny.cpu_physical = 2;
  tiny.shm_total_mb = 64;
  EXPECT_TRUE(tiny.IsVeryLowRam());
  EXPECT_TRUE(tiny.IsLowRam());
  EXPECT_TRUE(tiny.ShouldUseDevShmWorkaround());
  EXPECT_TRUE(tiny.RecommendedRendererLimit() >= 2);
  EXPECT_TRUE(tiny.RecommendedCacheSizeMb() <= 64);

  SystemProfile server;
  server.total_ram_mb = 131072;
  server.cpu_physical = 64;
  server.shm_total_mb = 4096;
  EXPECT_FALSE(server.IsLowRam());
  EXPECT_FALSE(server.ShouldUseDevShmWorkaround());
  EXPECT_TRUE(server.RecommendedRasterThreads() <= 4);
  EXPECT_TRUE(server.RecommendedCacheSizeMb() >= 512);
}

TEST(SystemProfile, UnknownRamNeverForcesALimit) {
  SystemProfile unknown;
  unknown.total_ram_mb = 0;
  EXPECT_FALSE(unknown.IsLowRam());
  EXPECT_EQ(unknown.RecommendedRendererLimit(), 0);
}

TEST(SystemProfile, DetectionRunsAndDescribesItself) {
  const SystemProfile profile = DetectSystemProfile();
  EXPECT_GT(profile.cpu_logical, 0);
  EXPECT_GT(profile.cpu_physical, 0);
  EXPECT_FALSE(profile.os_name.empty());
  const std::string description = DescribeProfile(profile);
  EXPECT_TRUE(description.find("System profile") != std::string::npos);
  EXPECT_TRUE(description.find("Memory") != std::string::npos);

  SystemProfile copy = profile;
  RefreshLiveMetrics(&copy);  // must not crash, and must not lose totals
  EXPECT_EQ(copy.total_ram_mb, profile.total_ram_mb);
}

TEST(SystemProfile, RefreshRateParsing) {
  const char* modetest_output =
      "CRTCs:\n"
      "  28  1920x1080   60.00  1920 2008 2052 2200  1080 1084 1089 1115\n"
      "  29  2560x1440  143.99  2560 2608 2640 2720  1440 1443 1448 1481\n";
  // The parser rounds to the nearest integer Hz and ignores mode timings that
  // are not plausible refresh rates (it must never mistake 1920 or 1080 for Hz).
  EXPECT_EQ(ParseRefreshRateFromModetest(modetest_output), 144);
  EXPECT_EQ(ParseRefreshRateFromModetest("  28  1920x1080   60.00  1920 2008 2052 2200  1080 1084 1089 1115\n"), 60);
  EXPECT_EQ(ParseRefreshRateFromModetest(""), 0);
  EXPECT_EQ(ParseRefreshRateFromModetest("nothing numeric here"), 0);
}
