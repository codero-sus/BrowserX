// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/security_state.h"

#include <cctype>
#include <string>

namespace browserx {
namespace core {
namespace {

/// Lower-cases the part before the first ':'. Returns the empty string when there is no
/// scheme at all - which is a real case (a relative URL inside a page) and must not be
/// mistaken for any of the known ones.
std::string SchemeOf(const std::string& url) {
  const size_t colon = url.find(':');
  if (colon == std::string::npos || colon == 0) return std::string();
  // A URL whose "scheme" contains a slash before the colon is not a URL with a scheme:
  // "example.com/a:b" is a relative path, not a protocol named "example.com/a".
  if (url.find('/') != std::string::npos && url.find('/') < colon) return std::string();
  std::string scheme = url.substr(0, colon);
  for (char& c : scheme) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return scheme;
}

}  // namespace

Transport ClassifyTransport(const std::string& url) {
  const std::string scheme = SchemeOf(url);
  if (scheme == "https") return Transport::kSecure;
  if (scheme == "http") return Transport::kInsecure;
  if (scheme == "browserx") return Transport::kInternal;
  if (scheme == "file") return Transport::kLocal;
  return Transport::kUnknown;
}

const char* TransportGlyph(Transport transport) {
  switch (transport) {
    // A padlock for the encrypted case and a plain warning sign otherwise: the two must be
    // distinguishable at a glance and without reading, because that is how people use it.
    case Transport::kSecure: return "\xF0\x9F\x94\x92";      // 🔒
    case Transport::kInsecure: return "\xE2\x9A\xA0";        // ⚠
    case Transport::kInternal: return "\xE2\x97\x8F";        // ●
    case Transport::kLocal: return "\xF0\x9F\x93\x81";       // 📁
    case Transport::kUnknown:
    default: return "\xE2\x80\xA6";                          // …
  }
}

const char* TransportLabel(Transport transport) {
  switch (transport) {
    case Transport::kSecure: return "Secure";
    case Transport::kInsecure: return "Not secure";
    case Transport::kInternal: return "BrowserX page";
    case Transport::kLocal: return "Local file";
    case Transport::kUnknown:
    default: return "Unknown";
  }
}

std::string TransportDetail(Transport transport) {
  switch (transport) {
    case Transport::kSecure:
      return "This page was served over HTTPS. What you send and receive is encrypted, and "
             "the certificate was accepted for this site. It says nothing about whether the "
             "site is honest - only that nobody on the path can read or change it.";
    case Transport::kInsecure:
      return "This page was served over plain HTTP. Anyone on the network path - the "
             "cafe's Wi-Fi, your provider, a proxy - can read it and can change it in "
             "transit. That is not the same as the site being malicious, but it does mean "
             "nothing you see here is guaranteed to be what the site sent.";
    case Transport::kInternal:
      return "This is one of BrowserX's own pages. It comes from inside the browser, so "
             "there is no network connection to secure and nothing outside can see it.";
    case Transport::kLocal:
      return "This is a file on this machine. It did not come over the network, and it is "
             "only as trustworthy as where you got it.";
    case Transport::kUnknown:
    default:
      return "BrowserX does not classify this kind of address yet, so it cannot tell you "
             "whether the connection is encrypted. It will not claim either way.";
  }
}

bool NeedsWarning(Transport transport) { return transport == Transport::kInsecure; }

}  // namespace core
}  // namespace browserx
