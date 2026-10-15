// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// Unit tests for omnibox input parsing (src/core/url_utils.cc).

#include <string>

#include "core/url_utils.h"
#include "test_harness.h"

namespace {

using browserx::core::url::BuildSearchUrl;
using browserx::core::url::EncodePath;
using browserx::core::url::EncodeQuery;
using browserx::core::url::HasScheme;
using browserx::core::url::IsIpLiteralHost;
using browserx::core::url::IsLocalhost;
using browserx::core::url::NormalizeForCompare;
using browserx::core::url::OriginOf;
using browserx::core::url::QueryParam;
using browserx::core::url::RegistrableDomain;
using browserx::core::url::SameSite;
using browserx::core::url::SchemeIsSecure;
using browserx::core::url::WithoutQueryParam;
using browserx::core::url::HostMatchesPattern;
using browserx::core::url::HostOf;
using browserx::core::url::IsHttp;
using browserx::core::url::IsInternal;
using browserx::core::url::IsPrivileged;
using browserx::core::url::LooksLikeUrl;
using browserx::core::url::PortOf;
using browserx::core::url::PrettyForDisplay;
using browserx::core::url::RegistrableHostForDisplay;
using browserx::core::url::ResolveInput;
using browserx::core::url::SameOrigin;
using browserx::core::url::SanitizeInput;
using browserx::core::url::SchemeOf;
using browserx::core::url::StripFragment;
using browserx::core::url::StripTrackingParams;

constexpr const char* kSearch = "https://duckduckgo.com/?q=%s";

TEST(Url, PercentEncoding) {
  EXPECT_EQ(EncodeQuery("a b+c"), std::string("a+b%2Bc"));
  EXPECT_EQ(EncodeQuery("a/b?c=d"), std::string("a%2Fb%3Fc%3Dd"));
  EXPECT_EQ(EncodeQuery("a b", false), std::string("a%20b"));
  // A path *segment* has no separators, so '/' is encoded too.
  EXPECT_EQ(EncodePath("a b/c"), std::string("a%20b%2Fc"));
  EXPECT_EQ(EncodeQuery("\xE2\x82\xAC"), std::string("%E2%82%AC"));
}

TEST(Url, SchemeDetection) {
  EXPECT_TRUE(HasScheme("https://example.com"));
  EXPECT_TRUE(HasScheme("browserx://settings"));
  EXPECT_TRUE(HasScheme("about:blank"));
  EXPECT_TRUE(HasScheme("mailto:a@b.c"));
  EXPECT_FALSE(HasScheme("example.com"));
  EXPECT_FALSE(HasScheme("localhost:8080"));
  EXPECT_EQ(SchemeOf("HTTPS://Example.com/A"), std::string("https"));
  EXPECT_EQ(SchemeOf("browserx://telemetry"), std::string("browserx"));
  EXPECT_EQ(SchemeOf("example.com"), std::string(""));
}

TEST(Url, HostAndPortExtraction) {
  EXPECT_EQ(HostOf("https://user:pw@Example.com:8443/x"), std::string("example.com"));
  EXPECT_EQ(HostOf("https://www.example.com"), std::string("www.example.com"));
  EXPECT_EQ(HostOf("browserx://telemetry"), std::string("telemetry"));
  EXPECT_EQ(HostOf("about:blank"), std::string(""));
  EXPECT_EQ(RegistrableHostForDisplay("www.example.com"), std::string("example.com"));
  EXPECT_EQ(RegistrableHostForDisplay("example.com"), std::string("example.com"));
  EXPECT_EQ(PortOf("https://example.com:8443/"), 8443);
  EXPECT_EQ(PortOf("https://example.com/"), 0);
}

TEST(Url, SchemeFamilies) {
  EXPECT_TRUE(IsHttp("https://example.com"));
  EXPECT_TRUE(IsHttp("http://example.com"));
  EXPECT_FALSE(IsHttp("browserx://settings"));
  EXPECT_FALSE(IsHttp("about:blank"));
  EXPECT_TRUE(IsInternal("browserx://settings"));
  EXPECT_TRUE(IsInternal("browserx://telemetry?x=1"));
  EXPECT_FALSE(IsInternal("https://browserx.com"));
  EXPECT_TRUE(IsPrivileged("about:blank"));
  EXPECT_TRUE(IsPrivileged("view-source:https://example.com"));
  EXPECT_FALSE(IsPrivileged("https://example.com"));
}

TEST(Url, LooksLikeUrlHeuristics) {
  EXPECT_TRUE(LooksLikeUrl("example.com"));
  EXPECT_TRUE(LooksLikeUrl("https://example.com/x"));
  EXPECT_TRUE(LooksLikeUrl("localhost:8080"));
  EXPECT_TRUE(LooksLikeUrl("192.168.1.1/admin"));
  EXPECT_TRUE(LooksLikeUrl("browserx://telemetry"));
  EXPECT_FALSE(LooksLikeUrl("how to bake bread"));
  EXPECT_FALSE(LooksLikeUrl("chromium memory usage"));
  EXPECT_FALSE(LooksLikeUrl("banana"));
}

TEST(Url, ResolveInputHandlesUrlsSearchesAndDanger) {
  EXPECT_EQ(ResolveInput("example.com", kSearch), std::string("https://example.com"));
  EXPECT_EQ(ResolveInput("https://example.com/a", kSearch), std::string("https://example.com/a"));
  EXPECT_EQ(ResolveInput("  example.com  ", kSearch), std::string("https://example.com"));

  const std::string searched = ResolveInput("low ram browser", kSearch);
  EXPECT_TRUE(searched.find("duckduckgo.com") != std::string::npos);
  EXPECT_TRUE(searched.find("low") != std::string::npos);

  // Empty input must not navigate anywhere.
  EXPECT_TRUE(ResolveInput("", kSearch).empty());
  EXPECT_TRUE(ResolveInput("   ", kSearch).empty());

  // A dangerous scheme is treated as a search query, never as a navigation.
  const std::string javascript = ResolveInput("javascript:alert(1)", kSearch);
  EXPECT_TRUE(javascript.find("duckduckgo.com") != std::string::npos);
  const std::string data = ResolveInput("data:text/html,<script>x</script>", kSearch);
  EXPECT_TRUE(data.find("duckduckgo.com") != std::string::npos);
  EXPECT_TRUE(ResolveInput("blob:https://example.com/x", kSearch).find("duckduckgo.com") !=
              std::string::npos);
  // Real navigable schemes still pass through untouched.
  EXPECT_EQ(ResolveInput("about:blank", kSearch), std::string("about:blank"));
  EXPECT_EQ(ResolveInput("file:///tmp/x.html", kSearch), std::string("file:///tmp/x.html"));
}

TEST(Url, BuildsSearchUrls) {
  EXPECT_EQ(BuildSearchUrl(kSearch, "hello world"),
            std::string("https://duckduckgo.com/?q=hello+world"));
  EXPECT_EQ(BuildSearchUrl("https://example.com/search", "x"),
            std::string("https://example.com/search?q=x"));
  EXPECT_EQ(BuildSearchUrl("%s", "x"), std::string("x"));
}

TEST(Url, SanitizeInputStripsControlCharacters) {
  const std::string hostile = std::string("exa") + '\x01' + "mple.com\x7f";
  EXPECT_EQ(SanitizeInput(hostile), std::string("example.com"));
  // Zero-width and bidi-override characters are a known URL spoofing vector.
  EXPECT_EQ(SanitizeInput("exa\xE2\x80\x8Bmple.com\xE2\x80\xAE"), std::string("example.com"));
  // A pasted URL wrapped over two lines becomes one line, not two navigations.
  const std::string wrapped = SanitizeInput("https://a\n\tb");
  EXPECT_FALSE(wrapped.find('\n') != std::string::npos);
  EXPECT_FALSE(wrapped.find('\t') != std::string::npos);
  EXPECT_EQ(SanitizeInput("  spaced  "), std::string("spaced"));
}

TEST(Url, DisplayFormHidesNoise) {
  EXPECT_EQ(PrettyForDisplay("https://www.example.com/a?b=1"), std::string("example.com/a?b=1"));
  EXPECT_EQ(PrettyForDisplay("https://example.com/"), std::string("example.com"));
  // Only a bare root slash is dropped; a real path keeps its trailing slash.
  EXPECT_EQ(PrettyForDisplay("https://example.com/a/"), std::string("example.com/a/"));
  EXPECT_EQ(PrettyForDisplay("browserx://telemetry"), std::string("browserx://telemetry"));
  const std::string long_url = std::string("https://example.com/") + std::string(200, 'x');
  // Truncation appends a 3-byte ellipsis, hence the +3.
  EXPECT_TRUE(PrettyForDisplay(long_url, 40).size() <= 43);
}

TEST(Url, SameOriginAndCleanLinks) {
  EXPECT_TRUE(SameOrigin("https://example.com/a", "https://example.com/b"));
  EXPECT_FALSE(SameOrigin("https://example.com/a", "http://example.com/a"));
  EXPECT_FALSE(SameOrigin("https://example.com/a", "https://other.com/a"));
  EXPECT_TRUE(SameOrigin("https://example.com:443/a", "https://example.com/b"));
  EXPECT_EQ(StripFragment("https://a/b#frag"), std::string("https://a/b"));
  EXPECT_EQ(StripFragment("https://a/b"), std::string("https://a/b"));
}

TEST(Url, TrackingParametersAreRemovable) {
  const std::string cleaned =
      StripTrackingParams("https://example.com/x?utm_source=news&id=7&fbclid=abc");
  EXPECT_TRUE(cleaned.find("id=7") != std::string::npos);
  EXPECT_FALSE(cleaned.find("utm_source") != std::string::npos);
  EXPECT_FALSE(cleaned.find("fbclid") != std::string::npos);
  EXPECT_TRUE(browserx::core::url::TrackingParamPrefixes().size() > 3);
}

TEST(Url, HostPatternMatching) {
  EXPECT_TRUE(HostMatchesPattern("ads.doubleclick.net", "doubleclick.net"));
  EXPECT_TRUE(HostMatchesPattern("doubleclick.net", "doubleclick.net"));
  EXPECT_TRUE(HostMatchesPattern("a.b.example.com", "*.example.com"));
  EXPECT_TRUE(HostMatchesPattern("example.com", "=example.com"));
  EXPECT_FALSE(HostMatchesPattern("notexample.com", "=example.com"));
  EXPECT_FALSE(HostMatchesPattern("notexample.com", "example.com"));
}

}  // namespace

