// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// Unit tests for the browserx:// internal pages.
//
// These matter more than they look: the internal pages are the only part of the
// UI that is fully testable without a GPU, and they are generated from browser
// state, so an escaping bug here is an injection bug in the user's browser.

#include <algorithm>
#include <string>
#include <vector>

#include "core/library.h"
#include "core/mode_engine.h"
#include "test_harness.h"
#include "web/internal_pages.h"

namespace {

using browserx::web::AllInternalHosts;
using browserx::web::ContentTypeFor;
using browserx::web::EscapeHtml;
using browserx::web::InternalUrl;
using browserx::web::KindForHost;
using browserx::web::PageData;
using browserx::web::PageKind;
using browserx::web::PageName;
using browserx::web::PageTitle;
using browserx::web::ParseInternalUrl;
using browserx::web::ParseModeSwitchRequest;
using browserx::web::RenderPage;

PageData SampleData() {
  PageData data;
  data.version = "136.0.0";
  data.active_mode_id = "low-ram";
  data.active_mode_label = "LOW RAM";
  data.runtime_summary = "background throttling on, cache 48 MB, tabs sleep after 60 s";
  data.plan_text = "LOW RAM (low-ram)\n  boot --enable-low-end-device-mode";
  data.plans_json = R"([{"mode":"ultra","label":"ULTRA","tagline":"everyday","command_line":"--disk-cache-size=100663296"}])";
  data.profile_report = "System profile\n  Memory: 3939 MB total";
  data.catalog_report = "Chromium switch catalogue\n  renderer-process-limit  verified";
  data.session_json = R"({"version":1,"windows":[{"tabs":[{"url":"https://example.com/"}]}]})";
  // A real history and bookmark pair, built by the stores the browser uses, so the pages
  // are tested against the exact JSON that reaches them in a live profile.
  browserx::core::History history;
  history.Add("https://example.com/docs", "Documentation", 1'700'000'000'000LL);
  history.Add("https://example.com/docs", "Documentation", 1'700'000'100'000LL);
  history.Add("https://news.example/story", "A story", 1'700'000'200'000LL);
  data.history_json = history.ToJson();
  browserx::core::Bookmarks bookmarks;
  bookmarks.Add("https://example.com/docs", "Documentation", "work", 1'700'000'000'000LL);
  data.bookmarks_json = bookmarks.ToJson();
  data.now_ms = 1'700'000'300'000LL;  // the page renders "when" from this, not from the clock
  return data;
}

bool Contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

/// Compares page kinds as their stable names, so a failure prints "modes" rather
/// than an enum ordinal (the harness cannot stream a scoped enum).
std::string HostFor(const char* host) { return std::string(PageName(KindForHost(host))); }

TEST(InternalPages, HostsMapToPages) {
  EXPECT_EQ(HostFor("modes"), std::string("modes"));
  EXPECT_EQ(HostFor("MODES"), std::string("modes"));  // case-insensitive
  EXPECT_EQ(HostFor("telemetry"), std::string("telemetry"));
  EXPECT_EQ(HostFor("settings"), std::string("settings"));
  EXPECT_EQ(HostFor("about"), std::string("about"));
  EXPECT_EQ(HostFor("session"), std::string("session"));
  EXPECT_EQ(HostFor("no-such-page"), std::string("unknown"));
  // Aliases people type by instinct must not 404.
  EXPECT_EQ(HostFor("flags"), std::string("catalog"));
  EXPECT_EQ(HostFor("performance"), std::string("modes"));
  EXPECT_EQ(HostFor(""), std::string("newtab"));
}

TEST(InternalPages, UrlParsingHandlesPathsQueriesAndJunk) {
  InternalUrl url;
  EXPECT_TRUE(ParseInternalUrl("browserx://modes", &url));
  EXPECT_EQ(url.host, std::string("modes"));
  EXPECT_EQ(url.path, std::string("/"));
  EXPECT_EQ(url.query, std::string());

  EXPECT_TRUE(ParseInternalUrl("browserx://modes?set=eco", &url));
  EXPECT_EQ(url.query, std::string("set=eco"));
  EXPECT_TRUE(ParseInternalUrl("browserx://settings/advanced?x=1", &url));
  EXPECT_EQ(url.host, std::string("settings"));
  EXPECT_EQ(url.path, std::string("/advanced"));
  EXPECT_EQ(url.query, std::string("x=1"));

  // A trailing slash with no query must not produce an empty path.
  EXPECT_TRUE(ParseInternalUrl("browserx://about/", &url));
  EXPECT_EQ(url.path, std::string("/"));

  EXPECT_FALSE(ParseInternalUrl("https://example.com/", &url));
  EXPECT_FALSE(ParseInternalUrl("not a url", &url));
  // Hostile-looking authority is kept as a (lowercased) host, never executed.
  EXPECT_TRUE(ParseInternalUrl("browserx://evil:8000/x", &url));
  EXPECT_EQ(std::string(PageName(KindForHost(url.host))), std::string("unknown"));
}

TEST(InternalPages, EveryAdvertisedHostRendersAPage) {
  EXPECT_TRUE(AllInternalHosts().size() >= 10);
  for (const std::string& host : AllInternalHosts()) {
    const PageKind kind = KindForHost(host);
    EXPECT_NE(std::string(PageName(kind)), std::string("unknown"));
    const std::string html = RenderPage(kind, SampleData());
    EXPECT_TRUE(Contains(html, "<!DOCTYPE html>"));
    EXPECT_TRUE(Contains(html, "</html>"));
    EXPECT_TRUE(Contains(html, "Browser<span>X</span>"));
    // The page must identify itself in the title, so the tab strip is useful.
    // (The shell escapes the title, e.g. "Modes &amp; performance".)
    EXPECT_TRUE(Contains(html, EscapeHtml(PageTitle(kind))));
    EXPECT_EQ(ContentTypeFor(kind), std::string("text/html; charset=utf-8"));
    EXPECT_FALSE(PageName(kind) == std::string("unknown"));
  }
}

TEST(InternalPages, UnknownHostListsTheRealOnes) {
  PageData data = SampleData();
  data.requested_host = "nope";
  const std::string html = RenderPage(PageKind::kUnknown, data);
  EXPECT_TRUE(Contains(html, "No such page"));
  EXPECT_TRUE(Contains(html, "browserx://nope"));
  EXPECT_TRUE(Contains(html, "browserx://modes"));
  EXPECT_TRUE(Contains(html, "browserx://telemetry"));
}

TEST(InternalPages, MissingDataSaysSoInsteadOfRenderingNothing) {
  PageData empty;
  const std::string telemetry = RenderPage(PageKind::kTelemetry, empty);
  EXPECT_TRUE(Contains(telemetry, "is not available yet"));
  const std::string session = RenderPage(PageKind::kSession, empty);
  EXPECT_TRUE(Contains(session, "is not available yet"));
  const std::string modes = RenderPage(PageKind::kModes, empty);
  EXPECT_TRUE(Contains(modes, "No plan has been built yet"));
}

TEST(InternalPages, PagesShowRealBrowserState) {
  const PageData data = SampleData();

  const std::string modes = RenderPage(PageKind::kModes, data);
  EXPECT_TRUE(Contains(modes, "LOW RAM"));         // the active mode from PageData
  EXPECT_TRUE(Contains(modes, "low-ram"));
  EXPECT_TRUE(Contains(modes, "--enable-low-end-device-mode"));  // the real plan text

  const std::string about = RenderPage(PageKind::kAbout, data);
  EXPECT_TRUE(Contains(about, "136.0.0"));
  EXPECT_TRUE(Contains(about, "3939 MB total"));   // profile report

  const std::string session = RenderPage(PageKind::kSession, data);
  EXPECT_TRUE(Contains(session, "https://example.com/"));

  const std::string settings = RenderPage(PageKind::kSettings, data);
  EXPECT_TRUE(Contains(settings, "last-session.json"));
  EXPECT_TRUE(Contains(settings, "renderer-process-limit"));  // catalogue report
}

TEST(InternalPages, UntrustedTextIsEscapedEverywhere) {
  PageData data = SampleData();
  const std::string payload = "<script>alert('x')</script>";
  data.profile_report = payload;
  data.plan_text = payload;
  data.session_json = payload;
  data.catalog_report = payload;
  data.active_mode_label = payload;
  data.runtime_summary = payload;

  for (const PageKind kind :
       {PageKind::kModes, PageKind::kTelemetry, PageKind::kSettings, PageKind::kSession,
        PageKind::kAbout, PageKind::kCatalog, PageKind::kNewTab}) {
    const std::string html = RenderPage(kind, data);
    EXPECT_FALSE(Contains(html, "<script>alert"));
    EXPECT_TRUE(Contains(html, "&lt;script&gt;alert"));
  }

  EXPECT_EQ(EscapeHtml("<a href=\"x\">&'"), std::string("&lt;a href=&quot;x&quot;&gt;&amp;&#39;"));
}

TEST(InternalPages, EmbeddedJsonCannotBreakOutOfTheScriptTag) {
  // A session URL or page title can contain "</script>"; the inlined JSON must not
  // be able to end the script block.
  PageData data = SampleData();
  data.plans_json = R"([{"label":"</script><img src=x onerror=alert(1)>"}])";
  data.requested_host = "modes";
  const std::string html = RenderPage(PageKind::kModes, data);
  EXPECT_FALSE(Contains(html, "</script><img"));
  EXPECT_TRUE(Contains(html, "\\u003c/script\\u003e"));
  // The readable fallback still shows the JSON, escaped for HTML.
  EXPECT_TRUE(Contains(html, "&lt;/script&gt;"));

  // U+2028/U+2029 are line terminators in JavaScript but legal in JSON strings.
  data.plans_json = "[\"\u2028\u2029\"]";
  const std::string escaped = RenderPage(PageKind::kModes, data);
  EXPECT_TRUE(Contains(escaped, "\\u2028"));
  EXPECT_TRUE(Contains(escaped, "\\u2029"));
}

TEST(InternalPages, ModeSwitchRequestsAreValidated) {
  InternalUrl url;
  std::string mode;

  EXPECT_TRUE(ParseInternalUrl("browserx://modes?set=eco", &url));
  EXPECT_TRUE(ParseModeSwitchRequest(url, &mode));
  EXPECT_EQ(mode, std::string("eco"));

  EXPECT_TRUE(ParseInternalUrl("browserx://modes?set=high-performance&x=1", &url));
  EXPECT_TRUE(ParseModeSwitchRequest(url, &mode));
  EXPECT_EQ(mode, std::string("high-performance"));

  EXPECT_TRUE(ParseInternalUrl("browserx://modes?set=low%2Dram", &url));
  EXPECT_TRUE(ParseModeSwitchRequest(url, &mode));
  EXPECT_EQ(mode, std::string("low-ram"));

  // Rejections: wrong page, no parameter, and anything that is not a mode id.
  EXPECT_TRUE(ParseInternalUrl("browserx://telemetry?set=eco", &url));
  EXPECT_FALSE(ParseModeSwitchRequest(url, &mode));
  EXPECT_TRUE(ParseInternalUrl("browserx://modes", &url));
  EXPECT_FALSE(ParseModeSwitchRequest(url, &mode));
  EXPECT_TRUE(ParseInternalUrl("browserx://modes?set=", &url));
  EXPECT_FALSE(ParseModeSwitchRequest(url, &mode));
  EXPECT_TRUE(ParseInternalUrl("browserx://modes?set=../../etc/passwd", &url));
  EXPECT_FALSE(ParseModeSwitchRequest(url, &mode));
  EXPECT_TRUE(ParseInternalUrl("browserx://modes?set=<script>", &url));
  EXPECT_FALSE(ParseModeSwitchRequest(url, &mode));
  EXPECT_TRUE(ParseInternalUrl("browserx://modes?set=ULTRA", &url));
  EXPECT_FALSE(ParseModeSwitchRequest(url, &mode));  // ids are lowercase
}

TEST(InternalPages, UnwiredPagesAdmitItHonestly) {
  // Downloads is still waiting for the CEF download callbacks, and says exactly that.
  const std::string downloads = RenderPage(PageKind::kDownloads, SampleData());
  EXPECT_TRUE(Contains(downloads, "not wired up in this build yet"));
  // Crucially: no fake empty-state table.
  EXPECT_FALSE(Contains(downloads, "<tbody></tbody>"));
  EXPECT_FALSE(Contains(downloads, "You have no"));

  // History and bookmarks used to be in that group. Now they render a real store, so
  // the honest thing is the opposite: they must no longer claim to be unwired.
  for (const PageKind kind : {PageKind::kHistory, PageKind::kBookmarks}) {
    const std::string html = RenderPage(kind, SampleData());
    EXPECT_FALSE(Contains(html, "not wired up in this build yet"))
        << PageName(kind) << " is wired to the library store and must not say otherwise";
    EXPECT_TRUE(Contains(html, "library-table")) << "it renders a real table";
  }
}

TEST(InternalPages, PagesAreSelfContained) {
  // No external *resource* is ever loaded: an internal page must work offline and
  // leak nothing (no fonts, no CDN, no analytics). Credits are hyperlinks, which
  // are inert until the user clicks one, so hrefs are allowed and everything that
  // fetches on its own is not. Page data may also contain real URLs (session
  // history), which is content, not a load.
  for (const std::string& host : AllInternalHosts()) {
    const std::string html = RenderPage(KindForHost(host), SampleData());
    EXPECT_FALSE(Contains(html, "<link"));
    EXPECT_FALSE(Contains(html, "@import"));
    EXPECT_FALSE(Contains(html, "<img"));
    EXPECT_FALSE(Contains(html, "<iframe"));
    EXPECT_FALSE(Contains(html, "src=\"http"));
    EXPECT_FALSE(Contains(html, "src=\"//"));
    EXPECT_FALSE(Contains(html, "url(http"));
    EXPECT_TRUE(Contains(html, "<style>"));
  }
}

// --- the modern shell -------------------------------------------------------
// These pin the behaviour the browser's own UI now depends on. They are cheap
// string assertions on purpose: the alternative is a browser to click, and the
// rules below are exactly the ones that break silently.

TEST(InternalPages, EveryPageCarriesTheModernShell) {
  const PageData data = SampleData();
  for (const std::string& host : AllInternalHosts()) {
    if (KindForHost(host) == PageKind::kBench) continue;  // own standalone document
    const std::string html = RenderPage(KindForHost(host), data);
    EXPECT_TRUE(Contains(html, "<header class=\"topbar\">")) << host;
    EXPECT_TRUE(Contains(html, "class=\"brand\"")) << host;
    EXPECT_TRUE(Contains(html, "<span class=\"mark\">X</span>")) << host;
    EXPECT_TRUE(Contains(html, "Browser<span>X</span>")) << host;
    EXPECT_TRUE(Contains(html, "id=\"palette\"")) << host;
    EXPECT_TRUE(Contains(html, "id=\"palette-input\"")) << host;
    EXPECT_TRUE(Contains(html, "id=\"palette-open\"")) << host;
    EXPECT_TRUE(Contains(html, "class=\"pill mode\"")) << host;
    EXPECT_TRUE(Contains(html, "window.__browserxNav")) << host;
    EXPECT_TRUE(Contains(html, "window.__browserxModes")) << host;
  }
}

TEST(InternalPages, ThePaletteKnowsEveryPageAndEveryMode) {
  // Ctrl+K is only honest if it can reach what it claims to. The data block is
  // generated from the same tables the browser serves and the engine exposes.
  const std::string html = RenderPage(PageKind::kNewTab, SampleData());
  for (const std::string& host : AllInternalHosts()) {
    EXPECT_TRUE(Contains(html, "browserx://" + host)) << "palette is missing " << host;
  }
  for (const browserx::core::ModeInfo& info : browserx::core::AllModes()) {
    EXPECT_TRUE(Contains(html, std::string("id:\"") + info.id + "\""))
        << "palette is missing mode " << info.id;
    EXPECT_TRUE(Contains(html, std::string("browserx://modes?set=") + info.id))
        << "palette cannot switch to " << info.id;
  }
  // The active mode is marked as such, so the palette can show where you are.
  EXPECT_TRUE(Contains(html, "active:true"));
}

TEST(InternalPages, TheStartPageSearchesWhereTheOmniboxDoes) {
  PageData data = SampleData();
  data.search_template = "https://search.example/?q=%s";
  const std::string html = RenderPage(PageKind::kNewTab, data);
  EXPECT_TRUE(Contains(html, "id=\"search-form\""));
  EXPECT_TRUE(Contains(html, "window.__browserxSearchTemplate = \"https://search.example/?q=%s\""));

  // A template is user data: it must be escaped into the script context, never
  // concatenated raw. One with a quote and a </script> is the classic escape test.
  data.search_template = "https://x.example/?q=%s\"></script><script>alert(1)</script>";
  const std::string hostile = RenderPage(PageKind::kNewTab, data);
  EXPECT_FALSE(Contains(hostile, "</script><script>alert(1)"));
  EXPECT_TRUE(Contains(hostile, "\\u003c/script\\u003e") ||
              Contains(hostile, "\\u003c"));
}

TEST(InternalPages, ModesAreClickableTilesNotJustChips) {
  const std::string html = RenderPage(PageKind::kModes, SampleData());
  // Every mode is a .tile, and each one carries the data attribute the page script
  // turns into a browserx://modes?set= navigation.
  size_t tiles = 0;
  size_t search_from = 0;
  while (true) {
    const size_t found = html.find("class=\"tile\" data-set-mode=", search_from);
    if (found == std::string::npos) break;
    ++tiles;
    search_from = found + 1;
  }
  EXPECT_EQ(tiles, browserx::core::AllModes().size());
  EXPECT_TRUE(Contains(html, "aria-current=\"true\"")) << "the active mode is marked";
}

TEST(InternalPages, TelemetryAndSessionRenderTheTabListClientSide) {
  const std::string telemetry = RenderPage(PageKind::kTelemetry, SampleData());
  EXPECT_TRUE(Contains(telemetry, "id=\"tab-list\""));
  EXPECT_TRUE(Contains(telemetry, "id=\"tab-counters\""));
  EXPECT_TRUE(Contains(telemetry, "window.__browserxSession"));
  // The raw JSON stays readable with scripting off, and says so.
  EXPECT_TRUE(Contains(telemetry, "Session JSON"));

  const std::string shortcut_filter = RenderPage(PageKind::kShortcuts, SampleData());
  EXPECT_TRUE(Contains(shortcut_filter, "data-filter=\"#shortcut-table\""));
  EXPECT_TRUE(Contains(shortcut_filter, "id=\"shortcut-table\""));
  EXPECT_TRUE(Contains(shortcut_filter, "data-filter-count"));
  EXPECT_TRUE(Contains(shortcut_filter, "Ctrl+K")) << "the palette shortcut is documented";
}

TEST(InternalPages, CreditsAppearOnEveryPageAndOnAbout) {
  const PageData data = SampleData();
  for (const std::string& host : AllInternalHosts()) {
    const std::string html = RenderPage(KindForHost(host), data);
    // The author is credited in the footer of every page the browser shows.
    EXPECT_TRUE(Contains(html, "codero-sus"));
    EXPECT_TRUE(Contains(html, "the-codero.web.app"));
    EXPECT_TRUE(Contains(html, "github.com/codero-sus/BrowserX"));
  }

  const std::string about = RenderPage(PageKind::kAbout, data);
  EXPECT_TRUE(Contains(about, "Credits"));
  EXPECT_TRUE(Contains(about, "https://github.com/codero-sus"));
  EXPECT_TRUE(Contains(about, "https://the-codero.web.app"));
  EXPECT_TRUE(Contains(about, "BrowserX Personal-Use License"));
  EXPECT_TRUE(Contains(about, "personal, non-commercial use only"));
  EXPECT_TRUE(Contains(about, "no modification"));
  EXPECT_TRUE(Contains(about, "no redistribution or resale"));
  EXPECT_TRUE(Contains(about, "Source-available, not open source"));
  // The About page must link the licence text, not just name it.
  EXPECT_TRUE(Contains(about, "blob/main/LICENSE"));
  // Upstream is credited too: BrowserX would not exist without them.
  EXPECT_TRUE(Contains(about, "chromium.org"));
  EXPECT_TRUE(Contains(about, "chromiumembedded/cef"));
  EXPECT_TRUE(Contains(about, "source.chromium.org"));

  // Credit links must not be able to smuggle a script tag through the author name.
  PageData hostile = SampleData();
  hostile.author = "<img src=x onerror=alert(1)>";
  hostile.website = "\"><script>alert(1)</script>";
  const std::string escaped = RenderPage(PageKind::kAbout, hostile);
  EXPECT_FALSE(Contains(escaped, "<img src=x"));
  EXPECT_FALSE(Contains(escaped, "<script>alert"));
  EXPECT_TRUE(Contains(escaped, "&lt;img src=x"));
}

TEST(InternalPages, LicenceIsPersonalUseOnlyEverywhere) {
  // One source of truth: the same four restrictions appear in the constant, on the
  // About page and in the CLI. A permissive licence must never creep back in.
  const std::string terms = browserx::web::kProjectLicenceTerms;
  for (const char* restriction : {"personal, non-commercial", "no modification",
                                  "no redistribution or resale"}) {
    EXPECT_TRUE(terms.find(restriction) != std::string::npos);
  }
  EXPECT_EQ(std::string(browserx::web::kProjectLicence),
            std::string("BrowserX Personal-Use License"));
  EXPECT_TRUE(std::string(browserx::web::kProjectLicenceUrl).find("LICENSE") != std::string::npos);

  const std::string about = RenderPage(PageKind::kAbout, SampleData());
  EXPECT_TRUE(Contains(about, browserx::web::kProjectLicenceTerms));
  EXPECT_FALSE(Contains(about, "BSD-3-Clause (BrowserX)"));
  // The upstream licences are still credited - not relicensed.
  EXPECT_TRUE(Contains(about, "third-party licences for the Chromium/CEF runtime") ||
              Contains(about, "keeps its own licences"));
}

TEST(InternalPages, IdentityConstantsMatchTheProduct) {
  EXPECT_EQ(std::string(browserx::web::kProjectAuthor), std::string("codero-sus"));
  EXPECT_EQ(std::string(browserx::web::kProjectSite), std::string("the-codero.web.app"));
  EXPECT_EQ(std::string(browserx::web::kProjectSiteUrl), std::string("https://the-codero.web.app"));
  EXPECT_EQ(std::string(browserx::web::kProjectRepositoryUrl),
            std::string("https://github.com/codero-sus/BrowserX"));
  // Defaults are wired: a caller that fills nothing in still gets real credits.
  const PageData defaults;
  EXPECT_EQ(defaults.author, std::string(browserx::web::kProjectAuthor));
  EXPECT_EQ(defaults.website, std::string(browserx::web::kProjectSite));
}

}  // namespace

