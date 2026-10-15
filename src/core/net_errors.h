// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — what a network error actually means, in plain language.
//
// A browser that answers a failure with "ERR_CONNECTION_REFUSED" has told the user
// nothing they can act on. This module turns the codes Chromium reports into a short
// description, what it usually means, and two or three things worth trying - and it
// refuses to invent anything: an unknown code says it is unknown and prints the number,
// because a confident-sounding wrong explanation is worse than none.
//
// The codes are from upstream, not from memory: `net/base/net_error_list.h` in
// chromium/chromium was read while writing this table, and tests/unit/net_errors_test.cc
// pins each number/name pair. Three of them were wrong when this file was first drafted
// from recollection (the certificate group is COMMON_NAME_INVALID -200, DATE_INVALID -201,
// AUTHORITY_INVALID -202, not the order you would guess), which is the whole argument for
// including the numbers in a test.

#ifndef BROWSERX_CORE_NET_ERRORS_H_
#define BROWSERX_CORE_NET_ERRORS_H_

#include <string>
#include <string_view>
#include <vector>

namespace browserx {
namespace core {

/// Which family a failure belongs to. Used for the colour and the advice on the error
/// page, and for tests that assert the families do not overlap.
enum class NetErrorFamily {
  kUnknown,
  kLocalInput,   // the browser was asked for something it cannot open
  kBlocked,      // BrowserX's own shield or a policy, not the network
  kDns,          // the name could not be turned into an address
  kConnection,   // the address was known, the connection was not made
  kTimeout,      // nothing answered in time
  kProtocol,     // an answer arrived but was not usable
  kTls,          // a certificate or handshake problem: do not push past this
};

/// Everything the UI needs to explain one failure. `code` is Chromium's value, which is
/// negative; `name` is the upstream name without the ERR_ prefix.
struct NetErrorHelp {
  int code = 0;
  std::string name;                  // "CONNECTION_REFUSED"
  NetErrorFamily family = NetErrorFamily::kUnknown;
  std::string headline;              // "The site refused the connection"
  std::string explanation;           // two sentences, no jargon
  std::vector<std::string> try_first;  // concrete steps, in the order worth trying
  /// True when the failure is a security problem where continuing is dangerous. The page
  /// uses this to say so instead of offering a "continue anyway" that must not exist.
  bool security_relevant = false;
  /// True when this browser is the reason, not the network or the site.
  bool caused_by_browser = false;
};

/// The explanation for a Chromium error code, or a table-independent answer that says the
/// code is not known here. Never returns an empty explanation.
NetErrorHelp ExplainNetError(int code);

/// The same, by upstream name. Accepts the name with or without the "ERR_" prefix and in
/// any case ("err_name_not_resolved", "NAME_NOT_RESOLVED"), because both spellings reach
/// this function in practice: CEF's enum names and the text Chromium logs.
NetErrorHelp ExplainNetErrorName(std::string_view name);

/// The code for an upstream name, or 0 when this table does not know it.
int NetErrorCodeForName(std::string_view name);

/// Every code this table explains, ascending by name - the list the error page shows when
/// someone wants to see what the browser can and cannot explain.
const std::vector<int>& KnownNetErrorCodes();

/// "ERR_NAME_NOT_RESOLVED (-105)" - the form to show when there is no explanation.
std::string FormatNetErrorCode(int code);

/// Name of the family, for class names and tests ("tls", "dns", …).
const char* NetErrorFamilyName(NetErrorFamily family);

/// True when the failure is worth a full error page rather than a status-bar flash.
/// ERR_ABORTED is a user who stopped the load; ERR_CACHE_MISS is routine; those are not
/// failures to interrupt anyone with, and treating them as such is how "browsers" end up
/// showing an error page every time someone presses Stop.
bool NetErrorWarrantsPage(int code);

}  // namespace core
}  // namespace browserx

#endif  // BROWSERX_CORE_NET_ERRORS_H_
