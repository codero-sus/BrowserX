// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// Tests for the omnibox security indicator.
//
// The failure this prevents is subtle and common: an indicator that shows a padlock for
// something it has not verified. Every case below is a URL shape that actually occurs, and
// the assertion is that http:// is never mistaken for secure - including the shapes that a
// naive prefix check gets wrong (`HTTP://`, `https.evil.example`, a relative path with a
// colon in it).

#include "core/security_state.h"

#include <string>

#include "test_harness.h"

using namespace browserx::core;

TEST(SecurityState, HttpsIsSecureAndNothingElseIs) {
  EXPECT_TRUE(ClassifyTransport("https://example.com") == Transport::kSecure);
  EXPECT_TRUE(ClassifyTransport("HTTPS://EXAMPLE.COM") == Transport::kSecure);
  EXPECT_TRUE(ClassifyTransport("http://example.com") == Transport::kInsecure);
  EXPECT_TRUE(ClassifyTransport("HTTP://example.com") == Transport::kInsecure);
}

TEST(SecurityState, LookalikeSchemesAreNotSecure) {
  // A host that starts with "https" must not be read as the https scheme. This is the bug
  // a `starts_with("https")` check ships with.
  EXPECT_TRUE(ClassifyTransport("https.evil.example/path") == Transport::kUnknown);
  EXPECT_TRUE(ClassifyTransport("https:/example.com") == Transport::kSecure);   // still the scheme
  EXPECT_TRUE(ClassifyTransport("httpsfoo://example.com") == Transport::kUnknown);
}

TEST(SecurityState, InternalAndLocalPagesAreNamedAsSuch) {
  EXPECT_TRUE(ClassifyTransport("browserx://newtab") == Transport::kInternal);
  EXPECT_TRUE(ClassifyTransport("browserx://settings") == Transport::kInternal);
  EXPECT_TRUE(ClassifyTransport("file:///home/x/page.html") == Transport::kLocal);
}

TEST(SecurityState, UnknownAndRelativeAreNotClaimedEitherWay) {
  EXPECT_TRUE(ClassifyTransport("about:blank") == Transport::kUnknown);
  EXPECT_TRUE(ClassifyTransport("") == Transport::kUnknown);
  // A relative path with a colon in it is not a URL with a scheme.
  EXPECT_TRUE(ClassifyTransport("/a/b:c") == Transport::kUnknown);
  EXPECT_TRUE(ClassifyTransport("example.com/a:b") == Transport::kUnknown);
  EXPECT_TRUE(ClassifyTransport("devtools://devtools/bundled/x") == Transport::kUnknown);
}

TEST(SecurityState, OnlyPlainHttpIsAWarning) {
  EXPECT_TRUE(NeedsWarning(Transport::kInsecure));
  EXPECT_TRUE(!NeedsWarning(Transport::kSecure));
  EXPECT_TRUE(!NeedsWarning(Transport::kInternal));
  EXPECT_TRUE(!NeedsWarning(Transport::kLocal));
  // "Unknown" is not "dangerous": saying either would be a claim we cannot support.
  EXPECT_TRUE(!NeedsWarning(Transport::kUnknown));
}

TEST(SecurityState, TheSecureAndInsecureGlyphsDifferVisibly) {
  EXPECT_TRUE(std::string(TransportGlyph(Transport::kSecure)) !=
              std::string(TransportGlyph(Transport::kInsecure)));
  EXPECT_TRUE(std::string(TransportGlyph(Transport::kSecure)).size() > 0);
  EXPECT_TRUE(std::string(TransportLabel(Transport::kInsecure)) == "Not secure");
  EXPECT_TRUE(std::string(TransportLabel(Transport::kSecure)) == "Secure");
}

TEST(SecurityState, TheDetailSaysWhatItMeansRatherThanWhatItIsCalled) {
  const std::string insecure = TransportDetail(Transport::kInsecure);
  EXPECT_TRUE(insecure.find("read") != std::string::npos);
  EXPECT_TRUE(insecure.find("change") != std::string::npos);
  // The honest distinction: plain HTTP is not the same claim as "this site is bad".
  EXPECT_TRUE(insecure.find("not the same as the site being malicious") != std::string::npos);

  const std::string secure = TransportDetail(Transport::kSecure);
  EXPECT_TRUE(secure.find("certificate") != std::string::npos);
  // And the other direction of the same honesty: a padlock is not a trustworthiness mark.
  EXPECT_TRUE(secure.find("nothing about whether the site is honest") != std::string::npos);

  // Every state explains itself; none returns an empty sentence.
  for (const Transport transport : {Transport::kSecure, Transport::kInsecure,
                                    Transport::kInternal, Transport::kLocal,
                                    Transport::kUnknown}) {
    EXPECT_TRUE(!TransportDetail(transport).empty());
    EXPECT_TRUE(TransportDetail(transport).size() > 40);
  }
}