TEST(InternalPages, EveryModeGetsAchipAndTheCliListsThemAll) {
  // The modes page must show every mode the engine knows, including future ones:
  // the chips are generated from AllModes(), not from a hardcoded list.
  const std::string html = RenderPage(PageKind::kModes, SampleData());
  for (const browserx::core::ModeInfo& info : browserx::core::AllModes()) {
    EXPECT_TRUE(Contains(html, std::string("data-set-mode=\"") + info.id + "\""))
        << "mode chip missing: " << info.id;
    EXPECT_TRUE(Contains(html, std::string("browserx://modes?set=") + info.id));
  }
  // The specialist modes are the point of this test: they must be reachable from
  // the page without any extra wiring.
  for (const char* id : {"super-internet", "download", "streaming"}) {
    EXPECT_TRUE(Contains(html, std::string("data-set-mode=\"") + id + "\""));
  }
}

// ---------------------------------------------------------------------------
// History and bookmarks: the two pages that read a real store
// ---------------------------------------------------------------------------
TEST(InternalPages, HistoryRendersRealRowsInDateOrder) {
  const PageData data = SampleData();
  const std::string html = RenderPage(PageKind::kHistory, data);

  EXPECT_TRUE(Contains(html, "browserx://history")) << "the page identifies itself in the nav";
  EXPECT_TRUE(Contains(html, ">A story<")) << "the newest page is listed";
  EXPECT_TRUE(Contains(html, "Documentation"));
  EXPECT_TRUE(Contains(html, "example.com")) << "the host of each row is shown, not just the URL";
  EXPECT_TRUE(Contains(html, "data-filter=\"#library-table\""))
      << "filtering reuses the page's one filter implementation";
  EXPECT_TRUE(Contains(html, "<span class=\"v\">2</span><span class=\"k\">pages</span>"))
      << "the KPI counts pages, not visits: two pages, one of them visited twice";
  EXPECT_TRUE(Contains(html, "<span class=\"v\">3</span><span class=\"k\">visits</span>"));

  // Relative time comes from data.now_ms, so the rendered page and the store agree.
  EXPECT_TRUE(Contains(html, "1 min ago")) << "the newest visit, 100 s before the page's now";
  EXPECT_TRUE(Contains(html, "3 min ago")) << "and the older one, 200 s before it";
  EXPECT_FALSE(Contains(html, "1970")) << "a page rendered with a clock never shows epoch dates";

  // Newest first: "A story" appears before the docs row.
  EXPECT_TRUE(html.find(">A story<") < html.find(">Documentation<"));

  // A fragment in the stored URL is part of the same page, so it is not listed twice.
  EXPECT_TRUE(html.find("Documentation") != html.find("Documentation", html.find("Documentation") + 1))
      << "the page appears exactly once even though it was visited twice";

  // Escaping: a title with markup in it must not become markup.
  PageData hostile = SampleData();
  browserx::core::History evil;
  evil.Add("https://evil.example/?q=1", "\"><script>alert(1)</script>", 1'700'000'300'000LL);
  hostile.history_json = evil.ToJson();
  const std::string hostile_html = RenderPage(PageKind::kHistory, hostile);
  EXPECT_FALSE(Contains(hostile_html, "<script>alert(1)"))
      << "a page title cannot inject script into a browser page";
  EXPECT_TRUE(Contains(hostile_html, "&lt;script&gt;"));
}

