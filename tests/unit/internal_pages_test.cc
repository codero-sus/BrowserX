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
  for (const PageKind kind : {PageKind::kHistory, PageKind::kBookmarks, PageKind::kDownloads}) {
    const std::string html = RenderPage(kind, SampleData());
    EXPECT_TRUE(Contains(html, "not wired up in this build yet"));
    // Crucially: no fake empty-state table.
    EXPECT_FALSE(Contains(html, "<tbody></tbody>"));
    EXPECT_FALSE(Contains(html, "You have no"));
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

TEST(InternalPages, CreditsAppearOnEveryPageAndOnAbout) {
  const PageData data = SampleData();
  for (const std::string& host : AllInternalHosts()) {
    const std::string html = RenderPage(KindForHost(host), data);
    // The author is credited in the footer of every page the browser shows.
    EXPECT_TRUE(Contains(html, "codero-sus"));
    EXPECT_TRUE(Contains(html, "the-codero.web.app"));
    EXPECT_TRUE(Contains(html, "github.com/codero-sus/MyBrowser"));
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
            std::string("https://github.com/codero-sus/MyBrowser"));
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
