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

#include <cstdint>
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
  kTor,       // Tor mode: what it does, what it does not, and whether it is reachable
  kSecurity,  // connection details for the page the indicator was clicked on
  kShield,    // the tracker shield: what it blocks, and what the user's own file added
  kExtensions,
  kError,    // what a failed load means, in plain language (core::ExplainNetError)
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
  /// The commit this binary was built from (tools/revision.py), or "unknown".
  std::string git_revision = "unknown";
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
  // --- native features (browserx://tor, browserx://extensions) -----------------
  /// The address whose connection details are shown on browserx://security. Empty when the
  /// page is opened directly, with nothing to explain.
  std::string security_url;
  /// True when the browser was started with Tor mode on.
  bool tor_on = false;
  int tor_port = 9050;
  /// Whether something answered on the SOCKS port; the page must not guess.
  bool tor_reachable = false;
  /// What the probe found, in plain words.
  std::string tor_status;
  /// The leak surface, verbatim from core::TorWarnings(). Never empty when tor_on.
  std::vector<std::string> tor_warnings;
  bool extensions_on = true;
  std::string extensions_dir;
  /// One line per extension: name, version, manifest generation.
  std::vector<std::string> extension_lines;
  /// Why each refused folder was refused.
  std::vector<std::string> extension_refused;
  // --- the shield (browserx://shield) -----------------------------------------
  bool shield_on = true;
  /// Rules in the live list: curated defaults plus whatever the user's file added.
  size_t shield_rules = 0;
  /// `core::Blocklist::Describe()`-style counts per category, pre-formatted.
  std::vector<std::string> shield_categories;
  /// `<profile>/rules.txt` and what came out of it.
  std::string rules_file;
  bool rules_present = false;
  size_t rules_added = 0;
  size_t rules_duplicates = 0;
  size_t rules_skipped = 0;
  /// The lines the importer did not understand. Shown verbatim: a skip count alone would
  /// not tell the user whether their list or the importer is at fault.
  std::vector<std::string> rules_examples;

  /// The user's search template (core::Settings::search_template). The start page
  /// searches where the omnibox searches; the default is only a fallback for callers
  /// that render pages without settings (tests, the preview).
  std::string search_template = "https://duckduckgo.com/?q=%s";
  /// browserx://history as JSON (core::History::ToJson()). The page parses this and
  /// renders the rows itself, so the same renderer works in the browser, in the preview
  /// server and in the mobile panels. Empty means "no history store in this build".
  std::string history_json;
  /// browserx://bookmarks as JSON (core::Bookmarks::ToJson()).
  std::string bookmarks_json;
  /// Milliseconds since the epoch, used for "3 days ago". 0 means the page falls back to
  /// the system clock; the browser passes its own value so a rendered page and the
  /// history it came from agree on what "now" is.
  int64_t now_ms = 0;
  /// core::DiffPlans against the active mode, as JSON: one entry per mode with a summary
  /// line ("adds 4 switches and removes 2") and the differing flags with their reasons.
  /// The engine computes it, so the page cannot describe a mode differently from what the
  /// browser would apply.
  std::string mode_diffs_json;
  /// True when the JSON above is sample data rather than this browser's real store. Pages
  /// say so out loud instead of presenting invented rows as history (the preview server
  /// and the tests both need that honesty).
  bool library_is_sample = false;

  /// The host that was actually requested. Only used by the "no such page" view,
  /// so the user sees the address they typed instead of a generic message.
  std::string requested_host;

  /// The error page (browserx://error?code=-105&url=...). `error_code` is Chromium's
  /// net error value and `error_url` the address that failed; both are shown as they
  /// came, because a page that paraphrases the failure without showing the code is
  /// impossible to search or report. `error_text` is the raw text CEF supplied, when
  /// there is one, and `error_is_headless_probe` marks the page rendered by the preview
  /// and the page dump so the view can say it is an example rather than a real failure.
  int error_code = 0;
  std::string error_url;
  std::string error_text;
  bool error_is_sample = false;

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

/// Reads an error code out of a browserx://error URL ("?code=-105"). Accepts the plain
/// number, a leading "ERR_", or the upstream name, because a hand-typed URL and a
/// generated one are not always the same shape; returns false when nothing usable is
/// there, and the page then says it has no code rather than pretending it has one.
bool ParseErrorRequest(const InternalUrl& url, int* code_out, std::string* url_out);

/// True when a mode switch request is valid: browserx://modes?set=<mode-id>.
/// Implemented here so the URL contract is testable without CEF.
bool ParseModeSwitchRequest(const InternalUrl& url, std::string* mode_id_out);

}  // namespace web
}  // namespace browserx

#endif  // BROWSERX_WEB_INTERNAL_PAGES_H_
