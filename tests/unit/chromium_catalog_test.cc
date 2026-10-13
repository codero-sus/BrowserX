// Guards on the switch catalogue: these tests are what stop BrowserX from
// ever shipping a flag that does not exist in Chromium.

#include "core/chromium_catalog.h"

#include <set>
#include <string>

#include "test_harness.h"

using namespace browserx::core;

TEST(ChromiumCatalog, HasNoDuplicateEntries) {
  std::set<std::string> ids;
  for (const SwitchEntry& entry : AllSwitches()) {
    EXPECT_TRUE(ids.insert(entry.id).second) << "duplicate switch id: " << entry.id;
  }
  std::set<std::string> names;
  for (const FeatureEntry& entry : AllFeatures()) {
    EXPECT_TRUE(names.insert(entry.name).second) << "duplicate feature: " << entry.name;
  }
}

TEST(ChromiumCatalog, NoEntryHasNullStrings) {
  // A missing initialiser would otherwise blow up deep inside formatting code.
  for (const SwitchEntry& entry : AllSwitches()) {
    ASSERT_TRUE(entry.id != nullptr);
    ASSERT_TRUE(entry.source != nullptr);
    ASSERT_TRUE(entry.summary != nullptr);
    ASSERT_TRUE(entry.value_hint != nullptr);
  }
  for (const FeatureEntry& entry : AllFeatures()) {
    ASSERT_TRUE(entry.name != nullptr);
    ASSERT_TRUE(entry.source != nullptr);
    ASSERT_TRUE(entry.summary != nullptr);
  }
}

TEST(ChromiumCatalog, EveryVerifiedEntryCitesARealSourceFile) {
  for (const SwitchEntry& entry : AllSwitches()) {
    if (entry.provenance != Provenance::kVerified && entry.provenance != Provenance::kChromeOnly) {
      continue;
    }
    EXPECT_TRUE(entry.source != nullptr && entry.source[0] != '\0')
        << entry.id << " has no source citation";
    EXPECT_TRUE(std::string(entry.source).find("(absent)") == std::string::npos)
        << entry.id << " claims to be verified but has no source";
    EXPECT_TRUE(entry.summary != nullptr && entry.summary[0] != '\0')
        << entry.id << " has no human explanation";
    EXPECT_TRUE(FindSwitch(entry.id) != nullptr);
  }
}

TEST(ChromiumCatalog, VerifiedSwitchesAreApplicableAndRemovedOnesAreNot) {
  for (const SwitchEntry& entry : AllSwitches()) {
    if (entry.provenance == Provenance::kRemoved) {
      EXPECT_FALSE(IsApplicable(entry)) << entry.id << " is catalogued as removed but applicable";
    }
    if (entry.provenance == Provenance::kVerified && entry.risk == Risk::kSafe) {
      EXPECT_TRUE(IsApplicable(entry));
      EXPECT_FALSE(RequiresOptIn(entry)) << entry.id << " is safe but requires opt-in";
    }
  }
}

TEST(ChromiumCatalog, KnownMythsAreMarkedRemoved) {
  // These four are the flags every "speed up Chrome" blog post recommends and
  // that no longer exist. BrowserX must never apply them.
  const char* myths[] = {"memory-pressure-off", "enable-quic", "enable-zero-copy",
                         "enable-oop-rasterization"};
  for (const char* myth : myths) {
    const SwitchEntry* entry = FindSwitch(myth);
    ASSERT_TRUE_OR_RETURN(entry != nullptr);
    EXPECT_TRUE(entry->provenance == Provenance::kRemoved) << myth << " should be catalogued removed";
    EXPECT_FALSE(IsApplicable(*entry));
  }
}

TEST(ChromiumCatalog, KnownGoodFlagsAreVerified) {
  const char* good[] = {"enable-low-end-device-mode", "renderer-process-limit", "process-per-site",
                        "disable-gpu-vsync", "disable-frame-rate-limit", "disable-dev-shm-usage",
                        "skia-font-cache-limit-mb", "mse-video-buffer-size-limit-mb",
                        "disable-ipc-flooding-protection", "no-sandbox"};
  for (const char* id : good) {
    const SwitchEntry* entry = FindSwitch(id);
    ASSERT_TRUE_OR_RETURN(entry != nullptr);
    EXPECT_TRUE(entry->provenance == Provenance::kVerified || entry->provenance == Provenance::kChromeOnly)
        << id << " should be verified";
    EXPECT_TRUE(IsApplicable(*entry));
  }
}

TEST(ChromiumCatalog, LookupsAndHelpers) {
  EXPECT_TRUE(FindSwitch("this-does-not-exist") == nullptr);
  EXPECT_TRUE(FindFeature("NetworkServiceInProcess2") != nullptr);
  EXPECT_TRUE(FindFeature("NotAFeature") == nullptr);
  EXPECT_EQ(std::string(ProvenanceLabel(Provenance::kVerified)), std::string("verified"));
  EXPECT_EQ(std::string(ScopeLabel(Scope::kGpu)), std::string("gpu"));
  EXPECT_EQ(std::string(RiskLabel(Risk::kLab)), std::string("lab"));

  const std::string url = SourceSearchUrl("disable-gpu-vsync");
  EXPECT_TRUE(url.find("source.chromium.org") != std::string::npos);
  EXPECT_TRUE(url.find("disable-gpu-vsync") != std::string::npos);
}

TEST(ChromiumCatalog, StatsMatchTheTable) {
  const CatalogStats stats = GetCatalogStats();
  EXPECT_EQ(stats.switches_total, AllSwitches().size());
  EXPECT_EQ(stats.features_total, AllFeatures().size());
  EXPECT_EQ(stats.switches_verified + stats.switches_chrome_only + stats.switches_legacy +
                stats.switches_removed,
            stats.switches_total);
  EXPECT_GT(stats.switches_verified, 50u) << "the catalogue should be mostly verified flags";
}

TEST(ChromiumCatalog, ReportRendersAndHidesRemovedByDefault) {
  const std::string report = RenderCatalogReport(false);
  EXPECT_TRUE(report.find("disable-gpu-vsync") != std::string::npos);
  EXPECT_TRUE(report.find("memory-pressure-off") == std::string::npos)
      << "removed switches must be hidden from the default report";
  const std::string full = RenderCatalogReport(true);
  EXPECT_TRUE(full.find("memory-pressure-off") != std::string::npos);
}

TEST(ChromiumCatalog, ValueSwitchesDeclareTheirType) {
  // Switches that take a value must be declared as such, so the plan builder
  // can never emit "--renderer-process-limit" without a number.
  const char* value_switches[] = {"renderer-process-limit", "disk-cache-size",
                                  "num-raster-threads",  "skia-font-cache-limit-mb",
                                  "mse-video-buffer-size-limit-mb"};
  for (const char* id : value_switches) {
    const SwitchEntry* entry = FindSwitch(id);
    ASSERT_TRUE_OR_RETURN(entry != nullptr);
    EXPECT_TRUE(entry->requires_value) << id << " should require a value";
  }
  const SwitchEntry* boolean = FindSwitch("disable-gpu-vsync");
  ASSERT_TRUE_OR_RETURN(boolean != nullptr);
  EXPECT_FALSE(boolean->requires_value);
}
