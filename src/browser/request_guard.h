// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#ifndef BROWSERX_BROWSER_REQUEST_GUARD_H_
#define BROWSERX_BROWSER_REQUEST_GUARD_H_

#include <string>

#include "core/request_guard.h"
#include "include/cef_resource_request_handler.h"
#include "include/cef_request.h"

namespace browserx {
namespace browser {

class BrowserShellImpl;

/// Translates CEF's resource types into the core's, so the policy in
/// `core::DecideRequest` stays free of CEF and testable without it.
core::ResourceType MapCefResourceType(cef_resource_type_t type);

/// The one place a BrowserX browser decides whether an outgoing request happens.
///
/// Without this the shield would be a switch that changes an icon: the rule set in
/// `core::Blocklist` is only useful if something consults it *before* the request
/// goes to the network, and the same is true of the resource policy that DATA SAVER
/// and WEAK NETWORK rely on.
///
/// Cancelling is deliberate and is what saves the data: a request that is never sent
/// costs nothing, while a response that is downloaded and then dropped in the
/// renderer has already been paid for.
class RequestGuard : public CefResourceRequestHandler {
 public:
  explicit RequestGuard(BrowserShellImpl* shell) : shell_(shell) {}

  ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefRequest> request,
                                   CefRefPtr<CefCallback> callback) override;

 private:
  /// Raw pointer is safe: the window owns the browser and outlives its handlers.
  BrowserShellImpl* shell_ = nullptr;

  IMPLEMENT_REFCOUNTING(RequestGuard);
  DISALLOW_COPY_AND_ASSIGN(RequestGuard);
};

}  // namespace browser
}  // namespace browserx

#endif  // BROWSERX_BROWSER_REQUEST_GUARD_H_