TEST(Url, PathExtractionForShieldPathRules) {
  // The request guard hands this to the shield's path rules, so it must never
  // include the host and must always be usable as a plain string.
  EXPECT_EQ(browserx::core::url::PathOf("https://example.com/a/b?x=1"), std::string("/a/b?x=1"));
  EXPECT_EQ(browserx::core::url::PathOf("https://example.com"), std::string("/"));
  EXPECT_EQ(browserx::core::url::PathOf("https://example.com/"), std::string("/"));
  EXPECT_EQ(browserx::core::url::PathOf("http://example.com/ad/track.gif#frag"),
            std::string("/ad/track.gif"));
  EXPECT_EQ(browserx::core::url::PathOf("not a url"), std::string("/"));
  EXPECT_EQ(browserx::core::url::PathOf("https://example.com/promo/"), std::string("/promo/"));
}

// --- origin, literals, query, domains (the helpers the UI leans on) ---------

TEST(UrlUtils, OriginIsSchemeHostAndNonDefaultPort) {
  EXPECT_EQ(OriginOf("https://example.com/a/b?c=1"), std::string("https://example.com"));
  EXPECT_EQ(OriginOf("https://example.com:8443/x"), std::string("https://example.com:8443"));
  // The default port is noise: it is the same origin with or without it.
  EXPECT_EQ(OriginOf("https://example.com:443/x"), std::string("https://example.com"));
  EXPECT_EQ(OriginOf("http://example.com:80/x"), std::string("http://example.com"));
  EXPECT_EQ(OriginOf("http://example.com:8080/"), std::string("http://example.com:8080"));
  // An internal page does have an origin string; what it does not have is a transport
  // to report, which is the question SchemeIsSecure() answers.
  EXPECT_EQ(OriginOf("browserx://modes"), std::string("browserx://modes"));
  EXPECT_TRUE(OriginOf("about:blank").empty());
}

