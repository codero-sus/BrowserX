#include "core/blocklist.h"

#include <algorithm>

#include "core/format.h"
#include "core/url_utils.h"

namespace mybrowser {
namespace core {
namespace {

// Curated third-party endpoints. Kept deliberately short and legible: every
// entry is something a normal user will never need for a page to work.
constexpr const char* kAdHosts[] = {
    "doubleclick.net", "googlesyndication.com", "googleadservices.com",
    "adservice.google.com", "adsystem.amazon.com", "amazon-adsystem.com",
    "criteo.com", "criteo.net", "taboola.com", "outbrain.com", "pubmatic.com",
    "rubiconproject.com", "magnite.com", "openx.net", "casalemedia.com",
    "adnxs.com", "appnexus.com", "smartadserver.com", "teads.tv", "sharethrough.com",
    "moatads.com", "adform.net", "bidswitch.net", "districtm.io", "indexww.com",
    "sovrn.com", "lijit.com", "gumgum.com", "yieldmo.com", "triplelift.com",
    "360yield.com", "improvedigital.com", "conversantmedia.com", "media.net",
    "undertone.com", "zedo.com", "adroll.com", "revcontent.com", "mgid.com",
    "zedo.com", "yieldlab.net", "3lift.com", "adsrvr.org", "1rx.io",
    "advertising.com", "pixel.ad", "serving-sys.com", "innovid.com",
    "springserve.com", "spotxchange.com", "freewheel.tv", "vungle.com",
    "chartboost.com", "unityads.unity3d.com", "applovin.com", "ironsrc.com",
};

constexpr const char* kAnalyticsHosts[] = {
    "google-analytics.com", "analytics.google.com", "googletagmanager.com",
    "googletagservices.com", "scorecardresearch.com", "quantserve.com",
    "hotjar.com", "hotjar.io", "mouseflow.com", "crazyegg.com", "fullstory.com",
    "mixpanel.com", "segment.io", "segment.com", "amplitude.com", "heapanalytics.com",
    "kissmetrics.com", "chartbeat.com", "chartbeat.net", "parsely.com",
    "newrelic.com", "nr-data.net", "bugsnag.com", "stats.wp.com", "quantcount.com",
    "matomo.cloud", "clicky.com", "statcounter.com", "histats.com", "gemius.pl",
    "adobedtm.com", "omtrdc.net", "demdex.net", "everesttech.net",
    "branch.io", "adjust.com", "appsflyer.com", "kochava.com", "singular.net",
    "mmstat.com", "umeng.com", "umengcloud.com", "talkingdata.com",
    "clarity.ms", "bat.bing.com", "c.bing.com", "pixel.rubiconproject.com",
};

constexpr const char* kSocialTrackers[] = {
    "connect.facebook.net", "graph.facebook.com", "pixel.facebook.com",
    "platform.twitter.com", "syndication.twitter.com", "analytics.twitter.com",
    "ads.linkedin.com", "px.ads.linkedin.com", "snap.licdn.com",
    "analytics.tiktok.com", "business-api.tiktok.com", "ads.pinterest.com",
    "ct.pinterest.com", "tr.snapchat.com", "sc-static.net",
};

constexpr const char* kCryptoMiners[] = {
    "coinhive.com", "coin-hive.com", "crypto-loot.com", "cryptoloot.pro",
    "coinimp.com", "webminepool.com", "webmine.cz", "authedmine.com",
    "minero.cc", "jsecoin.com", "deepMiner.js", "monerominer.rocks",
    "coolmining.org", "hashvault.pro", "minexmr.com", "nanopool.org",
};

constexpr const char* kFingerprintHosts[] = {
    "fingerprintjs.com", "fpjs.io", "fpapi.io", "fingerprint.com",
    "deviceatlas.com", "iovation.com", "threatmetrix.com", "maxmind.com",
    "sift.com", "siftscience.com", "perimeterx.net", "px-cloud.net",
};

// Path fragments (matched against host+path) for endpoints that live on
// otherwise legitimate hosts.
struct PathRule {
  const char* fragment;
  BlockCategory category;
};
constexpr PathRule kPathRules[] = {
    {"/pagead/", BlockCategory::kAds},
    {"/ads/", BlockCategory::kAds},
    {"/adserver/", BlockCategory::kAds},
    {"/advert/", BlockCategory::kAds},
    {"/banners/", BlockCategory::kAds},
    {"/sponsored/", BlockCategory::kAds},
    {"/analytics.js", BlockCategory::kAnalytics},
    {"/gtag/js", BlockCategory::kAnalytics},
    {"/collect?v=", BlockCategory::kAnalytics},
    {"/piwik.php", BlockCategory::kAnalytics},
    {"/matomo.js", BlockCategory::kAnalytics},
    {"/beacon?", BlockCategory::kAnalytics},
    {"/track?", BlockCategory::kAnalytics},
    {"/pixel.gif", BlockCategory::kAnalytics},
    {"/log?format=json", BlockCategory::kAnalytics},
    {"/coinhive.min.js", BlockCategory::kCrypto},
    {"/cryptonight.js", BlockCategory::kCrypto},
    {"/webmine.js", BlockCategory::kCrypto},
};

}  // namespace

const char* BlockCategoryLabel(BlockCategory category) {
  switch (category) {
    case BlockCategory::kAds: return "ads";
    case BlockCategory::kAnalytics: return "analytics";
    case BlockCategory::kSocial: return "social trackers";
    case BlockCategory::kCrypto: return "crypto miners";
    case BlockCategory::kFingerprint: return "fingerprinting";
  }
  return "?";
}

void Blocklist::AddRule(std::string pattern, BlockCategory category, bool third_party_only,
                        bool path_rule) {
  BlockRule rule;
  rule.pattern = fmt::ToLower(std::move(pattern));
  rule.category = category;
  rule.third_party_only = third_party_only;
  rule.path_rule = path_rule;
  rules_.push_back(std::move(rule));
}

void Blocklist::AddDefaults() {
  rules_.clear();
  for (const char* host : kAdHosts) AddRule(host, BlockCategory::kAds);
  for (const char* host : kAnalyticsHosts) AddRule(host, BlockCategory::kAnalytics);
  for (const char* host : kSocialTrackers) AddRule(host, BlockCategory::kSocial);
  for (const char* host : kCryptoMiners) AddRule(host, BlockCategory::kCrypto, /*third_party=*/false);
  for (const char* host : kFingerprintHosts) AddRule(host, BlockCategory::kFingerprint);
  for (const PathRule& rule : kPathRules) {
    AddRule(rule.fragment, rule.category, /*third_party_only=*/true, /*path_rule=*/true);
  }
  // Deduplicate (the curated lists intentionally have a couple of repeats).
  std::sort(rules_.begin(), rules_.end(), [](const BlockRule& a, const BlockRule& b) {
    if (a.pattern != b.pattern) return a.pattern < b.pattern;
    return static_cast<int>(a.category) < static_cast<int>(b.category);
  });
  rules_.erase(std::unique(rules_.begin(), rules_.end(),
                           [](const BlockRule& a, const BlockRule& b) {
                             return a.pattern == b.pattern && a.category == b.category &&
                                    a.path_rule == b.path_rule;
                           }),
               rules_.end());
}

void Blocklist::AllowlistHost(std::string host) {
  const std::string normalized = fmt::ToLower(fmt::Trim(host));
  if (normalized.empty()) return;
  if (std::find(allowlist_.begin(), allowlist_.end(), normalized) != allowlist_.end()) return;
  allowlist_.push_back(normalized);
}

void Blocklist::RemoveAllowlistHost(std::string host) {
  const std::string normalized = fmt::ToLower(fmt::Trim(host));
  allowlist_.erase(std::remove(allowlist_.begin(), allowlist_.end(), normalized), allowlist_.end());
}

void Blocklist::ClearAllowlist() { allowlist_.clear(); }

BlockDecision Blocklist::Decide(std::string_view document_host, std::string_view request_host,
                                std::string_view request_path, ResourceType type) const {
  if (!enabled_) return BlockDecision::kDisabled;

  // Never touch first-party document loads or MyBrowser's own pages.
  const std::string request_host_text = fmt::ToLower(std::string(request_host));
  if (request_host_text.empty()) return BlockDecision::kAllow;
  if (type == ResourceType::kDocument && document_host.empty()) return BlockDecision::kAllow;

  const std::string document_host_text = fmt::ToLower(std::string(document_host));
  const bool third_party =
      document_host_text.empty() || !url::HostMatchesPattern(request_host_text, document_host_text);
  // A request to the exact host of the page is first-party by definition.
  const bool same_host = !document_host_text.empty() && request_host_text == document_host_text;

  for (const std::string& allowed : allowlist_) {
    if (url::HostMatchesPattern(request_host_text, allowed)) return BlockDecision::kAllowlisted;
  }

  const std::string host_and_path = request_host_text + std::string(request_path);
  for (const BlockRule& rule : rules_) {
    if (rule.third_party_only && (!third_party || same_host)) continue;
    const bool matches = rule.path_rule
                             ? host_and_path.find(rule.pattern) != std::string::npos
                             : url::HostMatchesPattern(request_host_text, rule.pattern);
    if (matches) return BlockDecision::kBlock;
  }
  return BlockDecision::kAllow;
}

void Blocklist::NoteBlocked(BlockCategory category, uint64_t bytes_estimate) {
  ++blocked_total_;
  bytes_saved_ += bytes_estimate;
  const int index = static_cast<int>(category);
  if (index >= 0 && index < 5) ++category_counts_[index];
}

uint64_t Blocklist::blocked_in_category(BlockCategory category) const {
  const int index = static_cast<int>(category);
  if (index < 0 || index >= 5) return 0;
  return category_counts_[index];
}

void Blocklist::ResetStatistics() {
  blocked_total_ = 0;
  bytes_saved_ = 0;
  for (uint64_t& count : category_counts_) count = 0;
}

bool Blocklist::IsHeavyResource(ResourceType type) {
  switch (type) {
    case ResourceType::kImage:
    case ResourceType::kMedia:
    case ResourceType::kFont:
      return true;
    default:
      return false;
  }
}

}  // namespace core
}  // namespace mybrowser
