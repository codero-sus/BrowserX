// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — routing the browser through Tor, and saying what that does not do.
//
// The switches below are the verifiable half. The warnings are the other half, and they
// are not decoration: "Tor mode" in a Chromium browser is a SOCKS5 proxy, and a SOCKS5
// proxy is not Tor Browser. Everything that makes Tor Browser resistant to fingerprinting
// — the letterboxed window, the fixed user agent, canvas and font mitigation, the
// disablement of a long list of APIs — is absent here, and a person who believes otherwise
// is less safe than one who knows they are on a proxy.
//
// So this file has two jobs of equal weight:
//   1. build the switch set that actually routes traffic, with the leaks closed where a
//      switch can close them;
//   2. enumerate, in plain words, what is still exposed.
//
// The probe (does something listen on the port?) lives in the app layer, because this file
// is core and stays dependency-free and unit-testable.

#ifndef BROWSERX_CORE_TOR_H_
#define BROWSERX_CORE_TOR_H_

#include <string>
#include <vector>

#include "core/mode_engine.h"

namespace browserx {
namespace core {

/// Tor's default SOCKS port. Pluggable-transport and multi-instance setups differ, so the
/// port is a parameter everywhere rather than a constant used directly.
constexpr int kDefaultTorSocksPort = 9050;

/// Where traffic goes. Kept as data so the UI can show it without parsing a switch.
struct TorPlan {
  int port = kDefaultTorSocksPort;
  /// Ready-to-apply switches. Reusing PlannedSwitch means the boot path in CefHost applies
  /// them exactly like a mode's switches — one mechanism, one place that can be wrong.
  std::vector<PlannedSwitch> switches;
  /// What the user is still exposed to. Never empty: a plan that claimed to be complete
  /// would be a lie, and an empty list would say that.
  std::vector<std::string> warnings;
};

/// The switch set for routing through Tor on `port`.
TorPlan MakeTorPlan(int port = kDefaultTorSocksPort);

/// The leak surface, in plain language. Empty input is not possible: this always returns
/// at least the "not Tor Browser" warning.
std::vector<std::string> TorWarnings();

/// What to tell the user when the probe has run. `reachable` comes from the app layer,
/// which is the only part allowed to open a socket.
std::string DescribeTorStatus(bool reachable, int port);

/// Is `port` a usable TCP port number? Rejects 0 and out-of-range values, so a typo in
/// `--tor-port=905O` cannot become a silently broken proxy configuration.
bool ValidTorPort(int port);

/// Parse `--tor-port=<value>`; returns false and leaves `out` alone when it is not a port.
bool ParseTorPort(const std::string& text, int* out);

/// The one-line explanation shown in the status bar when Tor mode is on. Short enough for a
/// status bar, honest enough to be worth the space.
std::string TorStatusBarText(bool reachable, int port);

}  // namespace core
}  // namespace browserx

#endif  // BROWSERX_CORE_TOR_H_