TEST(InternalPages, BookmarksShowFoldersAndSayWhenTheyAreSamples) {
  const PageData data = SampleData();
  const std::string html = RenderPage(PageKind::kBookmarks, data);

  EXPECT_TRUE(Contains(html, ">Documentation<"));
  EXPECT_TRUE(Contains(html, ">work<")) << "the folder column carries the folder name";
  EXPECT_TRUE(Contains(html, "Folder")) << "and the column says what it holds";
  EXPECT_TRUE(Contains(html, "<span class=\"v\">1</span><span class=\"k\">bookmarks</span>"));
  EXPECT_TRUE(Contains(html, "<span class=\"v\">1</span><span class=\"k\">folders</span>"));
  EXPECT_FALSE(Contains(html, "Sample data")) << "real data is never labelled as a sample";

  // The preview and the tests both use sample data; when they do, the page says so.
  PageData sample = SampleData();
  sample.library_is_sample = true;
  const std::string sample_html = RenderPage(PageKind::kBookmarks, sample);
  EXPECT_TRUE(Contains(sample_html, "Sample data"))
      << "invented rows must never be presented as the user's bookmarks";

  // Empty stores say what to do next instead of rendering an empty table.
  PageData empty = SampleData();
  empty.history_json.clear();
  empty.bookmarks_json.clear();
  EXPECT_TRUE(Contains(RenderPage(PageKind::kHistory, empty), "History"));
  EXPECT_TRUE(Contains(RenderPage(PageKind::kBookmarks, empty), "Bookmarks"));
}

