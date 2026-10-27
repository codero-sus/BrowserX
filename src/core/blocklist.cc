// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/blocklist.h"

#include <algorithm>
#include <string_view>

#include "core/format.h"
#include "core/url_utils.h"

namespace browserx {
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
  AppendRule(std::move(pattern), category, third_party_only, path_rule);
  RebuildIndex();
}

void Blocklist::AppendRule(std::string pattern, BlockCategory category, bool third_party_only,
                           bool path_rule) {
  BlockRule rule;
  rule.pattern = fmt::ToLower(fmt::Trim(std::move(pattern)));
  rule.category = category;
  rule.third_party_only = third_party_only;
  rule.path_rule = path_rule;
  rules_.push_back(std::move(rule));
}

void Blocklist::RebuildIndex() {
  suffix_index_.clear();
  exact_index_.clear();
  path_rule_indices_.clear();
  other_rule_indices_.clear();

  for (size_t i = 0; i < rules_.size(); ++i) {
    const BlockRule& rule = rules_[i];
    if (rule.path_rule) {
      path_rule_indices_.push_back(i);
      continue;
    }
    const std::string& pattern = rule.pattern;
    if (pattern.empty()) continue;
    if (pattern.front() == '=') {
      exact_index_[pattern.substr(1)].push_back(i);
    } else if (pattern.size() > 2 && pattern[0] == '*' && pattern[1] == '.') {
      suffix_index_[pattern.substr(2)].push_back(i);
    } else if (pattern.find('*') == std::string::npos) {
      suffix_index_[pattern].push_back(i);
    } else {
      // A wildcard anywhere but the leading "*." is not something the index can
      // answer, so it falls back to the linear scan.
      other_rule_indices_.push_back(i);
    }
  }
}

