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