TEST(InternalPages, LibraryPagesSurviveBrokenJsonAndNeedNoNetwork) {
  PageData broken = SampleData();
  broken.history_json = "{this is not json";
  broken.bookmarks_json = "[]";
  const std::string history = RenderPage(PageKind::kHistory, broken);
  EXPECT_TRUE(Contains(history, "could not be read"))
      << "a corrupt store is reported on the page, not hidden behind an empty list";
  const std::string bookmarks = RenderPage(PageKind::kBookmarks, broken);
  EXPECT_TRUE(Contains(bookmarks, "Bookmarks"));

  // Both pages stay self-contained: no external resource may be referenced, or the
  // browser would need the network to show its own history.
  for (const PageKind kind : {PageKind::kHistory, PageKind::kBookmarks}) {
    const std::string html = RenderPage(kind, SampleData());
    EXPECT_FALSE(Contains(html, "http://")) << PageName(kind) << " must not fetch anything";
    EXPECT_FALSE(Contains(html, "src=\"http"));
    EXPECT_FALSE(Contains(html, "<link ")) << "no external stylesheet";
    EXPECT_FALSE(Contains(html, "//cdn"));
  }
}

TEST(InternalPages, ErrorRequestsAreParsedInEverySpelling) {
  InternalUrl url;
  int code = 0;
  std::string failed;

  EXPECT_TRUE(ParseInternalUrl("browserx://error?code=-105&url=https%3A%2F%2Fx.example%2Fa", &url));
  EXPECT_TRUE(ParseErrorRequest(url, &code, &failed));
  EXPECT_EQ(code, -105);
  EXPECT_EQ(failed, std::string("https://x.example/a"));

  // The name form, with and without the ERR_ prefix, because a hand-typed URL and a
  // generated one are not the same shape.
  EXPECT_TRUE(ParseInternalUrl("browserx://error?code=ERR_NAME_NOT_RESOLVED", &url));
  EXPECT_TRUE(ParseErrorRequest(url, &code, &failed));
  EXPECT_EQ(code, -105);
  EXPECT_TRUE(ParseInternalUrl("browserx://error?code=name_not_resolved", &url));
  EXPECT_TRUE(ParseErrorRequest(url, &code, &failed));
  EXPECT_EQ(code, -105);

  // A real code this build has no explanation for is still a code, and it must survive
  // the trip: the page prints it rather than losing it.
  EXPECT_TRUE(ParseInternalUrl("browserx://error?code=-9999", &url));
  EXPECT_TRUE(ParseErrorRequest(url, &code, &failed));
  EXPECT_EQ(code, -9999);

  // '+' is a space in a query string, and percent escapes are decoded once.
  EXPECT_TRUE(ParseInternalUrl("browserx://error?code=-102&url=a+b%20c", &url));
  EXPECT_TRUE(ParseErrorRequest(url, &code, &failed));
  EXPECT_EQ(failed, std::string("a b c"));

  // Rejections: the wrong page, no code at all, and a code that is not a code.
  EXPECT_TRUE(ParseInternalUrl("browserx://about?code=-105", &url));
  EXPECT_FALSE(ParseErrorRequest(url, &code, &failed));
  EXPECT_TRUE(ParseInternalUrl("browserx://error?url=x", &url));
  EXPECT_FALSE(ParseErrorRequest(url, &code, &failed));
  EXPECT_TRUE(ParseInternalUrl("browserx://error?code=", &url));
  EXPECT_FALSE(ParseErrorRequest(url, &code, &failed));
  // A positive number is not a Chromium error code, and inventing one would be a lie
  // with no upside; it is refused so the page says it has no code.
  EXPECT_TRUE(ParseInternalUrl("browserx://error?code=105", &url));
  EXPECT_FALSE(ParseErrorRequest(url, &code, &failed));
}

