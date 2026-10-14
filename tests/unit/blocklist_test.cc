// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/blocklist.h"

#include "test_harness.h"

using namespace browserx::core;

namespace {
Blocklist MakeDefault() {
  Blocklist list;
  list.AddDefaults();
  return list;
}
}  // namespace

TEST(Blocklist, DefaultsAreLoadedAndDeduplicated) {
  const Blocklist list = MakeDefault();
  EXPECT_GT(list.rule_count(), 60u);
  EXPECT_LT(list.rule_count(), 200u) << "the shield stays curated, not a giant list";
}

TEST(Blocklist, BlocksKnownAdAndTrackerHosts) {
  const Blocklist list = MakeDefault();
  EXPECT_TRUE(list.Decide("news.example", "ads.doubleclick.net", "/x.js", ResourceType::kScript) ==
              BlockDecision::kBlock);
  EXPECT_TRUE(list.Decide("news.example", "www.google-analytics.com", "/collect?v=1",
                          ResourceType::kXhr) == BlockDecision::kBlock);
  EXPECT_TRUE(list.Decide("news.example", "connect.facebook.net", "/en_US/sdk.js",
                          ResourceType::kScript) == BlockDecision::kBlock);
  EXPECT_TRUE(list.Decide("news.example", "coinhive.com", "/lib/miner.js",
                          ResourceType::kScript) == BlockDecision::kBlock);
}

TEST(Blocklist, AllowsNormalSites) {
  const Blocklist list = MakeDefault();
  EXPECT_TRUE(list.Decide("news.example", "news.example", "/style.css",
                          ResourceType::kStylesheet) == BlockDecision::kAllow);
  EXPECT_TRUE(list.Decide("news.example", "cdn.jsdelivr.net", "/npm/x.js",
                          ResourceType::kScript) == BlockDecision::kAllow);
  EXPECT_TRUE(list.Decide("news.example", "fonts.gstatic.com", "/s/x.woff2",
                          ResourceType::kFont) == BlockDecision::kAllow);
  EXPECT_TRUE(list.Decide("", "", "", ResourceType::kDocument) == BlockDecision::kAllow);
}

TEST(Blocklist, FirstPartyIsNeverBlockedByThirdPartyOnlyRules) {
  const Blocklist list = MakeDefault();
  // Simulating a page on doubleclick.net itself (rare but legal) - the rule is
  // third-party-only, so first-party requests must pass.
  EXPECT_TRUE(list.Decide("doubleclick.net", "doubleclick.net", "/pagead/x",
                          ResourceType::kScript) == BlockDecision::kAllow);
  // ... but a third-party page loading from it is blocked.
  EXPECT_TRUE(list.Decide("example.com", "doubleclick.net", "/pagead/x",
                          ResourceType::kScript) == BlockDecision::kBlock);
}

TEST(Blocklist, PathRulesCatchAdsOnLegitimateHosts) {
  const Blocklist list = MakeDefault();
  EXPECT_TRUE(list.Decide("example.com", "cdn.example.net", "/pagead/slot.js",
                          ResourceType::kScript) == BlockDecision::kBlock);
  EXPECT_TRUE(list.Decide("example.com", "cdn.example.net", "/assets/gtag/js",
                          ResourceType::kScript) == BlockDecision::kBlock);
  EXPECT_TRUE(list.Decide("example.com", "cdn.example.net", "/assets/app.js",
                          ResourceType::kScript) == BlockDecision::kAllow);
}

