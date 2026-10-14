// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#ifndef BROWSERX_CORE_REQUEST_GUARD_H_
#define BROWSERX_CORE_REQUEST_GUARD_H_

#include <cstdint>
#include <string>

#include "core/blocklist.h"
#include "core/mode_engine.h"

namespace browserx {
namespace core {

/// One outgoing resource request, as the browser layer sees it.
///
/// The guard is a pure function of (shield, mode policy, request facts) so the
/// decision to block a request can be unit-tested without CEF, a renderer or a
/// network - the same rule the product applies is the rule the tests exercise.
struct RequestFacts {
  /// Host of the page that asked for this resource. Empty for a top-level load.
  std::string document_host;
  std::string request_host;
  std::string request_path;
  ResourceType type = ResourceType::kOther;
  /// Declared Content-Length, when the request carries one; 0 = unknown. Used
  /// only for the "bytes saved" statistic, never for the decision.
  uint64_t declared_bytes = 0;
};

/// Why a request was stopped. The shield and the mode policy are reported
/// separately because the user cares which one is talking.
enum class RequestVerdict {
  kAllow,             // goes to the network
  kTracker,           // a shield rule matched (ads/analytics/social/crypto/fingerprint)
  kPolicyImage,       // the mode blocks images
  kPolicyMedia,       // the mode blocks media
  kPolicyFont,        // the mode blocks fonts
  kPolicyThirdParty,  // the mode blocks third-party heavy resources
};

struct RequestOutcome {
  bool block = false;
  RequestVerdict verdict = RequestVerdict::kAllow;
  /// Category recorded in the shield statistics when `verdict == kTracker`.
  BlockCategory category = BlockCategory::kAds;
  /// Short line for the log/telemetry: "shield: analytics" or "mode: images".
  std::string reason;

  /// True when this request counts towards the shield's saved-bytes counter.
  bool CountsAsBlocked() const { return block; }
};

/// True when two hosts are the same site for the purposes of this guard: exact
/// match, or one is a subdomain of the other.
bool SameSite(std::string_view a, std::string_view b);

/// Applies, in order:
///   1. the tracker shield (a matched rule always wins, before any policy),
///   2. the mode's resource policy (`block_images`, `block_media`,
///      `block_fonts`, `block_third_party`).
///
/// Two rules are deliberately absolute, because getting them wrong breaks the
/// web rather than saving a kilobyte:
///   * a top-level document load is never blocked by the policy (the user asked
///     for that page; the shield may still stop known tracker *subresources*),
///   * `block_third_party` only covers images, media and fonts. Third-party
///     scripts and stylesheets are never blocked by a mode, because most sites
///     load both from a CDN and blocking them renders the page broken. The
///     shield's own rules are the place for "this third party is a tracker".
///
/// Blocked resources are better *not requested* than requested and dropped
/// (that is what saves data), so the browser layer cancels before the network.
RequestOutcome DecideRequest(const Blocklist& shield, const RuntimePolicy& policy,
                             const RequestFacts& facts);

/// Estimated size of a blocked resource when the request declares no length.
/// Kept deliberately conservative; the UI labels the total as an estimate.
uint64_t EstimatedBytesForType(ResourceType type);

}  // namespace core
}  // namespace browserx

#endif  // BROWSERX_CORE_REQUEST_GUARD_H_