TEST(InternalPages, ErrorPageExplainsTheFailureAndNeverBlamEsTheWrongParty) {
  PageData data;
  data.error_code = -105;
  data.error_url = "https://nowhere.example/tea";
  const std::string html = RenderPage(PageKind::kError, data);
  EXPECT_TRUE(html.find("That address could not be found") != std::string::npos);
  EXPECT_TRUE(html.find("https://nowhere.example/tea") != std::string::npos);
  EXPECT_TRUE(html.find("ERR_NAME_NOT_RESOLVED (-105)") != std::string::npos);
  EXPECT_TRUE(html.find("Check the spelling") != std::string::npos);
  // A DNS failure is not the browser's doing, and the page must not hint that it is.
  EXPECT_TRUE(html.find("BrowserX made this decision") == std::string::npos);
  // No certificate lecture where there is no certificate problem.
  EXPECT_TRUE(html.find("about trust, not convenience") == std::string::npos);
}

TEST(InternalPages, ErrorPageOwnsUpWhenTheBrowserIsTheReason) {
  PageData data;
  data.error_code = -20;  // BLOCKED_BY_CLIENT: the shield, in this browser
  data.error_url = "https://tracker.example/pixel";
  const std::string html = RenderPage(PageKind::kError, data);
  EXPECT_TRUE(html.find("BrowserX blocked this request") != std::string::npos);
  EXPECT_TRUE(html.find("BrowserX made this decision") != std::string::npos);
  EXPECT_TRUE(html.find("browserx://settings") != std::string::npos);
}