TEST(UrlUtils, IpLiteralHostsAreRecognised) {
  EXPECT_TRUE(IsIpLiteralHost("127.0.0.1"));
  EXPECT_TRUE(IsIpLiteralHost("192.168.1.10"));
  EXPECT_TRUE(IsIpLiteralHost("[::1]"));
  EXPECT_TRUE(IsIpLiteralHost("fe80::1"));
  EXPECT_FALSE(IsIpLiteralHost("example.com"));
  EXPECT_FALSE(IsIpLiteralHost("example.123"));
}

TEST(UrlUtils, TrackingParametersCanBeRemovedAndRead) {
  const std::string url = "https://shop.example/item?id=7&utm_source=x&gclid=y#top";
  const std::string clean = WithoutQueryParam(url, "utm_source");
  EXPECT_TRUE(clean.find("utm_source") == std::string::npos);
  EXPECT_TRUE(clean.find("id=7") != std::string::npos) << clean;
  EXPECT_TRUE(clean.find("gclid=y") != std::string::npos) << clean;
  EXPECT_TRUE(clean.find("#top") != std::string::npos) << "the fragment is not a parameter";
  // Empty name means "all of them" - the copy-clean-link action.
  const std::string bare = WithoutQueryParam(url, "");
  EXPECT_TRUE(bare.find('?') == std::string::npos) << bare;
  EXPECT_TRUE(bare.find("#top") != std::string::npos);
  // A parameter that is not there changes nothing.
  EXPECT_EQ(WithoutQueryParam(url, "nope"), url);
}

