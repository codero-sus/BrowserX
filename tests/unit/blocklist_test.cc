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

// The lookup index reordered how rules are *found*, so these tests pin the semantics
// that must not drift: which rule wins, where a host match may begin, and what a
// freshly added rule does to a list that is already in use.
TEST(Blocklist, RuleOrderStillDecidesTheCategory) {
  Blocklist list;
  list.AddRule("ads.example", BlockCategory::kAds);
  list.AddRule("ads.example", BlockCategory::kAnalytics);
  const BlockMatch match =
      list.DecideDetailed("news.example", "ads.example", "/x.js", ResourceType::kScript);
  EXPECT_TRUE(match.decision == BlockDecision::kBlock);
  EXPECT_TRUE(match.category == BlockCategory::kAds) << "the first rule on the page wins";
}

TEST(Blocklist, HostMatchesOnlyAtLabelBoundaries) {
  Blocklist list;
  list.AddRule("example.com", BlockCategory::kAds);
  EXPECT_TRUE(list.Decide("news.example", "example.com", "/a", ResourceType::kScript) ==
              BlockDecision::kBlock);
  EXPECT_TRUE(list.Decide("news.example", "cdn.example.com", "/a", ResourceType::kScript) ==
              BlockDecision::kBlock);
  // "notexample.com" ends with "example.com" as text but not as a domain.
  EXPECT_TRUE(list.Decide("news.example", "notexample.com", "/a", ResourceType::kScript) ==
              BlockDecision::kAllow);
}

TEST(Blocklist, WildcardAndExactPatternsBothStillWork) {
  Blocklist list;
  list.AddRule("*.wild.example", BlockCategory::kAds);
  list.AddRule("=exact.example", BlockCategory::kAds);
  EXPECT_TRUE(list.Decide("news.example", "a.wild.example", "/a", ResourceType::kScript) ==
              BlockDecision::kBlock);
  EXPECT_TRUE(list.Decide("news.example", "wild.example", "/a", ResourceType::kScript) ==
              BlockDecision::kBlock) << "a leading *. also covers the bare host";
  EXPECT_TRUE(list.Decide("news.example", "exact.example", "/a", ResourceType::kScript) ==
              BlockDecision::kBlock);
  EXPECT_TRUE(list.Decide("news.example", "sub.exact.example", "/a", ResourceType::kScript) ==
              BlockDecision::kAllow) << "=exact.example must not cover subdomains";
}

TEST(Blocklist, ARuleAddedAfterUseIsHonouredImmediately) {
  Blocklist list = MakeDefault();
  const std::string host = "late-addition.example";
  EXPECT_TRUE(list.Decide("news.example", host, "/a.js", ResourceType::kScript) ==
              BlockDecision::kAllow);
  list.AddRule(host, BlockCategory::kAds);
  EXPECT_TRUE(list.Decide("news.example", host, "/a.js", ResourceType::kScript) ==
              BlockDecision::kBlock) << "the index has to be rebuilt when the list changes";
}