TEST(InternalPages, ErrorPageDoesNotOfferAWayPastATrustFailure) {
  PageData data;
  data.error_code = -200;  // CERT_COMMON_NAME_INVALID
  data.error_url = "https://not-really-your-bank.example/login";
  const std::string html = RenderPage(PageKind::kError, data);
  EXPECT_TRUE(html.find("about trust, not convenience") != std::string::npos);
  // The phrase appears only to say it does not exist, and nothing on the page is a
  // button that would walk past the failure.
  EXPECT_TRUE(html.find("does not offer a") != std::string::npos);
  EXPECT_TRUE(html.find(">Continue") == std::string::npos);
  EXPECT_TRUE(html.find("Proceed anyway") == std::string::npos);
  // The address that failed is shown as text to read, never as a link to click: an error
  // page that links back to the untrusted host it just refused to load would be a button
  // past the failure wearing a different coat. The credits' own links still work.
  EXPECT_TRUE(html.find("href=\"https://not-really-your-bank.example") == std::string::npos);
  EXPECT_TRUE(html.find("https://not-really-your-bank.example/login") != std::string::npos);
  EXPECT_TRUE(html.find("href=\"https://github.com/codero-sus\"") != std::string::npos);
}

TEST(InternalPages, ErrorPageSaysWhenItHasNoExplanation) {
  PageData data;
  data.error_code = -4242;
  const std::string html = RenderPage(PageKind::kError, data);
  EXPECT_TRUE(html.find("cannot explain") != std::string::npos);
  EXPECT_TRUE(html.find("-4242") != std::string::npos);
  // The unknown-code path must not borrow another code's name.
  EXPECT_TRUE(html.find("ERR_") == std::string::npos);
  // ...and it lists what it *can* explain, so the gap is visible rather than hidden.
  EXPECT_TRUE(html.find("Every failure code this build can explain") != std::string::npos);
}