void Blocklist::AddDefaults() {
  rules_.clear();
  // AppendRule, not AddRule: the index is built once below instead of 163 times.
  for (const char* host : kAdHosts) AppendRule(host, BlockCategory::kAds);
  for (const char* host : kAnalyticsHosts) AppendRule(host, BlockCategory::kAnalytics);
  for (const char* host : kSocialTrackers) AppendRule(host, BlockCategory::kSocial);
  for (const char* host : kCryptoMiners) {
    AppendRule(host, BlockCategory::kCrypto, /*third_party=*/false);
  }
  for (const char* host : kFingerprintHosts) AppendRule(host, BlockCategory::kFingerprint);
  for (const PathRule& rule : kPathRules) {
    AppendRule(rule.fragment, rule.category, /*third_party_only=*/true, /*path_rule=*/true);
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
  // Rebuilt here as well as in AddRule(): this function reorders and drops rules
  // *after* adding them, which leaves any index built during the loop pointing at the
  // wrong entries. That is not a theoretical hazard - it silently disabled the shield
  // and read past the end of the rule vector.
  RebuildIndex();
}

namespace {

// A host rule has to be a host. Without this check a line like `example.com##.ad-banner`
// (element hiding, not a host rule) would be imported as a rule for a "host" called
// `example.com##.ad-banner`, which matches nothing and quietly inflates the rule count.
bool LooksLikeHost(const std::string& host) {
  if (host.empty() || host.size() > 253) return false;
  if (host.front() == '.' || host.front() == '-' || host.back() == '.' || host.back() == '-') {
    return false;
  }
  bool only_address_chars = true;
  for (const char c : host) {
    const bool alpha = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (!alpha && c != '.' && c != '-') return false;
    if (c != '.' && !(c >= '0' && c <= '9') && c != ':') only_address_chars = false;
  }
  if (host.find("..") != std::string::npos) return false;
  return !only_address_chars;  // 0.0.0.0 / 127.0.0.1 are addresses, not names
}

}  // namespace

Blocklist::ImportReport Blocklist::ImportRules(std::string_view text,
                                                BlockCategory category) {
  ImportReport report;
  std::vector<std::string> seen;  // patterns already in the list, for duplicate counting
  for (const BlockRule& rule : rules_) {
    if (!rule.path_rule) seen.push_back(rule.pattern);
  }

  size_t line_start = 0;
  while (line_start < text.size()) {
    size_t line_end = text.find('\n', line_start);
    if (line_end == std::string_view::npos) line_end = text.size();
    std::string line(fmt::Trim(text.substr(line_start, line_end - line_start)));
    line_start = line_end + 1;  // one past the newline; == size() ends the loop
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    // Comments: hosts files use '#', filter lists use '!'. Both are common enough that
    // rejecting either would look like the importer was broken.
    if (line[0] == '#' || line[0] == '!') continue;
    // Hosts-file section markers ([Adblock Plus 2.0]) are headers, not rules.
    if (line.front() == '[' && line.back() == ']') continue;

    std::string host;
    if (line.rfind("||", 0) == 0) {
      // ||ads.example^  - the anchor means "this host and its subdomains" in ABP, which
      // is what a host-suffix match already does here. Only a bare host rule is
      // understood; anything carrying options or wildcards is skipped, not approximated.
      std::string rest = line.substr(2);
      if (!rest.empty() && rest.back() == '^') rest.pop_back();
      if (rest.empty() || rest.find_first_of("^*/$|") != std::string::npos ||
          rest.find_first_of(" \t") != std::string::npos) {
        ++report.skipped;
        if (report.examples.size() < 5) report.examples.push_back(line);
        continue;
      }
      host = rest;
    } else if (line[0] == '|') {
      // Single-pipe anchors (||...| exact, |... prefix) are not host rules.
      ++report.skipped;
      if (report.examples.size() < 5) report.examples.push_back(line);
      continue;
    } else {
      // Hosts file: an address, whitespace, then one or more names.
      size_t space = line.find_first_of(" \t");
      if (space == std::string::npos) {
        // A bare domain is a reasonable thing for a person to type into a rule file.
        host = line;
      } else {
        const std::string address = line.substr(0, space);
        if (address != "0.0.0.0" && address != "127.0.0.1" && address != "::" &&
            address != "::1") {
          ++report.skipped;  // a real address mapping is not an ad rule
          if (report.examples.size() < 5) report.examples.push_back(line);
          continue;
        }
        size_t name_start = line.find_first_not_of(" \t", space);
        if (name_start == std::string::npos) {
          ++report.skipped;
          if (report.examples.size() < 5) report.examples.push_back(line);
          continue;
        }
        size_t name_end = line.find_first_of(" \t", name_start);
        host = line.substr(name_start, name_end == std::string::npos
                                         ? std::string::npos
                                         : name_end - name_start);
      }
    }

    host = fmt::ToLower(fmt::Trim(host));
    if (host == "localhost" || host == "localhost.localdomain" || host.empty()) {
      ++report.skipped;  // never block the machine you are on
      continue;
    }
    if (host.find('.') == std::string::npos || !LooksLikeHost(host)) {
      ++report.skipped;  // a LAN name, an address, or syntax this importer does not read
      if (report.examples.size() < 5) report.examples.push_back(line);
      continue;
    }
    if (std::find(seen.begin(), seen.end(), host) != seen.end()) {
      ++report.duplicates;
      continue;
    }
    seen.push_back(host);
    AppendRule(host, category);
    ++report.added;
  }
  if (report.added > 0) RebuildIndex();
  return report;
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
  return DecideDetailed(document_host, request_host, request_path, type).decision;
}

BlockMatch Blocklist::DecideDetailed(std::string_view document_host,
                                     std::string_view request_host,
                                     std::string_view request_path,
                                     ResourceType type) const {
  BlockMatch result;
  if (!enabled_) {
    result.decision = BlockDecision::kDisabled;
    return result;
  }

  // Never touch first-party document loads or BrowserX's own pages.
  const std::string request_host_text = fmt::ToLower(std::string(request_host));
  if (request_host_text.empty()) return result;
  if (type == ResourceType::kDocument && document_host.empty()) return result;

  const std::string document_host_text = fmt::ToLower(std::string(document_host));
  const bool third_party =
      document_host_text.empty() || !url::HostMatchesPattern(request_host_text, document_host_text);
  // A request to the exact host of the page is first-party by definition.
  const bool same_host = !document_host_text.empty() && request_host_text == document_host_text;

  // The allowlist is about the *sites the user browses*, so it is matched against
  // the document host first: "allow news.example" has to stop the shield on the
  // requests that site makes, which are exactly the ones a rule would block. The
  // request host is matched as well, because a top-level navigation has no
  // document host yet and the user expects the address they allowlisted to load.
  for (const std::string& allowed : allowlist_) {
    const bool document_allowed =
        !document_host_text.empty() && url::HostMatchesPattern(document_host_text, allowed);
    if (document_allowed || url::HostMatchesPattern(request_host_text, allowed)) {
      result.decision = BlockDecision::kAllowlisted;
      return result;
    }
  }

  // Indexed lookup, and the semantics are deliberately identical to the linear scan
  // it replaces: the *earliest* matching rule in insertion order wins, so a rule file
  // still reads top-to-bottom. What changed is the cost - a host that matches nothing
  // now costs one hash lookup per label instead of a pattern match against every rule.
  size_t best = SIZE_MAX;
  const auto consider = [&](size_t index) {
    if (index >= rules_.size()) return;  // cannot happen: RebuildIndex() mirrors rules_
    if (index >= best) return;           // an earlier rule already won
    const BlockRule& rule = rules_[index];
    if (rule.third_party_only && (!third_party || same_host)) return;
    const bool matches = rule.path_rule
                             ? (!request_path.empty() &&
                                (request_host_text + std::string(request_path))
                                        .find(rule.pattern) != std::string::npos)
                             : url::HostMatchesPattern(request_host_text, rule.pattern);
    if (matches) best = index;
  };

  // Every suffix of the request host, longest first, plus the whole host for "=exact".
  const std::string_view host_view(request_host_text);
  for (size_t start_at = 0; start_at < host_view.size();) {
    const std::string_view suffix = host_view.substr(start_at);
    const auto exact = exact_index_.find(suffix);
    if (exact != exact_index_.end()) {
      if (suffix.size() == host_view.size()) {
        for (const size_t index : exact->second) consider(index);
      }
    }
    if (const auto found = suffix_index_.find(suffix); found != suffix_index_.end()) {
      for (const size_t index : found->second) consider(index);
    }
    const size_t dot = host_view.find('.', start_at);
    if (dot == std::string_view::npos) break;
    start_at = dot + 1;
  }

  // Path rules are substring searches, so they stay a short list to walk - but only
  // when the caller passed a path at all.
  if (!request_path.empty()) {
    for (const size_t index : path_rule_indices_) consider(index);
  }
  for (const size_t index : other_rule_indices_) consider(index);

  if (best != SIZE_MAX) {
    result.decision = BlockDecision::kBlock;
    result.category = rules_[best].category;
    result.matched_rule = true;
  }
  return result;
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
}  // namespace browserx
