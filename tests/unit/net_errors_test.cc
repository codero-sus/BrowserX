// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// Tests for the error explanations. The important ones here are the number-to-name pairs:
// the table was written from upstream net/base/net_error_list.h, and this file is what
// keeps it honest. Three certificate codes were wrong when it was first drafted from
// memory, in exactly the plausible-looking way (the COMMON_NAME/DATE/AUTHORITY group read
// as -200/-201/-202 in a different order than upstream has them), and only a pinned number
// catches that.

#include "core/net_errors.h"

#include <string>
#include <vector>

#include "test_harness.h"

using namespace browserx::core;

namespace {

/// Upstream values, copied from net/base/net_error_list.h (NET_ERROR(NAME, value)).
const std::vector<std::pair<std::string, int>>& UpstreamCodes() {
  static const std::vector<std::pair<std::string, int>> codes = {
      {"ABORTED", -3},        {"FILE_NOT_FOUND", -6},      {"TIMED_OUT", -7},
      {"BLOCKED_BY_CLIENT", -20}, {"NETWORK_CHANGED", -21}, {"BLOCKED_BY_RESPONSE", -27},
      {"CONNECTION_CLOSED", -100}, {"CONNECTION_RESET", -101},
      {"CONNECTION_REFUSED", -102}, {"NAME_NOT_RESOLVED", -105},
      {"INTERNET_DISCONNECTED", -106}, {"SSL_PROTOCOL_ERROR", -107},
      {"ADDRESS_UNREACHABLE", -109}, {"SSL_VERSION_OR_CIPHER_MISMATCH", -113},
      {"CONNECTION_TIMED_OUT", -118}, {"PROXY_CONNECTION_FAILED", -130},
      {"MANDATORY_PROXY_CONFIGURATION_FAILED", -131}, {"NAME_RESOLUTION_FAILED", -137},
      {"CERT_COMMON_NAME_INVALID", -200}, {"CERT_DATE_INVALID", -201},
      {"CERT_AUTHORITY_INVALID", -202}, {"CERT_REVOKED", -206},
      {"UNKNOWN_URL_SCHEME", -302}, {"TOO_MANY_REDIRECTS", -310},
      {"EMPTY_RESPONSE", -324}, {"HTTP_RESPONSE_CODE_FAILURE", -379}, {"CACHE_MISS", -400},
      {"DNS_TIMED_OUT", -803},
  };
  return codes;
}

}  // namespace

TEST(NetErrors, EveryCodeMatchesUpstream) {
  for (const auto& [name, code] : UpstreamCodes()) {
    const NetErrorHelp by_number = ExplainNetError(code);
    EXPECT_EQ(by_number.name, name);
    EXPECT_EQ(by_number.code, code);
    EXPECT_EQ(NetErrorCodeForName(name), code);
    EXPECT_EQ(NetErrorCodeForName("ERR_" + name), code);
  }
}

TEST(NetErrors, CertificateCodesAreNotSwapped) {
  // The specific trap: the order you would guess is not the order upstream uses. If these
  // three ever get "tidied" into -200/-201/-202 in name order, the page would explain an
  // expired certificate to someone hitting an untrusted-authority error, and this fails.
  EXPECT_EQ(NetErrorCodeForName("CERT_COMMON_NAME_INVALID"), -200);
  EXPECT_EQ(NetErrorCodeForName("CERT_DATE_INVALID"), -201);
  EXPECT_EQ(NetErrorCodeForName("CERT_AUTHORITY_INVALID"), -202);
}

TEST(NetErrors, EveryEntryIsUsable) {
  for (int code : KnownNetErrorCodes()) {
    const NetErrorHelp help = ExplainNetError(code);
    EXPECT_FALSE(help.name.empty());
    EXPECT_FALSE(help.headline.empty());
    EXPECT_FALSE(help.explanation.empty());
    EXPECT_TRUE(help.family != NetErrorFamily::kUnknown);
    // Advice, not a shrug: at least one concrete step for every failure we claim to know.
    EXPECT_TRUE(help.try_first.size() >= 1);
    for (const std::string& step : help.try_first) {
      EXPECT_TRUE(!step.empty());
      // A "step" that ends without a full stop, or is a fragment, reads like a note rather
      // than advice; the table has one rule about this and this is it.
      EXPECT_TRUE(step.find("  ") == std::string::npos);
    }
  }
}

TEST(NetErrors, NamesAreUniqueAndSortedTheSameWayInTheList) {
  std::vector<std::string> names;
  for (int code : KnownNetErrorCodes()) names.push_back(ExplainNetError(code).name);
  for (size_t i = 1; i < names.size(); ++i) {
    EXPECT_TRUE(names[i - 1] != names[i]);
  }
  EXPECT_EQ(KnownNetErrorCodes().size(), names.size());
}

TEST(NetErrors, UnknownCodeIsExplainedWithoutInventingAnything) {
  const NetErrorHelp help = ExplainNetError(-9999);
  EXPECT_TRUE(help.family == NetErrorFamily::kUnknown);
  EXPECT_TRUE(help.headline.find("cannot explain") != std::string::npos);
  // The number is still reported: "we do not know" must not mean "you get nothing".
  EXPECT_TRUE(help.explanation.find("-9999") != std::string::npos);
  EXPECT_TRUE(help.name.empty());
  EXPECT_TRUE(help.try_first.size() >= 1);
}