TEST(InternalPages, ErrorPageEscapesWhatTheNetworkGaveIt) {
  PageData data;
  data.error_code = -102;
  data.error_url = "https://evil.example/<script>alert(1)</script>";
  data.error_text = "\"quoted\" & <b>bold</b>";
  const std::string html = RenderPage(PageKind::kError, data);
  EXPECT_TRUE(html.find("<script>alert(1)</script>") == std::string::npos);
  EXPECT_TRUE(html.find("&lt;script&gt;") != std::string::npos);
  EXPECT_TRUE(html.find("&amp;") != std::string::npos);
  // The page's own script is still there: escaping the data must not break the page.
  EXPECT_TRUE(html.find("browserx internal-page") != std::string::npos ||
              html.find("<script") != std::string::npos);
}

TEST(InternalPages, ErrorPageIsReachableAndNamedLikeEveryOtherPage) {
  EXPECT_TRUE(KindForHost("error") == PageKind::kError);
  EXPECT_EQ(std::string(PageName(PageKind::kError)), "error");
  EXPECT_TRUE(PageTitle(PageKind::kError) != nullptr);
  EXPECT_TRUE(std::string(ContentTypeFor(PageKind::kError)).find("text/html") != std::string::npos);
  bool listed = false;
  for (const std::string& host : AllInternalHosts()) {
    if (host == "error") listed = true;
  }
  EXPECT_TRUE(listed);
}

