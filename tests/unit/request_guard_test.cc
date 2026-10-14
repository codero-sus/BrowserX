// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/request_guard.h"

#include "test_harness.h"

using namespace browserx::core;

namespace {

Blocklist ShieldWithDefaults() {
  Blocklist shield;
  shield.AddDefaults();
  return shield;
}

RequestFacts Facts(std::string document, std::string host, ResourceType type,
                   std::string path = "/") {
  RequestFacts facts;
  facts.document_host = std::move(document);
  facts.request_host = std::move(host);
  facts.request_path = std::move(path);
  facts.type = type;
  return facts;
}

RuntimePolicy DefaultPolicy() { return RuntimePolicy{}; }

}  // namespace

TEST(RequestGuard, SameSiteUnderstandsSubdomains) {
  EXPECT_TRUE(SameSite("example.com", "example.com"));
  EXPECT_TRUE(SameSite("www.example.com", "example.com"));
  EXPECT_TRUE(SameSite("example.com", "cdn.example.com"));
  EXPECT_FALSE(SameSite("example.com", "example.net"));
  EXPECT_FALSE(SameSite("example.com", "notexample.com"));
  EXPECT_FALSE(SameSite("", "example.com")) << "an unknown document is not a same-site match";
}

TEST(RequestGuard, DefaultPolicyAndShieldAllowAnOrdinaryRequest) {
  const Blocklist shield = ShieldWithDefaults();
  const RuntimePolicy policy = DefaultPolicy();
  const RequestOutcome outcome =
      DecideRequest(shield, policy, Facts("example.com", "static.example.com", ResourceType::kImage));
  EXPECT_FALSE(outcome.block);
  EXPECT_TRUE(outcome.verdict == RequestVerdict::kAllow);
  EXPECT_TRUE(outcome.reason.empty());
}

TEST(RequestGuard, ShieldBlocksAKnownTrackerAndNamesItsCategory) {
  const Blocklist shield = ShieldWithDefaults();
  const RuntimePolicy policy = DefaultPolicy();
  const RequestOutcome outcome = DecideRequest(
      shield, policy, Facts("news.example", "www.google-analytics.com", ResourceType::kScript));
  EXPECT_TRUE(outcome.block);
  EXPECT_TRUE(outcome.verdict == RequestVerdict::kTracker);
  EXPECT_TRUE(outcome.category == BlockCategory::kAnalytics);
  EXPECT_TRUE(outcome.reason.find("analytics") != std::string::npos);
  EXPECT_TRUE(outcome.CountsAsBlocked());
}

TEST(RequestGuard, ShieldIsFinalEvenWhenTheModeWantsTheResource) {
  // A mode that blocks nothing (GAME, HIGH PERFORMANCE) must not re-open a tracker.
  const Blocklist shield = ShieldWithDefaults();
  RuntimePolicy permissive = DefaultPolicy();
  permissive.block_images = false;
  permissive.block_trackers = false;  // the flag is a mode preference, not an override
  const RequestOutcome outcome = DecideRequest(
      shield, permissive, Facts("news.example", "doubleclick.net", ResourceType::kImage));
  EXPECT_TRUE(outcome.block);
  EXPECT_TRUE(outcome.verdict == RequestVerdict::kTracker);
}

TEST(RequestGuard, DataSaverBlocksImagesBeforeTheyAreRequested) {
  const Blocklist shield = ShieldWithDefaults();
  RuntimePolicy policy = DefaultPolicy();
  policy.block_images = true;
  policy.block_media = true;
  policy.block_fonts = true;
  policy.block_third_party = true;

  EXPECT_TRUE(DecideRequest(shield, policy,
                            Facts("example.com", "example.com", ResourceType::kImage)).block);
  EXPECT_TRUE(DecideRequest(shield, policy,
                            Facts("example.com", "example.com", ResourceType::kMedia)).block);
  EXPECT_TRUE(DecideRequest(shield, policy,
                            Facts("example.com", "example.com", ResourceType::kFont)).block);

  // The pipeline the page needs to work is never touched by a data mode.
  EXPECT_FALSE(DecideRequest(shield, policy,
                             Facts("example.com", "example.com", ResourceType::kScript)).block);
  EXPECT_FALSE(DecideRequest(shield, policy,
                             Facts("example.com", "example.com", ResourceType::kStylesheet)).block);
  EXPECT_FALSE(DecideRequest(shield, policy,
                             Facts("example.com", "example.com", ResourceType::kXhr)).block);
}