TEST(Blocklist, PathRulesAreSlashAnchoredSoHostsNeedNoConcatenation) {
  // The decision path matches path rules against the request path alone (which is why a
  // request with no path cannot match one). That is only equivalent to matching
  // host+path while every path rule starts with "/" - if one ever does not, this fails
  // loudly instead of quietly letting ads through.
  const Blocklist list = MakeDefault();
  size_t path_rules = 0;
  for (const BlockRule& rule : list.rules()) {
    if (!rule.path_rule) continue;
    ++path_rules;
    EXPECT_TRUE(!rule.pattern.empty() && rule.pattern.front() == '/') << rule.pattern;
  }
  EXPECT_GT(path_rules, 0u) << "the default set is expected to carry path rules";
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

// --- importing a user's own list -------------------------------------------
// The importer's job is to be trusted: every line it does not understand must be counted
// as skipped, never approximated into a rule that pretends to work.
TEST(Blocklist, ImportsHostsFileLines) {
  Blocklist list;
  const Blocklist::ImportReport report = list.ImportRules(
      "# my hosts file\n"
      "0.0.0.0 ads.example.com\n"
      "127.0.0.1  tracker.example.net  # trailing comment\n");
  EXPECT_EQ(report.added, 2u);
  EXPECT_EQ(report.skipped, 0u);
  EXPECT_TRUE(list.DecideDetailed("news.example", "ads.example.com", "/a.js",
                                  ResourceType::kScript)
                  .decision == BlockDecision::kBlock);
  EXPECT_TRUE(list.DecideDetailed("news.example", "sub.tracker.example.net", "/a.png",
                                  ResourceType::kImage)
                  .decision == BlockDecision::kBlock)
      << "a host rule covers subdomains, as it does in every hosts-based blocker";
}

TEST(Blocklist, ImportsSimpleAbpHostRules) {
  Blocklist list;
  const Blocklist::ImportReport report =
      list.ImportRules("! title\n||ads.example^\n||cdn.tracker.example^\n");
  EXPECT_EQ(report.added, 2u);
  EXPECT_EQ(report.skipped, 0u);
  EXPECT_TRUE(list.DecideDetailed("site.example", "ads.example", "/x", ResourceType::kScript)
                  .decision == BlockDecision::kBlock);
}

TEST(Blocklist, SkipsSyntaxItDoesNotImplementInsteadOfGuessing) {
  Blocklist list;
  const Blocklist::ImportReport report = list.ImportRules(
      "example.com##.ad-banner\n"      // element hiding
      "||ads.example^$third-party\n"   // options
      "/banner\\d+\\.gif/\n"           // regex
      "|https://exact.example/|\n"     // exact anchor
      "0.0.0.0\n");                    // address with no name
  EXPECT_EQ(report.added, 0u);
  EXPECT_EQ(report.skipped, 5u);
  EXPECT_EQ(report.examples.size(), 5u) << "the user must be able to see what was skipped";
  EXPECT_TRUE(report.examples[0].find("##") != std::string::npos);
}

TEST(Blocklist, DeduplicatesWithinTheFileAndAgainstTheExistingList) {
  Blocklist list;
  list.AddRule("already.example", BlockCategory::kAds);
  const Blocklist::ImportReport report =
      list.ImportRules("0.0.0.0 already.example\n"
                       "0.0.0.0 new.example\n"
                       "||new.example^\n");
  EXPECT_EQ(report.added, 1u);
  EXPECT_EQ(report.duplicates, 2u);
  EXPECT_EQ(report.skipped, 0u);
}

TEST(Blocklist, NeverBlocksThisMachine) {
  Blocklist list;
  const Blocklist::ImportReport report =
      list.ImportRules("127.0.0.1 localhost\n0.0.0.0 localhost.localdomain\n0.0.0.0 router\n");
  EXPECT_EQ(report.added, 0u) << "blocking localhost breaks loopback pages and the updater";
  EXPECT_EQ(report.skipped, 3u);
}

TEST(Blocklist, ImportedRulesAreFoundImmediatelyAndClassifiedByArgument) {
  Blocklist list;
  const size_t before = list.rule_count();
  const Blocklist::ImportReport report =
      list.ImportRules("||metrics.example^\n", BlockCategory::kAnalytics);
  EXPECT_EQ(list.rule_count(), before + 1);
  EXPECT_EQ(report.added, 1u);
  const BlockMatch match =
      list.DecideDetailed("news.example", "metrics.example", "/v1", ResourceType::kScript);
  EXPECT_TRUE(match.decision == BlockDecision::kBlock);
  EXPECT_TRUE(match.category == BlockCategory::kAnalytics)
      << "an imported rule reports the category the user chose, not a guess";
}

TEST(Blocklist, HandlesCrLfAndAMissingTrailingNewline) {
  Blocklist list;
  const Blocklist::ImportReport report = list.ImportRules("0.0.0.0 one.example\r\n0.0.0.0 two.example");
  EXPECT_EQ(report.added, 2u) << "a file that ends without a newline still ends with a rule";
  EXPECT_EQ(report.skipped, 0u);
}

TEST(Blocklist, EmptyInputImportsNothingAndSaysSo) {
  Blocklist list;
  const size_t before = list.rule_count();
  const Blocklist::ImportReport report = list.ImportRules("");
  EXPECT_EQ(report.added, 0u);
  EXPECT_EQ(report.skipped, 0u);
  EXPECT_EQ(list.rule_count(), before);
}
