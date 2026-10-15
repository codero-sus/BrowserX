// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/url_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>

#include "core/format.h"

namespace browserx {
namespace core {
namespace url {
namespace {

bool IsHexDigit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool IsUnreserved(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_' || c == '.' ||
         c == '~';
}

bool IsSchemeChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '+' || c == '-' || c == '.';
}

bool LooksLikeIpv4(std::string_view host) {
  int octets = 0;
  int value = -1;
  int digits = 0;
  for (size_t i = 0; i <= host.size(); ++i) {
    const char c = i < host.size() ? host[i] : '.';
    if (c == '.') {
      if (digits == 0 || value < 0 || value > 255) return false;
      ++octets;
      value = -1;
      digits = 0;
      continue;
    }
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    if (value < 0) value = 0;
    value = value * 10 + (c - '0');
    ++digits;
    if (digits > 3) return false;
  }
  return octets == 4;
}

/// A hostname is "plausible" when it has a dot and a valid-looking TLD, or is
/// a known single-label host (localhost, intranet names with a port, ...).
bool LooksLikeHostname(std::string_view text) {
  if (text.empty()) return false;
  if (LooksLikeIpv4(text)) return true;
  if (text.find(' ') != std::string_view::npos) return false;
  if (text.find('[') == 0 && text.back() == ']') return true;  // IPv6 literal
  const size_t dot = text.rfind('.');
  if (dot == std::string_view::npos) {
    return text == "localhost" || text == "localhost.localdomain";
  }
  if (dot + 1 >= text.size()) return false;
  const std::string_view tld = text.substr(dot + 1);
  if (tld.size() < 2 || tld.size() > 24) return false;
  for (const char c : tld) {
    if (!std::isalpha(static_cast<unsigned char>(c))) return false;
  }
  for (const char c : text) {
    if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '-' || c == ':' ||
        c == '/' || c == '?') {
      continue;
    }
    return false;
  }
  return true;
}

std::string_view SubstrBetween(std::string_view url, std::string_view begin_marker,
                               std::string_view end_markers) {
  const size_t begin = url.find(begin_marker);
  if (begin == std::string_view::npos) return std::string_view();
  const size_t start = begin + begin_marker.size();
  const size_t end = url.find_first_of(end_markers, start);
  return end == std::string_view::npos ? url.substr(start) : url.substr(start, end - start);
}

}  // namespace

std::string EncodeQuery(std::string_view text, bool plus_for_space) {
  std::string out;
  out.reserve(text.size() + 8);
  for (const char raw : text) {
    const unsigned char c = static_cast<unsigned char>(raw);
    if (IsUnreserved(raw)) {
      out.push_back(raw);
    } else if (raw == ' ') {
      out.push_back(plus_for_space ? '+' : '%');
      if (!plus_for_space) out.append("20");
    } else {
      char buffer[8];
      std::snprintf(buffer, sizeof(buffer), "%%%02X", c);
      out.append(buffer);
    }
  }
  return out;
}

std::string EncodePath(std::string_view text) { return EncodeQuery(text, /*plus_for_space=*/false); }

bool HasScheme(std::string_view text) {
  const size_t colon = text.find(':');
  if (colon == std::string_view::npos || colon == 0) return false;
  for (size_t i = 0; i < colon; ++i) {
    if (!IsSchemeChar(text[i])) return false;
  }
  // Require "://" or a known schemeless-scheme like "about:" / "data:".
  const std::string scheme = fmt::ToLower(text.substr(0, colon));
  static const std::set<std::string> kKnown = {
      "http", "https", "file", "ftp", "about", "data", "chrome", "devtools",
      "view-source", "blob", "ws", "wss", "mailto", "tel", "browserx", "javascript"};
  if (text.compare(colon, 3, "://") == 0) return true;
  return kKnown.count(scheme) > 0;
}

std::string SchemeOf(std::string_view url_text) {
  const size_t colon = url_text.find(':');
  if (colon == std::string_view::npos || colon == 0) return std::string();
  for (size_t i = 0; i < colon; ++i) {
    if (!IsSchemeChar(url_text[i])) return std::string();
  }
  return fmt::ToLower(url_text.substr(0, colon));
}

std::string HostOf(std::string_view url_text) {
  const std::string scheme = SchemeOf(url_text);
  size_t start = 0;
  if (!scheme.empty()) {
    const size_t marker = url_text.find("://");
    if (marker == std::string_view::npos) return scheme == "file" ? std::string() : std::string();
    start = marker + 3;
  }
  std::string_view rest = url_text.substr(start);
  const size_t end = rest.find_first_of("/?#");
  if (end != std::string_view::npos) rest = rest.substr(0, end);
  const size_t at = rest.rfind('@');  // strip userinfo
  if (at != std::string_view::npos) rest = rest.substr(at + 1);
  std::string host(rest);
  // Strip the port (careful with IPv6 literals in brackets).
  if (!host.empty() && host.front() == '[') {
    const size_t close = host.find(']');
    if (close != std::string::npos) host = host.substr(0, close + 1);
  } else {
    const size_t colon = host.rfind(':');
    if (colon != std::string::npos) host = host.substr(0, colon);
  }
  return fmt::ToLower(host);
}

std::string RegistrableHostForDisplay(std::string_view host) {
  std::string text(host);
  if (fmt::StartsWith(text, "www.")) text = text.substr(4);
  return text;
}

int PortOf(std::string_view url_text) {
  const size_t marker = url_text.find("://");
  if (marker == std::string_view::npos) return 0;
  std::string_view rest = url_text.substr(marker + 3);
  const size_t end = rest.find_first_of("/?#");
  if (end != std::string_view::npos) rest = rest.substr(0, end);
  const size_t at = rest.rfind('@');
  if (at != std::string_view::npos) rest = rest.substr(at + 1);
  const size_t colon = rest.rfind(':');
  if (colon == std::string_view::npos) return 0;
  int64_t port = 0;
  if (!fmt::ParseInt(rest.substr(colon + 1), &port)) return 0;
  if (port <= 0 || port > 65535) return 0;
  return static_cast<int>(port);
}

std::string PathOf(std::string_view url) {
  const size_t scheme = url.find("://");
  if (scheme == std::string_view::npos) return std::string("/");
  const size_t after_host = url.find('/', scheme + 3);
  if (after_host == std::string_view::npos) return std::string("/");
  const size_t end = url.find('#', after_host);
  const std::string_view path = end == std::string_view::npos
                                    ? url.substr(after_host)
                                    : url.substr(after_host, end - after_host);
  return path.empty() ? std::string("/") : std::string(path);
}

bool IsHttp(std::string_view url_text) {
  const std::string scheme = SchemeOf(url_text);
  return scheme == "http" || scheme == "https";
}

bool IsInternal(std::string_view url_text) { return SchemeOf(url_text) == "browserx"; }

bool IsPrivileged(std::string_view url_text) {
  const std::string scheme = SchemeOf(url_text);
  return scheme == "browserx" || scheme == "chrome" || scheme == "devtools" ||
         scheme == "about" || scheme == "view-source";
}

std::string SanitizeInput(std::string_view text) {
  std::string cleaned = fmt::SanitizeText(text);
  // Remove characters that only ever appear when text is pasted from rich
  // sources (zero-width joiners, bidi overrides, BOM) - these are a known
  // phishing vector in URLs.
  static const char* kInvisibleUtf8[] = {"\xE2\x80\x8B", "\xE2\x80\x8C", "\xE2\x80\x8D",
                                         "\xE2\x80\xAE", "\xE2\x80\xAD", "\xEF\xBB\xBF"};
  for (const char* needle : kInvisibleUtf8) {
    cleaned = fmt::ReplaceAll(cleaned, needle, "");
  }
  cleaned = fmt::Trim(cleaned);
  // Collapse whitespace runs (search queries with stray newlines/tabs) and drop
  // the remaining ASCII control characters (0x00-0x1F, 0x7F): an omnibox value
  // must never contain something invisible that changes what the page sees.
  std::string out;
  bool last_space = false;
  for (const unsigned char c : cleaned) {
    if (c < 0x20 || c == 0x7F) {
      if (!last_space) out.push_back(' ');
      last_space = true;
      continue;
    }
    if (std::isspace(c) != 0) {
      if (!last_space) out.push_back(' ');
      last_space = true;
    } else {
      out.push_back(static_cast<char>(c));
      last_space = false;
    }
  }
  return fmt::Trim(out);
}

bool LooksLikeUrl(std::string_view text) {
  if (text.empty()) return false;
  if (HasScheme(text)) return true;
  if (text.find(' ') != std::string_view::npos) return false;  // queries have spaces
  if (fmt::StartsWith(text, "localhost") || fmt::StartsWith(text, "127.0.0.1") ||
      fmt::StartsWith(text, "[::1]")) {
    return true;
  }
  if (fmt::StartsWith(text, "/") || fmt::StartsWith(text, "~/") || fmt::StartsWith(text, "./")) {
    return true;  // local path
  }
  // "example.com", "sub.example.com/path?a=b", "example.com:8080"
  std::string_view host = text;
  const size_t slash = host.find_first_of("/?#");
  if (slash != std::string_view::npos) host = host.substr(0, slash);
  if (host.find('@') != std::string_view::npos) return true;  // userinfo form
  size_t colon = host.rfind(':');
  std::string_view host_only = host;
  if (colon != std::string_view::npos && host.find(']') == std::string_view::npos) {
    host_only = host.substr(0, colon);
  }
  return LooksLikeHostname(host_only);
}

std::string BuildSearchUrl(std::string_view search_template, std::string_view query) {
  const std::string encoded = EncodeQuery(query, /*plus_for_space=*/true);
  const std::string template_text(search_template);
  const size_t placeholder = template_text.find("%s");
  if (placeholder == std::string::npos) {
    const std::string separator = template_text.find('?') == std::string::npos ? "?" : "&";
    return template_text + separator + "q=" + encoded;
  }
  return template_text.substr(0, placeholder) + encoded + template_text.substr(placeholder + 2);
}

namespace {

/// Schemes the omnibox is allowed to navigate to directly. Anything else that
/// merely *looks* like a scheme (javascript:, data:, blob:, ...) is treated as
/// a search query: pasting `javascript:...` must never execute anything.
bool IsNavigableScheme(std::string_view text) {
  const std::string scheme = SchemeOf(text);
  if (scheme.empty()) return false;
  static const char* const kAllowed[] = {"http",   "https",      "browserx", "file",
                                         "ftp",    "view-source", "about",    "ws",
                                         "wss"};
  for (const char* allowed : kAllowed) {
    if (scheme == allowed) return true;
  }
  return false;
}

}  // namespace

std::string ResolveInput(std::string_view text, std::string_view search_template) {
  const std::string clean = SanitizeInput(text);
  if (clean.empty()) return std::string();
  if (IsInternal(clean)) return clean;
  if (LooksLikeUrl(clean)) {
    if (HasScheme(clean)) {
      if (!IsNavigableScheme(clean)) return BuildSearchUrl(search_template, clean);
      // view-source:browserx://... and similar pass through untouched.
      return clean;
    }
    if (fmt::StartsWith(clean, "/") || fmt::StartsWith(clean, "./") || fmt::StartsWith(clean, "~/")) {
      return "file://" + clean;
    }
    return "https://" + clean;
  }
  return BuildSearchUrl(search_template, clean);
}

std::string PrettyForDisplay(std::string_view url_text, size_t max_length) {
  std::string text(url_text);
  static const char* kPrefixes[] = {"https://", "http://"};
  for (const char* prefix : kPrefixes) {
    if (fmt::StartsWith(text, prefix)) {
      text = text.substr(std::string(prefix).size());
      break;
    }
  }
  if (fmt::StartsWith(text, "www.")) text = text.substr(4);
  // A bare root path is noise in the UI: "example.com/" -> "example.com".
  const size_t query = text.find_first_of("?#");
  const size_t slash = text.find('/');
  if (slash != std::string::npos && (query == std::string::npos || slash < query) &&
      text.size() == slash + 1) {
    text = text.substr(0, slash);
  }
  if (text.empty()) text = std::string(url_text);
  return fmt::Truncate(text, max_length);
}

namespace {

/// Default port for a scheme (0 when it has none we know of).
int DefaultPortFor(std::string_view scheme) {
  if (scheme == "https" || scheme == "wss") return 443;
  if (scheme == "http" || scheme == "ws") return 80;
  if (scheme == "ftp") return 21;
  return 0;
}

/// Explicit port, or the scheme default: `https://x:443` == `https://x`.
int EffectivePort(std::string_view url_text, std::string_view scheme) {
  const int explicit_port = PortOf(url_text);
  return explicit_port != 0 ? explicit_port : DefaultPortFor(scheme);
}

}  // namespace

bool SameOrigin(std::string_view a, std::string_view b) {
  const std::string scheme_a = SchemeOf(a);
  const std::string scheme_b = SchemeOf(b);
  if (scheme_a != scheme_b) return false;
  if (HostOf(a) != HostOf(b)) return false;
  return EffectivePort(a, scheme_a) == EffectivePort(b, scheme_b);
}

std::string StripFragment(std::string_view url_text) {
  const size_t hash = url_text.find('#');
  return std::string(hash == std::string_view::npos ? url_text : url_text.substr(0, hash));
}

const std::vector<std::string>& TrackingParamPrefixes() {
  static const std::vector<std::string> kPrefixes = {
      "utm_",    "fbclid",  "gclid",    "dclid",   "msclkid", "mc_eid",  "igshid",
      "yclid",   "twclid",  "ttclid",   "vero_",   "oly_",    "wickedid", "_hsenc",
      "_hsmi",   "mkt_tok", "ref_src",  "spm",     "scm",     "si",      "s_kwcid",
      "trk_",    "cmpid",   "campaign", "hsa_",    "pk_campaign", "piwik_", "at_medium"};
  return kPrefixes;
}

std::string StripTrackingParams(std::string_view url_text) {
  const std::string without_fragment = StripFragment(url_text);
  const size_t query = without_fragment.find('?');
  if (query == std::string::npos) return without_fragment;
  const std::string base = without_fragment.substr(0, query);
  const std::string query_text = without_fragment.substr(query + 1);
  const std::vector<std::string> pairs = fmt::Split(query_text, '&');
  std::vector<std::string> kept;
  for (const std::string& pair : pairs) {
    const size_t equals = pair.find('=');
    std::string key = equals == std::string::npos ? pair : pair.substr(0, equals);
    key = fmt::ToLower(fmt::Trim(key));
    bool tracking = false;
    for (const std::string& prefix : TrackingParamPrefixes()) {
      if (fmt::StartsWith(key, prefix)) {
        tracking = true;
        break;
      }
    }
    if (!tracking && !key.empty()) kept.push_back(pair);
  }
  if (kept.empty()) return base;
  return base + "?" + fmt::Join(kept, "&");
}

// --- origin, literals, query, domains -------------------------------------
// These exist because four different parts of the UI need the same answers
// (status bar, shield, bookmarks, permission display) and five slightly different
// answers were worse than one tested one.

std::string OriginOf(std::string_view url_text) {
  const std::string scheme = SchemeOf(url_text);
  const std::string host = HostOf(url_text);
  if (scheme.empty() || host.empty()) return std::string();
  const int port = PortOf(url_text);
  const int default_port = DefaultPortFor(scheme);
  if (port > 0 && port != default_port) return scheme + "://" + host + ":" + std::to_string(port);
  return scheme + "://" + host;
}

bool IsIpLiteralHost(std::string_view host) {
  std::string_view text = host;
  if (text.size() >= 2 && text.front() == '[' && text.back() == ']') return true;  // [::1]
  if (text.find(':') != std::string_view::npos) return true;                        // bare IPv6
  return LooksLikeIpv4(text);
}

namespace {

/// Splits a URL into the part before '?', the query, and the fragment. The fragment is
/// tracked separately because it is not a query parameter: dropping it while editing the
/// query would silently lose the reader's place in the page, and leaving it inside the
/// query would make "...#section" look like a parameter value.
void SplitUrlParts(std::string_view url, std::string* base, std::string* query,
                   std::string* fragment) {
  std::string_view rest = url;
  base->clear();
  query->clear();
  fragment->clear();
  const size_t hash = rest.find('#');
  if (hash != std::string_view::npos) {
    *fragment = std::string(rest.substr(hash));
    rest = rest.substr(0, hash);
  }
  const size_t question = rest.find('?');
  if (question == std::string_view::npos) {
    *base = std::string(rest);
    return;
  }
  *base = std::string(rest.substr(0, question));
  *query = std::string(rest.substr(question + 1));
}

/// Rebuilds the URL from the three parts, leaving a fragment attached to the path when
/// there is no query left.
std::string JoinUrlParts(const std::string& base, const std::vector<std::string>& pairs,
                         const std::string& fragment) {
  if (pairs.empty()) return base + fragment;
  return base + "?" + fmt::Join(pairs, "&") + fragment;
}

int HexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

std::string WithoutQueryParam(std::string_view url, std::string_view name) {
  const std::string lower_name = fmt::ToLower(name);
  std::string base;
  std::string query;
  std::string fragment;
  SplitUrlParts(url, &base, &query, &fragment);
  if (query.empty()) return std::string(url);

  std::vector<std::string> kept;
  for (const std::string& pair : fmt::Split(query, '&')) {
    const size_t equals = pair.find('=');
    const std::string key = fmt::ToLower(equals == std::string::npos ? pair : pair.substr(0, equals));
    if (lower_name.empty() || key == lower_name) continue;  // dropped
    kept.push_back(pair);
  }
  return JoinUrlParts(base, kept, fragment);
}

std::string QueryParam(std::string_view url, std::string_view name) {
  const std::string lower_name = fmt::ToLower(name);
  std::string base;
  std::string query;
  std::string fragment;
  SplitUrlParts(url, &base, &query, &fragment);
  for (const std::string& pair : fmt::Split(query, '&')) {
    const size_t equals = pair.find('=');
    if (equals == std::string::npos) continue;
    if (fmt::ToLower(pair.substr(0, equals)) != lower_name) continue;
    std::string value;
    for (size_t i = equals + 1; i < pair.size(); ++i) {
      if (pair[i] == '+') {
        value += ' ';
      } else if (pair[i] == '%' && i + 2 < pair.size()) {
        const int high = HexDigit(pair[i + 1]);
        const int low = HexDigit(pair[i + 2]);
        if (high < 0 || low < 0) {
          value += pair[i];
        } else {
          value += static_cast<char>(high * 16 + low);
          i += 2;
        }
      } else {
        value += pair[i];
      }
    }
    return value;
  }
  return std::string();
}

namespace {

/// Multi-label public suffixes that change what "the site" means. Not the full PSL -
/// a curated list of the ones that appear in ordinary browsing, with a documented
/// fallback to the last two labels. Being wrong about the PSL means blocking or
/// allowing the wrong site, so the list is deliberately conservative and tested.
const char* const kPublicSuffixes[] = {
    "co.uk", "org.uk", "ac.uk", "gov.uk", "me.uk", "net.uk", "sch.uk",
    "com.au", "net.au", "org.au", "edu.au", "gov.au", "id.au",
    "co.nz", "net.nz", "org.nz", "govt.nz", "ac.nz",
    "co.jp", "ne.jp", "or.jp", "go.jp", "ac.jp", "ad.jp",
    "com.br", "com.mx", "com.ar", "com.tr", "com.cn", "com.hk", "com.sg", "com.tw",
    "co.in", "net.in", "org.in", "gov.in", "ac.in", "co.za", "co.kr", "co.il",
    "com.pl", "com.ua", "com.ru", "co.id", "or.id", "my.id", "web.id",
    "github.io", "gitlab.io", "pages.dev", "workers.dev", "vercel.app", "netlify.app",
    "herokuapp.com", "onrender.com", "firebaseapp.com", "web.app", "appspot.com",
};

}  // namespace

std::string RegistrableDomain(std::string_view host) {
  const std::string lower = fmt::ToLower(fmt::Trim(host));
  if (lower.empty()) return std::string();
  // A trailing dot is legal in DNS ("example.com.") and means the same host.
  std::string text = lower;
  if (text.back() == '.') text.pop_back();
  if (IsIpLiteralHost(text)) return text;

  const std::vector<std::string> labels = fmt::Split(text, '.');
  if (labels.size() <= 2) return text;

  // Longest known suffix match wins, so "a.b.example.co.uk" -> "example.co.uk".
  size_t suffix_labels = 0;
  for (const char* suffix : kPublicSuffixes) {
    const std::vector<std::string> suffix_parts = fmt::Split(suffix, '.');
    if (suffix_parts.size() >= labels.size()) continue;
    bool matches = true;
    for (size_t i = 0; i < suffix_parts.size(); ++i) {
      if (labels[labels.size() - suffix_parts.size() + i] != suffix_parts[i]) {
        matches = false;
        break;
      }
    }
    if (matches && suffix_parts.size() > suffix_labels) suffix_labels = suffix_parts.size();
  }
  // With no multi-label suffix match the public suffix is the TLD itself, so
  // "www.example.com" is "example.com" - not "com".
  const size_t public_suffix_labels = suffix_labels > 0 ? suffix_labels : 1;
  const size_t keep = public_suffix_labels + 1;
  if (keep >= labels.size()) return text;
  std::vector<std::string> tail(labels.end() - static_cast<long>(keep), labels.end());
  return fmt::Join(tail, ".");
}

bool SameSite(std::string_view a, std::string_view b) {
  const std::string left = RegistrableDomain(HostOf(a));
  const std::string right = RegistrableDomain(HostOf(b));
  if (left.empty() || right.empty()) return false;
  return left == right;
}

bool SchemeIsSecure(std::string_view url_text) {
  const std::string scheme = SchemeOf(url_text);
  return scheme == "https" || scheme == "browserx" || scheme == "about" || scheme == "file";
}

bool IsLocalhost(std::string_view host) {
  const std::string lower = fmt::ToLower(host);
  if (lower == "localhost" || lower == "127.0.0.1" || lower == "::1" || lower == "[::1]") return true;
  return fmt::EndsWith(lower, ".localhost");
}

std::string NormalizeForCompare(std::string_view url_text) {
  std::string out;
  for (char c : url_text) out += c;
  // Fragments never change which resource is being fetched.
  const size_t hash = out.find('#');
  if (hash != std::string::npos) out.erase(hash);
  // A trailing slash after a path means the same resource as the path itself.
  const size_t question = out.find('?');
  const size_t slash_end = question == std::string::npos ? out.size() : question;
  if (slash_end > 1 && out[slash_end - 1] == '/') out.erase(slash_end - 1, 1);
  return out;
}

bool HostMatchesPattern(std::string_view host, std::string_view pattern) {
  // Allocation-free on purpose: the shield calls this once per rule for every
  // resource request, and an earlier version lowercased *both* strings into new
  // std::strings each time - 163 rules per request meant ~326 allocations on the
  // request path. Trimming and lowercasing now happen in place on string_views.
  const auto trim = [](std::string_view text) {
    size_t begin = 0;
    size_t finish = text.size();
    while (begin < finish && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
    while (finish > begin && (text[finish - 1] == ' ' || text[finish - 1] == '\t' ||
                              text[finish - 1] == '\r' || text[finish - 1] == '\n')) {
      --finish;
    }
    return text.substr(begin, finish - begin);
  };
  const auto lower_eq = [](std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
      const unsigned char ca = static_cast<unsigned char>(a[i]);
      const unsigned char cb = static_cast<unsigned char>(b[i]);
      const unsigned char la = (ca >= 'A' && ca <= 'Z') ? static_cast<unsigned char>(ca + 32) : ca;
      const unsigned char lb = (cb >= 'A' && cb <= 'Z') ? static_cast<unsigned char>(cb + 32) : cb;
      if (la != lb) return false;
    }
    return true;
  };

  host = trim(host);
  pattern = trim(pattern);
  if (host.empty() || pattern.empty()) return false;

  bool exact_only = false;
  if (pattern.front() == '=') {
    exact_only = true;
    pattern.remove_prefix(1);
  } else if (pattern.size() > 2 && pattern[0] == '*' && pattern[1] == '.') {
    pattern.remove_prefix(2);
  }
  if (pattern.empty() || pattern.size() > host.size()) return false;
  if (!lower_eq(host.substr(host.size() - pattern.size()), pattern)) return false;
  if (exact_only) return host.size() == pattern.size();
  if (host.size() == pattern.size()) return true;
  // Suffix match has to land on a label boundary: "a.b.example.com" matches
  // "example.com", but "notexample.com" does not.
  return host[host.size() - pattern.size() - 1] == '.';
}

}  // namespace url
}  // namespace core
}  // namespace browserx
