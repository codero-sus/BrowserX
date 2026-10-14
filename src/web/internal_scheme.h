// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — serving browserx:// from inside the browser.
//
// The scheme itself is registered as standard/secure in CefHost's
// OnRegisterCustomSchemes(); this file supplies the *content*: a resource handler
// that renders the internal pages from live browser state, and the one privileged
// action an internal page is allowed to request (switch mode).
//
// Everything user-visible is produced by src/web/internal_pages.cc, which is
// CEF-free and unit tested. This layer only moves bytes and reads state.

#ifndef BROWSERX_WEB_INTERNAL_SCHEME_H_
#define BROWSERX_WEB_INTERNAL_SCHEME_H_

#include <string>

#include "include/cef_scheme.h"
#include "web/internal_pages.h"

namespace browserx {
namespace app {
class CefHost;
}
namespace web {

/// Collects the state the internal pages display. Safe to call on the UI thread;
/// every field degrades to an empty string when the data does not exist yet, and
/// the pages then say so instead of rendering a blank table.
PageData BuildPageData(const app::CefHost& host);

/// Installs the browserx:// handler. Call once, after CefInitialize() (the request
/// context must exist). Returns false when CEF refused the registration.
bool RegisterInternalSchemeHandler(app::CefHost* host);

/// Handles `browserx://modes?set=<mode>`: switches the live mode and returns a
/// human-readable summary, or false when the request is not a valid switch. Kept
/// free of CEF types so the URL contract stays unit testable (see
/// ParseModeSwitchRequest) and so the mobile edition can reuse the same rules.
bool ApplyModeSwitchRequest(app::CefHost* host, const std::string& url, std::string* summary);

}  // namespace web
}  // namespace browserx

#endif  // BROWSERX_WEB_INTERNAL_SCHEME_H_