// --- the native-feature pages ----------------------------------------------
TEST(InternalPages, SecurityPageExplainsAndDisclaims) {
  PageData data;
  data.security_url = "https://bank.example/login";
  const std::string html = RenderPage(PageKind::kSecurity, data);
  EXPECT_TRUE(html.find("Secure") != std::string::npos);
  EXPECT_TRUE(html.find("bank.example") != std::string::npos);
  // The disclaimer is the product: an indicator may not imply it checked honesty.
  EXPECT_TRUE(html.find("certificate") != std::string::npos);
  EXPECT_TRUE(html.find("honest") != std::string::npos);
}

TEST(InternalPages, SecurityPageWarnsOnPlainHttp) {
  PageData data;
  data.security_url = "http://neighbourhood.example/";
  const std::string html = RenderPage(PageKind::kSecurity, data);
  EXPECT_TRUE(html.find("Not secure") != std::string::npos);
  EXPECT_TRUE(html.find("not the same as the site being malicious") != std::string::npos);
}

TEST(InternalPages, SecurityPageWithoutAnAddressAsksForOneInsteadOfGuessing) {
  const std::string html = RenderPage(PageKind::kSecurity, PageData{});
  EXPECT_TRUE(html.find("Open a page") != std::string::npos);
  EXPECT_TRUE(html.find("Not secure") == std::string::npos) << "nothing was checked";
}

TEST(InternalPages, ShieldPageReportsTheImportedFileHonestly) {
  PageData data;
  data.shield_rules = 400;
  data.shield_categories = {"Ads - 250 rules"};
  data.rules_file = "/home/u/.browserx/profile/rules.txt";
  data.rules_present = true;
  data.rules_added = 3;
  data.rules_duplicates = 2;
  data.rules_skipped = 1;
  data.rules_examples = {"example.com##.ad"};
  const std::string html = RenderPage(PageKind::kShield, data);
  EXPECT_TRUE(html.find("400") != std::string::npos) << "the live rule count, not a guess";
  EXPECT_TRUE(html.find("3 rules") != std::string::npos);
  EXPECT_TRUE(html.find("2 already present") != std::string::npos);
  EXPECT_TRUE(html.find("1 line(s)") != std::string::npos);
  EXPECT_TRUE(html.find("example.com##.ad") != std::string::npos)
      << "a skip count without the skipped line would not tell the user which side is wrong";
  EXPECT_TRUE(html.find("not uBlock") != std::string::npos);
}

TEST(InternalPages, ShieldPageWithoutAFileSaysWhereToPutIt) {
  PageData data;
  data.rules_file = "/p/rules.txt";
  const std::string html = RenderPage(PageKind::kShield, data);
  EXPECT_TRUE(html.find("No rule file yet") != std::string::npos);
  EXPECT_TRUE(html.find("/p/rules.txt") != std::string::npos);
}

TEST(InternalPages, CaptchaNoticesAreNotAdsForSolving) {
  // The start page links to the assist, so the wording of that link is load-bearing:
  // it must never read like a solver.
  PageData data;
  const std::string html = RenderPage(PageKind::kNewTab, data);
  EXPECT_TRUE(html.find("browserx://tor") != std::string::npos);
  EXPECT_TRUE(html.find("browserx://extensions") != std::string::npos);
  EXPECT_TRUE(html.find("browserx://security") != std::string::npos);
  EXPECT_TRUE(html.find("browserx://shield") != std::string::npos);
  EXPECT_TRUE(html.find("Never solves") != std::string::npos);
  EXPECT_TRUE(html.find("solver") == std::string::npos || html.find("never") != std::string::npos);
}
