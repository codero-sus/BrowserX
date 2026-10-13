// Unit tests for omnibox input parsing (src/core/url_utils.cc).

#include <string>

#include "core/url_utils.h"
#include "test_harness.h"

namespace {

using mybrowser::core::url::BuildSearchUrl;
using mybrowser::core::url::EncodePath;
using mybrowser::core::url::EncodeQuery;
using mybrowser::core::url::HasScheme;
using mybrowser::core::url::HostMatchesPattern;
using mybrowser::core::url::HostOf;
using mybrowser::core::url::IsHttp;
using mybrowser::core::url::IsInternal;
using mybrowser::core::url::IsPrivileged;
using mybrowser::core::url::LooksLikeUrl;
using mybrowser::core::url::PortOf;
using mybrowser::core::url::PrettyForDisplay;
using mybrowser::core::url::RegistrableHostForDisplay;
using mybrowser::core::url::ResolveInput;
using mybrowser::core::url::SameOrigin;
using mybrowser::core::url::SanitizeInput;
using mybrowser::core::url::SchemeOf;
using mybrowser::core::url::StripFragment;
using mybrowser::core::url::StripTrackingParams;

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
  EXPECT_TRUE(HasScheme("mybrowser://settings"));
  EXPECT_TRUE(HasScheme("about:blank"));
  EXPECT_TRUE(HasScheme("mailto:a@b.c"));
  EXPECT_FALSE(HasScheme("example.com"));
  EXPECT_FALSE(HasScheme("localhost:8080"));
  EXPECT_EQ(SchemeOf("HTTPS://Example.com/A"), std::string("https"));
  EXPECT_EQ(SchemeOf("mybrowser://telemetry"), std::string("mybrowser"));
  EXPECT_EQ(SchemeOf("example.com"), std::string(""));
}

TEST(Url, HostAndPortExtraction) {
  EXPECT_EQ(HostOf("https://user:pw@Example.com:8443/x"), std::string("example.com"));
  EXPECT_EQ(HostOf("https://www.example.com"), std::string("www.example.com"));
  EXPECT_EQ(HostOf("mybrowser://telemetry"), std::string("telemetry"));
  EXPECT_EQ(HostOf("about:blank"), std::string(""));
  EXPECT_EQ(RegistrableHostForDisplay("www.example.com"), std::string("example.com"));
  EXPECT_EQ(RegistrableHostForDisplay("example.com"), std::string("example.com"));
  EXPECT_EQ(PortOf("https://example.com:8443/"), 8443);
  EXPECT_EQ(PortOf("https://example.com/"), 0);
}

TEST(Url, SchemeFamilies) {
  EXPECT_TRUE(IsHttp("https://example.com"));
  EXPECT_TRUE(IsHttp("http://example.com"));
  EXPECT_FALSE(IsHttp("mybrowser://settings"));
  EXPECT_FALSE(IsHttp("about:blank"));
  EXPECT_TRUE(IsInternal("mybrowser://settings"));
  EXPECT_TRUE(IsInternal("mybrowser://telemetry?x=1"));
  EXPECT_FALSE(IsInternal("https://mybrowser.com"));
  EXPECT_TRUE(IsPrivileged("about:blank"));
  EXPECT_TRUE(IsPrivileged("view-source:https://example.com"));
  EXPECT_FALSE(IsPrivileged("https://example.com"));
}

TEST(Url, LooksLikeUrlHeuristics) {
  EXPECT_TRUE(LooksLikeUrl("example.com"));
  EXPECT_TRUE(LooksLikeUrl("https://example.com/x"));
  EXPECT_TRUE(LooksLikeUrl("localhost:8080"));
  EXPECT_TRUE(LooksLikeUrl("192.168.1.1/admin"));
  EXPECT_TRUE(LooksLikeUrl("mybrowser://telemetry"));
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
  EXPECT_EQ(PrettyForDisplay("mybrowser://telemetry"), std::string("mybrowser://telemetry"));
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
  EXPECT_TRUE(mybrowser::core::url::TrackingParamPrefixes().size() > 3);
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
