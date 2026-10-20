// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — what the omnibox indicator is allowed to say about a page.
//
// Modern browsers put a small mark at the left of the address bar, and it is worth copying
// only because it is *honest*: a padlock means the transport is authenticated and encrypted,
// and its absence means it is not. The interesting part is everything in between — a page
// that is `http://` is not "unsafe" in the sense of malware, it is simply readable by
// anyone on the path, and saying so precisely is the whole feature.
//
// This is a pure function of the URL so the toolbar, the site panel and the tests cannot
// disagree about what a given page is.

#ifndef BROWSERX_CORE_SECURITY_STATE_H_
#define BROWSERX_CORE_SECURITY_STATE_H_

#include <string>

namespace browserx {
namespace core {

enum class Transport {
  kSecure,    // https:// - encrypted, and the certificate was accepted
  kInsecure,  // http:// - plain text on the wire
  kInternal,  // browserx:// - our own pages, no network involved
  kLocal,     // file:// - this machine's disk
  kUnknown,   // about:blank, devtools://, a scheme we do not classify
};

/// Classifies a URL. Case-insensitive on the scheme, as URLs are.
Transport ClassifyTransport(const std::string& url);

/// A short glyph for the toolbar. Kept in core so a test can assert that the insecure case
/// is visibly different from the secure one - an indicator that looks the same either way
/// is worse than none.
const char* TransportGlyph(Transport transport);

/// The one-word state ("Secure", "Not secure", "BrowserX page", "Local file", "Unknown").
const char* TransportLabel(Transport transport);

/// The full sentence for the site panel: what this means, not just what it is called.
std::string TransportDetail(Transport transport);

/// Does this transport deserve the warning colour? Used by the toolbar and the tests.
bool NeedsWarning(Transport transport);

}  // namespace core
}  // namespace browserx

#endif  // BROWSERX_CORE_SECURITY_STATE_H_
