// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

// BrowserX — "is a Tor process actually listening?"
//
// This lives in the app layer, not in core, for one reason: it opens a socket, and core is
// kept free of platform I/O so its tests run anywhere. The question it answers is narrow on
// purpose - a TCP connect either succeeds or it does not. It does not speak the SOCKS
// protocol and does not claim the listener is Tor; a port could be anything. Combined with
// the warnings in core/tor.h, that is the honest amount to claim.

#ifndef BROWSERX_APP_TOR_PROBE_H_
#define BROWSERX_APP_TOR_PROBE_H_

#include <string>

namespace browserx {
namespace app {

struct TorProbeResult {
  bool reachable = false;
  int port = 0;
  /// Plain words for the UI: what was tried, and what happened.
  std::string detail;
  /// Milliseconds the connect attempt took, for the status line.
  int elapsed_ms = 0;
};

/// Tries to connect to 127.0.0.1:<port> and closes the socket immediately.
/// Never blocks longer than `timeout_ms`; never throws.
TorProbeResult ProbeTor(int port, int timeout_ms = 300);

}  // namespace app
}  // namespace browserx

#endif  // BROWSERX_APP_TOR_PROBE_H_