TEST(RequestGuard, ThirdPartyPolicyNeverBlocksScriptsOrStylesheets) {
  // Blocking third-party scripts breaks most of the web, so no mode may do it by
  // accident: the policy can only reach images, media and fonts.
  const Blocklist shield = ShieldWithDefaults();
  RuntimePolicy policy = DefaultPolicy();
  policy.block_third_party = true;

  for (const ResourceType type :
       {ResourceType::kScript, ResourceType::kStylesheet, ResourceType::kXhr,
        ResourceType::kSubdocument}) {
    const RequestOutcome outcome =
        DecideRequest(shield, policy, Facts("example.com", "cdn.other.net", type));
    EXPECT_FALSE(outcome.block) << "third-party type " << static_cast<int>(type) << " was blocked";
  }
  EXPECT_TRUE(DecideRequest(shield, policy,
                            Facts("example.com", "cdn.other.net", ResourceType::kImage)).block);
  EXPECT_TRUE(DecideRequest(shield, policy,
                            Facts("example.com", "cdn.other.net", ResourceType::kFont)).block);
  // First-party images are still fine: the policy is about third parties.
  EXPECT_FALSE(DecideRequest(shield, policy,
                             Facts("example.com", "www.example.com", ResourceType::kImage)).block);
}

TEST(RequestGuard, TopLevelNavigationIsNeverBlockedByAPolicy) {
  const Blocklist shield = ShieldWithDefaults();
  RuntimePolicy policy = DefaultPolicy();
  policy.block_images = true;
  policy.block_media = true;
  policy.block_fonts = true;
  policy.block_third_party = true;

  const RequestOutcome outcome =
      DecideRequest(shield, policy, Facts("", "some-ad-network.example", ResourceType::kDocument));
  EXPECT_FALSE(outcome.block) << "the user typed that address; a data mode must not refuse it";
}

TEST(RequestGuard, AllowlistedSiteBypassesTheShieldButNotTheUsersDataChoice) {
  Blocklist shield = ShieldWithDefaults();
  shield.AllowlistHost("news.example");
  RuntimePolicy policy = DefaultPolicy();

  // The allowlist is about the shield; the mode's own resource policy still applies,
  // because "this site is not a tracker" is not "download all its video".
  const RequestOutcome tracker =
      DecideRequest(shield, policy, Facts("news.example", "www.google-analytics.com",
                                         ResourceType::kScript));
  EXPECT_FALSE(tracker.block);
  EXPECT_TRUE(tracker.verdict == RequestVerdict::kAllow);

  policy.block_media = true;
  const RequestOutcome media =
      DecideRequest(shield, policy, Facts("news.example", "news.example", ResourceType::kMedia));
  EXPECT_TRUE(media.block);
  EXPECT_TRUE(media.verdict == RequestVerdict::kPolicyMedia);
}

TEST(RequestGuard, ShieldOffAllowsEverythingItWouldBlock) {
  Blocklist shield = ShieldWithDefaults();
  shield.SetEnabled(false);
  RuntimePolicy policy = DefaultPolicy();
  const RequestOutcome outcome = DecideRequest(
      shield, policy, Facts("news.example", "www.google-analytics.com", ResourceType::kScript));
  EXPECT_FALSE(outcome.block) << "the shield switch in the toolbar must really turn it off";
}

TEST(RequestGuard, ByteEstimatesAreConservativeAndTypeAware) {
  // The status bar calls this an estimate, and it must not invent huge numbers for
  // small things: media above images above scripts above everything else.
  EXPECT_GT(EstimatedBytesForType(ResourceType::kMedia),
            EstimatedBytesForType(ResourceType::kImage));
  EXPECT_GT(EstimatedBytesForType(ResourceType::kImage),
            EstimatedBytesForType(ResourceType::kScript));
  EXPECT_GT(EstimatedBytesForType(ResourceType::kScript),
            EstimatedBytesForType(ResourceType::kOther));
  EXPECT_GE(EstimatedBytesForType(ResourceType::kOther), 1024u);
}
