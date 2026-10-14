// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/request_guard.h"

#include <algorithm>

#include "core/format.h"
#include "core/url_utils.h"

namespace browserx {
namespace core {
namespace {

std::string Lower(std::string_view text) { return fmt::ToLower(std::string(text)); }

const char* VerdictReason(RequestVerdict verdict) {
  switch (verdict) {
    case RequestVerdict::kAllow: return "allowed";
    case RequestVerdict::kTracker: return "shield";
    case RequestVerdict::kPolicyImage: return "mode: images off";
    case RequestVerdict::kPolicyMedia: return "mode: media off";
    case RequestVerdict::kPolicyFont: return "mode: web fonts off";
    case RequestVerdict::kPolicyThirdParty: return "mode: third-party heavy resources off";
  }
  return "blocked";
}

RequestOutcome Make(RequestVerdict verdict, BlockCategory category = BlockCategory::kAds) {
  RequestOutcome outcome;
  outcome.block = verdict != RequestVerdict::kAllow;
  outcome.verdict = verdict;
  outcome.category = category;
  if (outcome.block) {
    outcome.reason = VerdictReason(verdict);
    if (verdict == RequestVerdict::kTracker) {
      outcome.reason += std::string(": ") + BlockCategoryLabel(category);
    }
  }
  return outcome;
}

/// The mode policy, without the shield. `third_party` is passed in because the
/// caller already had to compute it for the shield.
RequestVerdict PolicyVerdict(const RuntimePolicy& policy, ResourceType type, bool third_party,
                            bool is_top_level) {
  if (is_top_level) return RequestVerdict::kAllow;  // never break a navigation

  // Free of charge to the site: dropping these cannot break layout logic.
  if (policy.block_images && type == ResourceType::kImage) return RequestVerdict::kPolicyImage;
  if (policy.block_media && type == ResourceType::kMedia) return RequestVerdict::kPolicyMedia;
  if (policy.block_fonts && type == ResourceType::kFont) return RequestVerdict::kPolicyFont;

  if (policy.block_third_party && third_party) {
    switch (type) {
      case ResourceType::kImage: return RequestVerdict::kPolicyThirdParty;
      case ResourceType::kMedia: return RequestVerdict::kPolicyThirdParty;
      case ResourceType::kFont: return RequestVerdict::kPolicyThirdParty;
      // Scripts, stylesheets, XHR and sub-documents are deliberately not here.
      default: break;
    }
  }
  return RequestVerdict::kAllow;
}

}  // namespace

bool SameSite(std::string_view a, std::string_view b) {
  if (a.empty() || b.empty()) return false;
  const std::string lhs = Lower(a);
  const std::string rhs = Lower(b);
  if (lhs == rhs) return true;
  // "www.example.com" and "example.com" are the same site for this purpose.
  const std::string lhs_bare = url::RegistrableHostForDisplay(lhs);
  const std::string rhs_bare = url::RegistrableHostForDisplay(rhs);
  if (lhs_bare == rhs_bare) return true;
  return url::HostMatchesPattern(lhs, rhs) || url::HostMatchesPattern(rhs, lhs);
}

uint64_t EstimatedBytesForType(ResourceType type) {
  switch (type) {
    case ResourceType::kImage: return 120 * 1024;         // a typical hero/list image
    case ResourceType::kMedia: return 2 * 1024 * 1024;     // a few seconds of video
    case ResourceType::kFont: return 40 * 1024;
    case ResourceType::kSubdocument: return 30 * 1024;
    case ResourceType::kScript: return 60 * 1024;
    case ResourceType::kStylesheet: return 30 * 1024;
    case ResourceType::kXhr: return 8 * 1024;
    default: return 4 * 1024;
  }
}

RequestOutcome DecideRequest(const Blocklist& shield, const RuntimePolicy& policy,
                             const RequestFacts& facts) {
  const std::string request_host = Lower(facts.request_host);
  const std::string document_host = Lower(facts.document_host);
  if (request_host.empty()) return Make(RequestVerdict::kAllow);

  const bool is_top_level = facts.type == ResourceType::kDocument;
  const bool third_party = !SameSite(document_host, request_host);

  // 1) The shield. It is the user's own rule set, so it is applied first and its
  //    verdict is final - a policy can never re-enable a blocked tracker.
  const BlockMatch match = shield.DecideDetailed(document_host, request_host, facts.request_path,
                                                 facts.type);
  if (match.decision == BlockDecision::kBlock) return Make(RequestVerdict::kTracker, match.category);

  // 2) The mode's resource policy.
  return Make(PolicyVerdict(policy, facts.type, third_party, is_top_level));
}

}  // namespace core
}  // namespace browserx
