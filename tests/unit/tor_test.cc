// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// Tests for Tor mode. Two things are being protected here, and the second matters more.
//
// The switches: each one closes a specific leak, and a typo in one of them would leave a
// browsing session that *says* it is routed through Tor while leaking DNS, QUIC or WebRTC
// around it. The assertions are exact strings for that reason - these are values Chromium
// parses, not prose.
//
// The warnings: Tor mode must never present itself as anonymity. A build that quietly
// dropped the caveats would be more dangerous than one without the feature.

#include "core/tor.h"

#include <string>
#include <vector>

#include "test_harness.h"

using namespace browserx::core;

namespace {

bool HasSwitch(const TorPlan& plan, const std::string& id, const std::string& value) {
  for (const PlannedSwitch& entry : plan.switches) {
    if (entry.id == id && entry.value == value) return true;
  }
  return false;
}

bool Mentions(const std::vector<std::string>& lines, const std::string& text) {
  for (const std::string& line : lines) {
    if (line.find(text) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

TEST(Tor, TheProxyIsSocks5WithRemoteDns) {
  const TorPlan plan = MakeTorPlan(9050);
  EXPECT_TRUE(HasSwitch(plan, "proxy-server", "socks5://127.0.0.1:9050"));
  // socks4 would resolve locally and hand the exit node an IP address; that is the classic
  // leak this test exists to prevent.
  EXPECT_TRUE(!HasSwitch(plan, "proxy-server", "socks4://127.0.0.1:9050"));
}

TEST(Tor, EverySwitchTheLeakSurfaceNeedsIsPresent) {
  const TorPlan plan = MakeTorPlan(9150);
  EXPECT_TRUE(HasSwitch(plan, "proxy-server", "socks5://127.0.0.1:9150"));
  EXPECT_TRUE(HasSwitch(plan, "disable-quic", ""));                 // UDP ignores the proxy
  EXPECT_TRUE(HasSwitch(plan, "force-webrtc-ip-handling-policy", "disable_non_proxied_udp"));
  bool resolver_rule = false;
  for (const PlannedSwitch& entry : plan.switches) {
    if (entry.id == "host-resolver-rules" &&
        entry.value.find("~NOTFOUND") != std::string::npos) {
      resolver_rule = true;
    }
  }
  EXPECT_TRUE(resolver_rule);   // a local lookup would leak the site name
  EXPECT_TRUE(plan.switches.size() >= 4);
}

TEST(Tor, SwitchesSayWhyTheyExistAndThatARestartIsNeeded) {
  const TorPlan plan = MakeTorPlan();
  for (const PlannedSwitch& entry : plan.switches) {
    EXPECT_TRUE(!entry.reason.empty());
    EXPECT_TRUE(entry.reason.size() > 20);
    // Chromium reads these before any page exists, so "applies live" would be a lie.
    EXPECT_TRUE(entry.restart_required);
  }
}

TEST(Tor, AnInvalidPortFallsBackInsteadOfBuildingABrokenProxy) {
  const TorPlan zero = MakeTorPlan(0);
  EXPECT_TRUE(zero.port == kDefaultTorSocksPort);
  const TorPlan huge = MakeTorPlan(70000);
  EXPECT_TRUE(huge.port == kDefaultTorSocksPort);
  const TorPlan normal = MakeTorPlan(9150);
  EXPECT_TRUE(normal.port == 9150);
}

TEST(Tor, PortParsingRejectsAnythingThatIsNotAPort) {
  int port = 0;
  EXPECT_TRUE(ParseTorPort("9150", &port));
  EXPECT_TRUE(port == 9150);
  EXPECT_TRUE(!ParseTorPort("905O", &port));   // letter O, a plausible typo
  EXPECT_TRUE(!ParseTorPort("", &port));
  EXPECT_TRUE(!ParseTorPort("0", &port));
  EXPECT_TRUE(!ParseTorPort("65536", &port));
  EXPECT_TRUE(!ParseTorPort("-1", &port));
  EXPECT_TRUE(!ParseTorPort("9050 ", &port));
  EXPECT_TRUE(!ParseTorPort("999999", &port));
}

TEST(Tor, ItNeverClaimsToBeTorBrowser) {
  const std::vector<std::string> warnings = TorWarnings();
  EXPECT_TRUE(warnings.size() >= 5);
  // The first warning is the one that matters most and must lead with the distinction.
  EXPECT_TRUE(warnings.front().find("not Tor Browser") != std::string::npos);
  EXPECT_TRUE(Mentions(warnings, "Fingerprinting"));
  EXPECT_TRUE(Mentions(warnings, "exit node"));
  EXPECT_TRUE(Mentions(warnings, "Extensions"));
  EXPECT_TRUE(Mentions(warnings, "Tor Browser"));
  // A plan can never be handed out with an empty warning list - that would read as "safe".
  EXPECT_TRUE(!MakeTorPlan().warnings.empty());
}

TEST(Tor, TheStatusTextDistinguishesReachableFromNot) {
  const std::string up = DescribeTorStatus(true, 9050);
  EXPECT_TRUE(up.find("something is listening") != std::string::npos);
  EXPECT_TRUE(up.find("not Tor Browser") != std::string::npos);
  const std::string down = DescribeTorStatus(false, 9050);
  EXPECT_TRUE(down.find("nothing is listening") != std::string::npos);
  // The important property: a missing Tor must not suggest a direct connection.
  EXPECT_TRUE(down.find("fail to load") != std::string::npos);

  const std::string bar_up = TorStatusBarText(true, 9050);
  EXPECT_TRUE(bar_up.find("9050") != std::string::npos);
  EXPECT_TRUE(TorStatusBarText(false, 9050).find("no Tor process") != std::string::npos);
}

TEST(Tor, PortNumbersAppearInTheSwitchAndTheStatus) {
  const TorPlan plan = MakeTorPlan(19050);
  EXPECT_TRUE(HasSwitch(plan, "proxy-server", "socks5://127.0.0.1:19050"));
  EXPECT_TRUE(DescribeTorStatus(true, 19050).find("19050") != std::string::npos);
}
