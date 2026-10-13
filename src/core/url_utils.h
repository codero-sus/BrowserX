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

/// True when the host matches `pattern`, where pattern may be
/// "example.com" (suffix match, including subdomains), "*.example.com" or
/// "exact.host" (leading '=' forces an exact match).
bool HostMatchesPattern(std::string_view host, std::string_view pattern);

}  // namespace url
}  // namespace core
}  // namespace browserx

#endif  // BROWSERX_CORE_URL_UTILS_H_
