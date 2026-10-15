// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX internal pages — browserx://settings, telemetry, modes, history, …
//
// This file is deliberately CEF-free: it turns *data* (the mode plan, the session,
// the machine profile, the catalogue) into HTML strings. That keeps the pages unit
// testable, and it means the same renderer can be reused by the mobile edition's
// panels and by the preview server.
//
// The CEF side (src/web/internal_scheme.{h,cc}) only has to answer "which page,
// with which data" — every string the browser shows to the user is produced here.

#ifndef BROWSERX_WEB_INTERNAL_PAGES_H_
#define BROWSERX_WEB_INTERNAL_PAGES_H_

#include <string>
#include <string_view>
#include <vector>

namespace browserx {
namespace web {

/// Every browserx:// page the browser can serve.
enum class PageKind {
  kNewTab,
  kModes,
  kTelemetry,
  kSettings,
  kShortcuts,
  kCatalog,
  kSession,
  kAbout,
  kHistory,
  kBookmarks,
  kDownloads,
  kFind,
  kBench,    // rendering/script benchmark (the measuring stick for speed claims)
  kUnknown,  // anything else -> a helpful "no such page" listing
};

/// Project identity, in one place so the UI, the CLI and the packaged build can
/// never disagree about who made this or where it lives.
constexpr const char* kProjectAuthor = "codero-sus";
constexpr const char* kProjectAuthorUrl = "https://github.com/codero-sus";
constexpr const char* kProjectSite = "the-codero.web.app";
constexpr const char* kProjectSiteUrl = "https://the-codero.web.app";
constexpr const char* kProjectRepository = "github.com/codero-sus/BrowserX";
constexpr const char* kProjectRepositoryUrl = "https://github.com/codero-sus/BrowserX";
constexpr const char* kProjectLicence = "BrowserX Personal-Use License";
constexpr const char* kProjectLicenceUrl =
    "https://github.com/codero-sus/BrowserX/blob/main/LICENSE";
/// The four restrictions, in one string so the CLI, the About page, the packaged
/// build and the mobile app can never describe the licence differently.
constexpr const char* kProjectLicenceTerms =
    "personal, non-commercial use only; no modification; no redistribution or resale";

/// Parsed form of a browserx:// URL.
struct InternalUrl {
  std::string scheme;   // always "browserx"
  std::string host;     // "modes", "telemetry", …
  std::string path;     // "/" or a deeper path
  std::string query;    // raw query string without '?'
};

/// Data every page can draw on. The browser fills what it has; a page only shows
/// the sections it needs, and says so when the data is missing instead of lying.
struct PageData {
  std::string version = "dev";
  std::string active_mode_id = "ultra";
  std::string active_mode_label = "ULTRA";
  /// core::report::RenderPlansJson() — every mode's switches, policy and rationale.
  std::string plans_json;
  /// core::report::RenderPlanText() for the active mode (human-readable).
  std::string plan_text;
  /// core::DescribeProfile() — the machine BrowserX detected.
  std::string profile_report;
  /// core::report::RenderRuntimePolicySummary() for the active mode.
  std::string runtime_summary;
  /// core::RenderCatalogReport() — verified switches with provenance.
  std::string catalog_report;
  /// browser::Session::ToJson() — the live tab/session state.
  std::string session_json;
  /// Escape hatch for the app: extra HTML appended to the About page.
  std::string extra_about_html;
  /// The user's search template (core::Settings::search_template). The start page
  /// searches where the omnibox searches; the default is only a fallback for callers
  /// that render pages without settings (tests, the preview).
  std::string search_template = "https://duckduckgo.com/?q=%s";
  /// The host that was actually requested. Only used by the "no such page" view,
  /// so the user sees the address they typed instead of a generic message.
  std::string requested_host;

  /// Credits shown in the footer and on the About page.
  std::string author = kProjectAuthor;
  std::string author_url = kProjectAuthorUrl;
  std::string website = kProjectSite;
  std::string website_url = kProjectSiteUrl;
  std::string repository = kProjectRepository;
  std::string repository_url = kProjectRepositoryUrl;
};

/// Splits a browserx:// URL. Returns false when the scheme is not "browserx".
bool ParseInternalUrl(std::string_view url, InternalUrl* out);

/// Maps a host to a page kind; unknown hosts become kUnknown (never a crash).
PageKind KindForHost(std::string_view host);
const char* PageName(PageKind kind);
const char* PageTitle(PageKind kind);

/// Every host that serves a real page (used by the omnibox, the new-tab page and
/// the tests that assert none of them 404).
const std::vector<std::string>& AllInternalHosts();

/// Full HTML document for a page. Never returns an empty string: an unknown host
/// gets the "no such page" page, and a page whose data is missing says exactly
/// which data is missing.
std::string RenderPage(PageKind kind, const PageData& data);

/// MIME type for a page ("text/html; charset=utf-8").
std::string ContentTypeFor(PageKind kind);

/// Escapes text for HTML text/attribute context (also used for the JSON embedded
/// in <script> tags, where it must additionally neutralise "</script>").
std::string EscapeHtml(std::string_view text);

/// True when a mode switch request is valid: browserx://modes?set=<mode-id>.
/// Implemented here so the URL contract is testable without CEF.
bool ParseModeSwitchRequest(const InternalUrl& url, std::string* mode_id_out);

}  // namespace web
}  // namespace browserx

#endif  // BROWSERX_WEB_INTERNAL_PAGES_H_
