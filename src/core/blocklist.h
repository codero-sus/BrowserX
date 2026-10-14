// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — the Shield: a small, curated, honest content blocker.
//
// Design notes (important for honesty in the UI):
//   * This is NOT uBlock Origin. It is a curated list of well-known ad,
//     analytics, social-tracking and crypto-mining endpoints, matched by host
//     suffix or path pattern. It blocks a large share of common tracking with
//     near-zero CPU cost, which matters for the LOW RAM/ECO modes.
//   * Every rule that could break a site is third-party-only by default, and
//     the user can allowlist a host from the toolbar in one click.
//   * Matching is pure string logic => fully unit tested.

#ifndef BROWSERX_CORE_BLOCKLIST_H_
#define BROWSERX_CORE_BLOCKLIST_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace browserx {
namespace core {

enum class BlockCategory { kAds, kAnalytics, kSocial, kCrypto, kFingerprint };

const char* BlockCategoryLabel(BlockCategory category);

enum class ResourceType {
  kDocument,
  kSubdocument,
  kStylesheet,
  kScript,
  kImage,
  kFont,
  kMedia,
  kXhr,
  kOther,
};

enum class BlockDecision {
  kAllow,
  kBlock,
  kAllowlisted,  // would have been blocked, but the user allowed this site
  kDisabled,     // shield off for this profile
};

/// A shield decision together with the rule that made it, so callers can report
/// *which* category saved the bytes instead of just "something was blocked".
struct BlockMatch {
  BlockDecision decision = BlockDecision::kAllow;
  BlockCategory category = BlockCategory::kAds;
  bool matched_rule = false;
};

struct BlockRule {
  std::string pattern;        // host suffix, "*.host", "=exact.host" or "/path/fragment"
  BlockCategory category;
  bool third_party_only;      // only block when the request is cross-site
  bool path_rule;             // pattern is matched against host+path
};

class Blocklist {
 public:
  /// Adds the curated default rule set (see blocklist.cc for the list).
  void AddDefaults();

  void AddRule(std::string pattern, BlockCategory category, bool third_party_only = true,
               bool path_rule = false);

  void SetEnabled(bool enabled) { enabled_ = enabled; }
  bool enabled() const { return enabled_; }

  /// Sites where the shield must not act (user choice, one click in the UI).
  void AllowlistHost(std::string host);
  void RemoveAllowlistHost(std::string host);
  void ClearAllowlist();
  const std::vector<std::string>& allowlist() const { return allowlist_; }

  /// The core decision. `document_host` may be empty (top-level navigation).
  BlockDecision Decide(std::string_view document_host, std::string_view request_host,
                       std::string_view request_path, ResourceType type) const;

  /// Same rule set, but the matched rule's category comes back with the verdict.
  BlockMatch DecideDetailed(std::string_view document_host, std::string_view request_host,
                            std::string_view request_path, ResourceType type) const;

  const std::vector<BlockRule>& rules() const { return rules_; }
  size_t rule_count() const { return rules_.size(); }

  // --- statistics for the status bar / Shield panel ------------------------
  void NoteBlocked(BlockCategory category, uint64_t bytes_estimate);
  uint64_t blocked_total() const { return blocked_total_; }
  uint64_t bytes_saved_estimate() const { return bytes_saved_; }
  uint64_t blocked_in_category(BlockCategory category) const;
  void ResetStatistics();

  /// True when this resource type costs a lot of memory/CPU and the mode may
  /// want to block it (used by block_images/block_media policy flags).
  static bool IsHeavyResource(ResourceType type);

 private:
  std::vector<BlockRule> rules_;
  std::vector<std::string> allowlist_;
  bool enabled_ = true;
  uint64_t blocked_total_ = 0;
  uint64_t bytes_saved_ = 0;
  uint64_t category_counts_[5] = {0, 0, 0, 0, 0};
};

}  // namespace core
}  // namespace browserx

#endif  // BROWSERX_CORE_BLOCKLIST_H_
