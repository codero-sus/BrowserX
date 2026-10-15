// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — omnibox input parsing and URL helpers.
//
// Pure string logic, so every rule ("is this a URL or a search?") is unit
// tested. Anything that touches the network or the UI lives elsewhere.

#ifndef BROWSERX_CORE_URL_UTILS_H_
#define BROWSERX_CORE_URL_UTILS_H_

#include <string>
#include <string_view>
#include <vector>

namespace browserx {
namespace core {
namespace url {

/// Percent-encodes for a query string ("+ " for spaces when plus_for_space).
std::string EncodeQuery(std::string_view text, bool plus_for_space = true);
/// Percent-encodes a path segment (spaces become %20).
std::string EncodePath(std::string_view text);

/// True when the text already carries a scheme we understand
/// (http, https, browserx, file, ftp, about, data, view-source, ...).
bool HasScheme(std::string_view text);
/// Extracts the scheme (lower-case, without "://"); empty when there is none.
std::string SchemeOf(std::string_view url);
/// Extracts the host (lower-case, no port, no userinfo); empty for non-network URLs.
std::string HostOf(std::string_view url);
/// Host without a leading "www." (used for display and shield decisions).
std::string RegistrableHostForDisplay(std::string_view host);
/// Port (0 when absent/unparsable).
int PortOf(std::string_view url);
/// Path (and query) of a URL, without scheme/host/port; "/" when there is none.
/// The shield matches path rules against this, so it never sees a host twice.
std::string PathOf(std::string_view url);

/// True for http/https (the only schemes the shield and modes affect).
bool IsHttp(std::string_view url);
/// True for BrowserX's own pages.
bool IsInternal(std::string_view url);
/// True for about:, chrome:, devtools: pages that must not be blocked/restyled.
bool IsPrivileged(std::string_view url);

/// Sanitises raw user input: strips control characters, trims, collapses runs
/// of whitespace, removes leading/trailing junk that breaks navigation.
std::string SanitizeInput(std::string_view text);

/// Heuristic: does this look like something we should navigate to directly?
bool LooksLikeUrl(std::string_view text);

/// Resolves omnibox input to a URL, using `search_template` (must contain "%s")
/// when the input is a search query.
std::string ResolveInput(std::string_view text, std::string_view search_template);

/// Builds a search URL from a template and query (template without "%s" gets
/// the query appended as "?q=").
std::string BuildSearchUrl(std::string_view search_template, std::string_view query);

/// Short, human-readable form for the status bar / history ("example.com/x").
std::string PrettyForDisplay(std::string_view url, size_t max_length = 96);

/// True when two URLs point at the same origin (scheme+host+port).
bool SameOrigin(std::string_view a, std::string_view b);

/// Removes the fragment and (optionally) tracking parameters that are never
/// needed for navigation. Used by "copy clean link".
std::string StripFragment(std::string_view url);
std::string StripTrackingParams(std::string_view url);
/// List of query parameter prefixes considered tracking noise.
const std::vector<std::string>& TrackingParamPrefixes();

/// "scheme://host:port" — the origin, for the status bar and for SameOrigin.
/// browserx://modes has one too; only URLs with no host at all (about:, data:) return
/// an empty string, and the caller then shows nothing rather than inventing one.
std::string OriginOf(std::string_view url);
/// True when the host is a literal address (IPv4 or a bracketed IPv6), which needs
/// no DNS and is worth showing differently: "you are going to a number, not a name".
bool IsIpLiteralHost(std::string_view host);
/// The address minus any query parameter whose name appears in `name` (all of them
/// when the name is empty). Used by "copy clean link" and by the download page.
std::string WithoutQueryParam(std::string_view url, std::string_view name);
/// Value of a query parameter, or empty. Percent-decodes and turns '+' into a space
/// because that is what a query string means.
std::string QueryParam(std::string_view url, std::string_view name);
/// The registrable domain — the "example.com" part of "a.b.example.co.uk" — using a
/// built-in list of the public suffixes that matter for display and for the shield.
/// Falls back to the last two labels when the suffix is not known.
std::string RegistrableDomain(std::string_view host);
/// True when the two URLs would be "the same site" for a privacy decision:
/// same registrable domain, ignoring scheme and port.
bool SameSite(std::string_view a, std::string_view b);
/// True when the URL's scheme is one whose content the browser treats as secure
/// (https, browserx, about, file). Used by the omnibox indicator and by tests.
bool SchemeIsSecure(std::string_view url);
/// True for the local-development hosts (localhost, 127.0.0.1, ::1, *.localhost).
bool IsLocalhost(std::string_view host);
/// A stable form for de-duplicating history and comparing bookmarks: the fragment is
/// dropped (it never changes which resource is fetched) and so is a trailing slash -
/// including the root one, so "https://example.com" and "https://example.com/" are one
/// address instead of two entries in the list.
std::string NormalizeForCompare(std::string_view url);

/// True when the host matches `pattern`, where pattern may be
/// "example.com" (suffix match, including subdomains), "*.example.com" or
/// "exact.host" (leading '=' forces an exact match).
bool HostMatchesPattern(std::string_view host, std::string_view pattern);

}  // namespace url
}  // namespace core
}  // namespace browserx

#endif  // BROWSERX_CORE_URL_UTILS_H_
