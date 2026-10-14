// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "browser/request_guard.h"

#include <cstdint>
#include <string>

#include "browser/browser_shell.h"
#include "core/url_utils.h"
#include "include/cef_browser.h"
#include "include/cef_frame.h"

namespace browserx {
namespace browser {
namespace {

/// Content-Length when the request declares one (a "bytes saved" figure that is at
/// least the server's own number), otherwise the conservative per-type estimate.
uint64_t DeclaredBytes(CefRefPtr<CefRequest> request) {
  const std::string header = request->GetHeaderByName("Content-Length").ToString();
  if (header.empty()) return 0;
  uint64_t value = 0;
  for (const char c : header) {
    if (c < '0' || c > '9') return 0;  // not a plain number: do not guess
    value = value * 10 + static_cast<uint64_t>(c - '0');
    if (value > 1024ull * 1024 * 1024) return 0;  // absurd: fall back to the estimate
  }
  return value;
}

/// The document (first-party) host for this request. The frame's own URL is the
/// best source; the cookie first-party URL is the fallback for odd frames.
std::string DocumentHost(CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request) {
  if (frame.get() != nullptr) {
    const std::string host = core::url::HostOf(frame->GetURL().ToString());
    if (!host.empty()) return host;
  }
  return core::url::HostOf(request->GetFirstPartyForCookies().ToString());
}

}  // namespace

core::ResourceType MapCefResourceType(cef_resource_type_t type) {
  switch (type) {
    case RT_MAIN_FRAME: return core::ResourceType::kDocument;
    case RT_SUB_FRAME: return core::ResourceType::kSubdocument;
    case RT_STYLESHEET: return core::ResourceType::kStylesheet;
    case RT_SCRIPT: return core::ResourceType::kScript;
    case RT_IMAGE: return core::ResourceType::kImage;
    case RT_FAVICON: return core::ResourceType::kImage;
    case RT_FONT_RESOURCE: return core::ResourceType::kFont;
    case RT_MEDIA: return core::ResourceType::kMedia;
    case RT_XHR: return core::ResourceType::kXhr;
    case RT_PING: return core::ResourceType::kXhr;
    case RT_SUB_RESOURCE:
    case RT_OBJECT:
    default:
      return core::ResourceType::kOther;
  }
}

CefResourceRequestHandler::ReturnValue RequestGuard::OnBeforeResourceLoad(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request,
    CefRefPtr<CefCallback> callback) {
  if (shell_ == nullptr || request.get() == nullptr) return RV_CONTINUE;

  const std::string url = request->GetURL().ToString();
  // Only the web goes through the guard: browserx:// is ours, and a data: or file:
  // request has no host to judge.
  if (!core::url::IsHttp(url)) return RV_CONTINUE;

  core::RequestFacts facts;
  facts.document_host = DocumentHost(frame, request);
  facts.request_host = core::url::HostOf(url);
  facts.request_path = core::url::PathOf(url);
  facts.type = MapCefResourceType(request->GetResourceType());
  facts.declared_bytes = DeclaredBytes(request);
  if (facts.request_host.empty()) return RV_CONTINUE;

  const core::RuntimePolicy& policy = shell_->runtime_policy();
  const core::RequestOutcome outcome = core::DecideRequest(shell_->shield(), policy, facts);
  if (!outcome.block) return RV_CONTINUE;

  // Record it where the status bar and the shield panel read their numbers from,
  // so a blocked request is visible to the user rather than silent.
  const uint64_t bytes = facts.declared_bytes > 0 ? facts.declared_bytes
                                                  : core::EstimatedBytesForType(facts.type);
  shell_->NoteBlockedRequest(outcome, facts.request_host, bytes);
  return RV_CANCEL;
}

}  // namespace browser
}  // namespace browserx