TEST(UrlUtils, QueryParamDecodesPercentAndPlus) {
  EXPECT_EQ(QueryParam("https://x.example/?q=hello+world", "q"), std::string("hello world"));
  EXPECT_EQ(QueryParam("https://x.example/?q=caf%C3%A9", "q"), std::string("caf\xC3\xA9"));
  EXPECT_EQ(QueryParam("https://x.example/?q=100%25", "q"), std::string("100%"));
  EXPECT_EQ(QueryParam("https://x.example/?Q=up", "q"), std::string("up")) << "names are case-insensitive";
  EXPECT_TRUE(QueryParam("https://x.example/?a=1", "b").empty());
  EXPECT_TRUE(QueryParam("https://x.example/", "a").empty());
  // A truncated escape must not eat the character that follows it.
  EXPECT_EQ(QueryParam("https://x.example/?q=a%2", "q"), std::string("a%2"));
}

TEST(UrlUtils, RegistrableDomainHandlesMultiLabelSuffixes) {
  EXPECT_EQ(RegistrableDomain("www.example.com"), std::string("example.com"));
  EXPECT_EQ(RegistrableDomain("a.b.example.co.uk"), std::string("example.co.uk"));
  EXPECT_EQ(RegistrableDomain("cdn.example.co.uk"), std::string("example.co.uk"));
  EXPECT_EQ(RegistrableDomain("user.github.io"), std::string("user.github.io"))
      << "a user page on a shared host is its own site";
  EXPECT_EQ(RegistrableDomain("example.com."), std::string("example.com"))
      << "a trailing dot is the same host";
  EXPECT_EQ(RegistrableDomain("localhost"), std::string("localhost"));
  EXPECT_EQ(RegistrableDomain("127.0.0.1"), std::string("127.0.0.1"));
}

TEST(UrlUtils, SameSiteIgnoresSchemePortAndSubdomain) {
  EXPECT_TRUE(SameSite("https://a.example.com/x", "http://b.example.com/y"));
  EXPECT_TRUE(SameSite("https://example.com:8443/", "https://example.com/"));
  EXPECT_FALSE(SameSite("https://example.com/", "https://example.org/"));
  // The classic trap: "notexample.com" is not a subdomain of "example.com".
  EXPECT_FALSE(SameSite("https://example.com/", "https://notexample.com/"));
  // Same-site is defined on the host rather than the scheme - that is what the cookie
  // rules use - so a single-label host really is the same site.
  EXPECT_TRUE(SameSite("browserx://modes", "https://modes/"));
  EXPECT_FALSE(SameSite("browserx://modes", "about:blank")) << "no host, no site";
}

TEST(UrlUtils, SecureSchemesAndLocalhost) {
  EXPECT_TRUE(SchemeIsSecure("https://example.com/"));
  EXPECT_TRUE(SchemeIsSecure("browserx://settings"));
  EXPECT_FALSE(SchemeIsSecure("http://example.com/"));
  EXPECT_FALSE(SchemeIsSecure("http://localhost:3000/"))
      << "localhost over http is still not https - the page says so rather than lying";
  EXPECT_TRUE(IsLocalhost("localhost"));
  EXPECT_TRUE(IsLocalhost("app.localhost"));
  EXPECT_TRUE(IsLocalhost("127.0.0.1"));
  EXPECT_FALSE(IsLocalhost("localhost.example.com"));
}

TEST(UrlUtils, NormalizeForCompareIsStable) {
  EXPECT_EQ(NormalizeForCompare("https://example.com/a#frag"), std::string("https://example.com/a"));
  EXPECT_EQ(NormalizeForCompare("https://example.com/a/"), std::string("https://example.com/a"));
  EXPECT_EQ(NormalizeForCompare("https://example.com/a/?x=1#f"), std::string("https://example.com/a?x=1"));
  // The root slash goes too, so both spellings of one address collapse into a single
  // history entry rather than two.
  EXPECT_EQ(NormalizeForCompare("https://example.com/"), std::string("https://example.com"));
  EXPECT_EQ(NormalizeForCompare("https://example.com"), std::string("https://example.com"));
}