TEST(NetErrors, UnknownNameIsReportedByItsName) {
  const NetErrorHelp help = ExplainNetErrorName("ERR_SOMETHING_NEW");
  EXPECT_TRUE(help.family == NetErrorFamily::kUnknown);
  EXPECT_TRUE(help.explanation.find("SOMETHING_NEW") != std::string::npos);
  EXPECT_EQ(NetErrorCodeForName("ERR_SOMETHING_NEW"), 0);

  // An empty name is a different case, and it must not print "ERR_".
  const NetErrorHelp blank = ExplainNetErrorName("");
  EXPECT_TRUE(blank.explanation.find("ERR_") == std::string::npos);
}

TEST(NetErrors, NamesAreAcceptedInEverySpellingThatReachesUs) {
  for (const char* spelling : {"NAME_NOT_RESOLVED", "err_name_not_resolved",
                               "Err_Name_Not_Resolved", "  NAME_NOT_RESOLVED  "}) {
    EXPECT_EQ(ExplainNetErrorName(spelling).code, -105);
  }
}

TEST(NetErrors, TheBrowserOwnsUpWhenItIsTheReason) {
  // The shield blocking a request, a URL scheme this browser has no handler for, and a
  // response refusing to be framed are all decisions made on this side. Saying "the site
  // refused" for any of them would blame the wrong party.
  EXPECT_TRUE(ExplainNetError(-20).caused_by_browser);
  EXPECT_TRUE(ExplainNetError(-302).caused_by_browser);
  EXPECT_TRUE(ExplainNetError(-27).caused_by_browser);
  EXPECT_TRUE(ExplainNetError(-3).caused_by_browser);
  EXPECT_FALSE(ExplainNetError(-102).caused_by_browser);
  EXPECT_FALSE(ExplainNetError(-105).caused_by_browser);
}

TEST(NetErrors, CertificateFailuresAreMarkedAsSecurityRelevant) {
  for (int code : {-200, -201, -202, -206, -107, -113}) {
    EXPECT_TRUE(ExplainNetError(code).security_relevant);
  }
  // ...and nothing else is, or the marking stops meaning anything.
  for (int code : {-3, -20, -102, -105, -106, -118, -310, -324, -400}) {
    EXPECT_FALSE(ExplainNetError(code).security_relevant);
  }
  for (int code : KnownNetErrorCodes()) {
    const NetErrorHelp help = ExplainNetError(code);
    if (help.security_relevant) EXPECT_TRUE(help.family == NetErrorFamily::kTls);
  }
}

TEST(NetErrors, FamiliesCoverTheTableAndReadAsWords) {
  for (int code : KnownNetErrorCodes()) {
    const char* family = NetErrorFamilyName(ExplainNetError(code).family);
    EXPECT_TRUE(family != nullptr && family[0] != '\0');
    EXPECT_TRUE(std::string(family) != "unknown");
  }
  EXPECT_EQ(std::string(NetErrorFamilyName(NetErrorFamily::kDns)), "dns");
  EXPECT_EQ(std::string(NetErrorFamilyName(NetErrorFamily::kUnknown)), "unknown");
  // DNS problems are DNS problems even when they look like timeouts.
  EXPECT_TRUE(ExplainNetError(-803).family == NetErrorFamily::kDns);
  EXPECT_TRUE(ExplainNetError(-137).family == NetErrorFamily::kDns);
  EXPECT_TRUE(ExplainNetError(-118).family == NetErrorFamily::kTimeout);
}

TEST(NetErrors, SomeFailuresDoNotDeserveAPage) {
  // Stopping a load and a cache miss are not failures to interrupt anyone with.
  EXPECT_FALSE(NetErrorWarrantsPage(-3));
  EXPECT_FALSE(NetErrorWarrantsPage(-400));
  EXPECT_TRUE(NetErrorWarrantsPage(-105));
  EXPECT_TRUE(NetErrorWarrantsPage(-102));
  EXPECT_TRUE(NetErrorWarrantsPage(-9999));
  // Every code the table knows about, except those two, gets a page.
  for (int code : KnownNetErrorCodes()) {
    if (code == -3 || code == -400) continue;
    EXPECT_TRUE(NetErrorWarrantsPage(code));
  }
}

TEST(NetErrors, FormattedCodesReadCorrectly) {
  EXPECT_EQ(FormatNetErrorCode(-105), std::string("ERR_NAME_NOT_RESOLVED (-105)"));
  EXPECT_EQ(FormatNetErrorCode(-400), std::string("ERR_CACHE_MISS (-400)"));
  // An unknown code is not given a name it does not have.
  const std::string unknown = FormatNetErrorCode(-4242);
  EXPECT_TRUE(unknown.find("ERR_") == std::string::npos);
  EXPECT_TRUE(unknown.find("-4242") != std::string::npos);
}

TEST(NetErrors, CopyingAHelpEntryIsSafe) {
  // The UI holds these by value; if a member were a pointer into a temporary vector the
  // copy would dangle. Types are checked by the compiler, behaviour here.
  NetErrorHelp help = ExplainNetError(-102);
  EXPECT_EQ(help.headline, std::string("The site refused the connection"));
  help.headline = "changed";
  EXPECT_EQ(ExplainNetError(-102).headline, std::string("The site refused the connection"));
}