TEST(Blocklist, AllowlistOverridesEverything) {
  Blocklist list = MakeDefault();
  list.AllowlistHost("DoubleClick.net");
  EXPECT_TRUE(list.Decide("news.example", "ads.doubleclick.net", "/x.js",
                          ResourceType::kScript) == BlockDecision::kAllowlisted);
  // Subdomains of the allowlisted host are covered too.
  EXPECT_TRUE(list.Decide("news.example", "sub.doubleclick.net", "/x.js",
                          ResourceType::kScript) == BlockDecision::kAllowlisted);
  list.RemoveAllowlistHost("doubleclick.net");
  EXPECT_TRUE(list.Decide("news.example", "ads.doubleclick.net", "/x.js",
                          ResourceType::kScript) == BlockDecision::kBlock);
  list.AllowlistHost("a.com");
  list.AllowlistHost("b.com");
  EXPECT_EQ(list.allowlist().size(), 2u);
  list.ClearAllowlist();
  EXPECT_EQ(list.allowlist().size(), 0u);
}

TEST(Blocklist, DisabledShieldAllowsEverything) {
  Blocklist list = MakeDefault();
  list.SetEnabled(false);
  EXPECT_TRUE(list.Decide("news.example", "ads.doubleclick.net", "/x.js",
                          ResourceType::kScript) == BlockDecision::kDisabled);
}

TEST(Blocklist, StatisticsTrackSavings) {
  Blocklist list = MakeDefault();
  list.NoteBlocked(BlockCategory::kAds, 50000);
  list.NoteBlocked(BlockCategory::kAds, 20000);
  list.NoteBlocked(BlockCategory::kAnalytics, 15000);
  EXPECT_EQ(list.blocked_total(), 3u);
  EXPECT_EQ(list.bytes_saved_estimate(), 85000u);
  EXPECT_EQ(list.blocked_in_category(BlockCategory::kAds), 2u);
  EXPECT_EQ(list.blocked_in_category(BlockCategory::kAnalytics), 1u);
  EXPECT_EQ(list.blocked_in_category(BlockCategory::kCrypto), 0u);
  list.ResetStatistics();
  EXPECT_EQ(list.blocked_total(), 0u);
  EXPECT_EQ(list.bytes_saved_estimate(), 0u);
}

TEST(Blocklist, HeavyResourceClassification) {
  EXPECT_TRUE(Blocklist::IsHeavyResource(ResourceType::kImage));
  EXPECT_TRUE(Blocklist::IsHeavyResource(ResourceType::kMedia));
  EXPECT_TRUE(Blocklist::IsHeavyResource(ResourceType::kFont));
  EXPECT_FALSE(Blocklist::IsHeavyResource(ResourceType::kScript));
  EXPECT_FALSE(Blocklist::IsHeavyResource(ResourceType::kXhr));
}

TEST(Blocklist, CategoryLabelsExist) {
  EXPECT_EQ(std::string(BlockCategoryLabel(BlockCategory::kAds)), std::string("ads"));
  EXPECT_EQ(std::string(BlockCategoryLabel(BlockCategory::kCrypto)), std::string("crypto miners"));
}

TEST(Blocklist, AllowlistingASiteAlsoAllowsWhatThatSiteLoads) {
  // A user who clicks "allow this site" means "stop blocking on the pages of this
  // site". Matching only the request host made that a no-op in practice, because the
  // requests a rule matches are third-party ones coming *from* the allowlisted page.
  Blocklist list;
  list.AddDefaults();
  list.AllowlistHost("news.example");

  EXPECT_TRUE(list.Decide("news.example", "www.google-analytics.com", "/collect",
                          ResourceType::kScript) == BlockDecision::kAllowlisted);
  // The allowlist is per site: another site's pages are still protected.
  EXPECT_TRUE(list.Decide("other.example", "www.google-analytics.com", "/collect",
                          ResourceType::kScript) == BlockDecision::kBlock);
  // And the matched category still comes back for the statistics.
  const BlockMatch match =
      list.DecideDetailed("other.example", "www.google-analytics.com", "/collect",
                          ResourceType::kScript);
  EXPECT_TRUE(match.decision == BlockDecision::kBlock);
  EXPECT_TRUE(match.category == BlockCategory::kAnalytics);
  EXPECT_TRUE(match.matched_rule);
}
